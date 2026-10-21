#include "pebble.h"
#include "codec.h"
#include "storage.h"

#include <assert.h>
#include <stdio.h>

#define BASE 7100u
#define STRIDE 16u
#define SLOT_COUNT 32u

typedef struct {
  bool exists;
  size_t size;
  uint8_t bytes[256];
} Slot;

typedef enum { NO_FAULT, ERROR_FAULT, SHORT_FAULT, CORRUPT_FAULT } Fault;
static Slot slots[SLOT_COUNT];
static unsigned writes, reads;
static unsigned write_fault_at, read_fault_at;
static Fault write_fault, read_fault;

static Slot *slot(uint32_t key) {
  assert(key >= BASE && key - BASE < SLOT_COUNT);
  return &slots[key - BASE];
}

int persist_get_size(uint32_t key) {
  Slot *s = slot(key);
  return s->exists ? (int)s->size : -1;
}

int persist_read_data(uint32_t key, void *buffer, size_t size) {
  reads++;
  Slot *s = slot(key);
  if (!s->exists) return -1;
  size_t amount = s->size < size ? s->size : size;
  Fault fault = reads == read_fault_at ? read_fault : NO_FAULT;
  if (fault == ERROR_FAULT) return -1;
  if (fault == SHORT_FAULT && amount) amount--;
  memcpy(buffer, s->bytes, amount);
  if (fault == CORRUPT_FAULT && amount) ((uint8_t *)buffer)[0] ^= 1;
  return (int)amount;
}

int persist_write_data(uint32_t key, const void *data, size_t size) {
  assert(size > 0 && size <= 256);
  writes++;
  Fault fault = writes == write_fault_at ? write_fault : NO_FAULT;
  if (fault == ERROR_FAULT) return -1;
  Slot *s = slot(key);
  if (fault == SHORT_FAULT) size--;
  s->size = size;
  s->exists = true;
  memcpy(s->bytes, data, size);
  if (fault == CORRUPT_FAULT && size) s->bytes[0] ^= 1;
  size_t allowance = 0;
  for (unsigned i = 0; i < SLOT_COUNT; ++i) allowance += slots[i].size;
  assert(allowance <= 4096);
  return (int)size;
}

bool persist_exists(uint32_t key) { return slot(key)->exists; }

static void clear_faults(void) {
  writes = reads = write_fault_at = read_fault_at = 0;
  write_fault = read_fault = NO_FAULT;
}

static void reset(void) {
  memset(slots, 0, sizeof(slots));
  clear_faults();
  InspectModel m;
  inspect_model_init(&m);
  inspect_storage_load(&m);
  assert(!m.has_observation);
  assert(strcmp(inspect_storage_status(), "New history") == 0);
  clear_faults();
}

static InspectModel full_model(void) {
  InspectModel m;
  inspect_model_init(&m);
  for (uint32_t i = 0; i < 80; ++i) {
    InspectObservation o = { .timestamp = 1000 + i * 60,
      .battery_percent = (uint8_t)(100 - i), .connected = (i % 2) != 0 };
    inspect_model_observe(&m, &o);
  }
  assert(m.sample_count == INSPECT_SAMPLE_CAPACITY);
  assert(m.event_count == INSPECT_EVENT_CAPACITY);
  return m;
}

static void distinguish(InspectModel *m) {
  InspectObservation o = m->current;
  o.timestamp += 60;
  o.battery_percent--;
  o.connected = !o.connected;
  inspect_model_observe(m, &o);
}

static void check_loaded(const InspectModel *expected) {
  clear_faults();
  InspectModel restored;
  inspect_model_init(&restored);
  inspect_storage_load(&restored);
  assert(restored.has_observation == expected->has_observation);
  assert(!restored.active);
  assert(restored.last_observed == expected->last_observed);
  assert(restored.current.battery_percent == expected->current.battery_percent);
  assert(restored.connected_seconds == expected->connected_seconds);
  assert(restored.disconnected_seconds == expected->disconnected_seconds);
  assert(restored.drop_count == expected->drop_count);
  assert(restored.session_count == expected->session_count);
}

static uint32_t crc32(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static void manifest_crc(unsigned bank) {
  Slot *manifest = slot(BASE + bank * STRIDE);
  put32(manifest->bytes + 16, crc32(manifest->bytes, 16));
}

static void set_generation(unsigned bank, uint32_t generation) {
  put32(slot(BASE + bank * STRIDE)->bytes + 4, generation);
  manifest_crc(bank);
}

static void test_roundtrip_and_fallback(void) {
  reset();
  InspectModel first = full_model();
  assert(inspect_storage_save(&first));
  assert(writes == 4 && reads == 4);
  assert(u32(slot(BASE)->bytes + 4) == 1);
  check_loaded(&first);
  assert(strcmp(inspect_storage_status(), "History restored") == 0);
  InspectModel second = first;
  distinguish(&second);
  assert(inspect_storage_save(&second));
  assert(u32(slot(BASE + STRIDE)->bytes + 4) == 2);
  check_loaded(&second);
  Slot complete[SLOT_COUNT];
  memcpy(complete, slots, sizeof(slots));

  for (unsigned damage = 0; damage < 7; ++damage) {
    memcpy(slots, complete, sizeof(slots));
    if (damage == 0) slot(BASE + STRIDE)->bytes[0] ^= 1;
    if (damage == 1) slot(BASE + STRIDE)->size--;
    if (damage >= 2 && damage <= 4)
      slot(BASE + STRIDE + 1 + damage - 2)->bytes[0] ^= 1;
    if (damage == 5) slot(BASE + STRIDE + 1)->exists = false;
    if (damage == 6) slot(BASE + STRIDE + 2)->size--;
    check_loaded(&first);
    assert(strcmp(inspect_storage_status(), "Older history recovered") == 0);
  }
  memcpy(slots, complete, sizeof(slots));
  /* Both manifests corrupted: no partial state can leak into a fresh model. */
  slot(BASE)->bytes[0] ^= 1;
  slot(BASE + STRIDE)->bytes[0] ^= 1;
  InspectModel empty;
  inspect_model_init(&empty);
  check_loaded(&empty);
  assert(strcmp(inspect_storage_status(), "Invalid history ignored") == 0);
}

static void test_save_failures(void) {
  /* Empty store, one bank committed, and overwriting a previously valid bank. */
  for (unsigned prior_saves = 0; prior_saves < 3; ++prior_saves) {
    for (unsigned operation = 1; operation <= 4; ++operation) {
      for (Fault fault = ERROR_FAULT; fault <= CORRUPT_FAULT; ++fault) {
        for (unsigned reading = 0; reading < 2; ++reading) {
          reset();
          InspectModel old = full_model();
          for (unsigned saved = 0; saved < prior_saves; ++saved) {
            assert(inspect_storage_save(&old));
            if (saved + 1 < prior_saves) distinguish(&old);
          }
          InspectModel next = old;
          distinguish(&next);
          clear_faults();
          if (reading) { read_fault_at = operation; read_fault = fault; }
          else { write_fault_at = operation; write_fault = fault; }
          assert(!inspect_storage_save(&next));
          assert(strcmp(inspect_storage_status(), "Save failed; history in RAM") == 0);
          /* A complete manifest can have committed before its readback failed. */
          if (reading && operation == 4) {
            check_loaded(&next);
          } else if (prior_saves) {
            check_loaded(&old);
          } else {
            InspectModel empty;
            inspect_model_init(&empty);
            check_loaded(&empty);
          }
        }
      }
    }
  }
}

static void test_interrupted_first_save(void) {
  reset();
  InspectModel m = full_model();
  /* A crash before the manifest commit has no complete generation to recover. */
  write_fault_at = 3;
  write_fault = ERROR_FAULT;
  assert(!inspect_storage_save(&m));
  assert(persist_exists(BASE + 1) && persist_exists(BASE + 2));
  assert(!persist_exists(BASE));
  InspectModel empty;
  inspect_model_init(&empty);
  check_loaded(&empty);
  assert(strcmp(inspect_storage_status(), "Interrupted history ignored") == 0);
  assert(inspect_storage_save(&m));
  assert(strcmp(inspect_storage_status(), "Saved; interrupted history reset") == 0);
  assert(inspect_storage_save(&m));
  assert(strcmp(inspect_storage_status(), "Saved; interrupted history reset") == 0);
  check_loaded(&m);
}

static void check_sticky_warning(const InspectModel *m, const char *saved_status) {
  assert(inspect_storage_save(m));
  assert(strcmp(inspect_storage_status(), saved_status) == 0);
  assert(inspect_storage_save(m));
  assert(strcmp(inspect_storage_status(), saved_status) == 0);
  clear_faults();
  write_fault_at = 1;
  write_fault = ERROR_FAULT;
  assert(!inspect_storage_save(m));
  assert(strcmp(inspect_storage_status(), "Save failed; history in RAM") == 0);
  clear_faults();
  assert(inspect_storage_save(m));
  assert(strcmp(inspect_storage_status(), saved_status) == 0);
}

static void test_sticky_load_warnings(void) {
  reset();
  InspectModel old = full_model();
  assert(inspect_storage_save(&old));
  InspectModel next = old;
  distinguish(&next);
  assert(inspect_storage_save(&next));
  slot(BASE + STRIDE + 1)->bytes[0] ^= 1;
  check_loaded(&old);
  assert(strcmp(inspect_storage_status(), "Older history recovered") == 0);
  check_sticky_warning(&old, "Saved; recovered older history");

  slot(BASE)->bytes[0] ^= 1;
  slot(BASE + STRIDE)->bytes[0] ^= 1;
  InspectModel empty;
  inspect_model_init(&empty);
  check_loaded(&empty);
  assert(strcmp(inspect_storage_status(), "Invalid history ignored") == 0);
  check_sticky_warning(&old, "Saved; invalid history reset");

  reset();
  /* Scan includes the final bounded chunk key in either bank. */
  Slot *orphan = slot(BASE + STRIDE + INSPECT_CODEC_MAX_SIZE / 256);
  orphan->exists = true;
  orphan->size = 1;
  orphan->bytes[0] = 42;
  check_loaded(&empty);
  assert(strcmp(inspect_storage_status(), "Interrupted history ignored") == 0);
  check_sticky_warning(&old, "Saved; interrupted history reset");

  reset();
  assert(inspect_storage_save(&old));
  assert(strcmp(inspect_storage_status(), "History saved") == 0);
}

static void test_load_read_failures(void) {
  reset();
  InspectModel old = full_model();
  assert(inspect_storage_save(&old));
  InspectModel next = old;
  distinguish(&next);
  assert(inspect_storage_save(&next));
  for (unsigned operation = 1; operation <= 5; ++operation) {
    for (Fault fault = ERROR_FAULT; fault <= CORRUPT_FAULT; ++fault) {
      clear_faults();
      read_fault_at = operation;
      read_fault = fault;
      InspectModel restored;
      inspect_model_init(&restored);
      inspect_storage_load(&restored);
      /* Bank zero's manifest read failure leaves newest bank one unaffected. */
      const InspectModel *expected = operation == 1 ? &next : &old;
      assert(restored.has_observation && restored.last_observed == expected->last_observed);
    }
  }
}

static void test_schema_and_wrap(void) {
  reset();
  InspectModel old = full_model();
  assert(inspect_storage_save(&old));
  InspectModel next = old;
  distinguish(&next);
  assert(inspect_storage_save(&next));
  /* Keep both outer checksums valid, but encode an unsupported codec schema. */
  uint8_t blob[INSPECT_CODEC_MAX_SIZE];
  Slot *manifest = slot(BASE + STRIDE);
  uint32_t size = u32(manifest->bytes + 8);
  for (uint32_t at = 0; at < size; at += 256) {
    Slot *chunk = slot(BASE + STRIDE + 1 + at / 256);
    memcpy(blob + at, chunk->bytes, chunk->size);
  }
  blob[4]++;
  put32(blob + size - 4, crc32(blob, size - 4));
  for (uint32_t at = 0; at < size; at += 256) {
    Slot *chunk = slot(BASE + STRIDE + 1 + at / 256);
    memcpy(chunk->bytes, blob + at, chunk->size);
  }
  put32(manifest->bytes + 12, crc32(blob, size));
  manifest_crc(1);
  check_loaded(&old);
  assert(strcmp(inspect_storage_status(), "Older history recovered") == 0);

  reset();
  assert(inspect_storage_save(&old));
  assert(inspect_storage_save(&next));
  set_generation(0, UINT32_MAX);
  set_generation(1, UINT32_MAX - 1);
  check_loaded(&old);
  distinguish(&next);
  assert(inspect_storage_save(&next));
  assert(u32(slot(BASE + STRIDE)->bytes + 4) == 0);
  check_loaded(&next);
  distinguish(&next);
  assert(inspect_storage_save(&next));
  assert(u32(slot(BASE)->bytes + 4) == 1);
  check_loaded(&next);
}

static void test_unobserved_recovery(void) {
  reset();
  InspectModel m;
  inspect_model_init(&m);
  for (uint32_t minute = 0; minute <= 120; ++minute) {
    InspectObservation o = { .timestamp = 1000 + minute * 60,
      .battery_percent = (uint8_t)(80 - minute / 40), .connected = true };
    inspect_model_observe(&m, &o);
  }
  InspectEstimate e;
  inspect_model_estimate(&m, m.last_observed, &e);
  assert(e.status == INSPECT_ESTIMATE_READY);
  assert(inspect_storage_save(&m));
  InspectModel restored;
  inspect_model_init(&restored);
  inspect_storage_load(&restored);
  assert(!restored.active);
  assert(restored.events[restored.event_count - 1].kind == INSPECT_EVENT_UNKNOWN_END);
  InspectObservation o = restored.current;
  o.timestamp += 3600;
  o.battery_percent = 70;
  o.connected = false;
  inspect_model_start(&restored, &o);
  assert(restored.connected_seconds == m.connected_seconds);
  assert(restored.disconnected_seconds == m.disconnected_seconds);
  assert(restored.drop_count == m.drop_count);
  assert(restored.session_count == m.session_count + 1 && restored.gap_count == m.gap_count + 1);
  inspect_model_estimate(&restored, o.timestamp, &e);
  assert(e.status == INSPECT_ESTIMATE_INSUFFICIENT);
}

int main(void) {
  test_roundtrip_and_fallback();
  test_save_failures();
  test_interrupted_first_save();
  test_sticky_load_warnings();
  test_load_read_failures();
  test_schema_and_wrap();
  test_unobserved_recovery();
  puts("storage: generations, corruption fallback, write/read fault matrix, schema, wrap and observation gaps passed");
  return 0;
}
