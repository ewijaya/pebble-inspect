#include <pebble.h>
#include "storage.h"
#include "codec.h"

/* Two independent banks: write chunks, then commit their small manifest last.
 * A failed or interrupted save leaves the last complete bank recoverable. */
#define BANK_BASE 7100u
#define BANK_STRIDE 16u
#define CHUNK_SIZE 256u
#define MANIFEST_SIZE 20u
static uint8_t s_buffer[INSPECT_CODEC_MAX_SIZE];
static uint32_t s_generation;
static int s_bank = -1;
static const char *s_status = "New history";
typedef enum {
  LOAD_CLEAN,
  LOAD_RECOVERED,
  LOAD_INVALID,
  LOAD_INTERRUPTED
} LoadWarning;
/* Keep the launch diagnosis visible after the immediate startup checkpoint. */
static LoadWarning s_load_warning;

static bool bank_has_data(int bank) {
  uint32_t base = BANK_BASE + (uint32_t)bank * BANK_STRIDE;
  if (persist_exists(base)) return true;
  const uint32_t chunks = (INSPECT_CODEC_MAX_SIZE + CHUNK_SIZE - 1u) / CHUNK_SIZE;
  for (uint32_t chunk = 1; chunk <= chunks; ++chunk) {
    if (persist_exists(base + chunk)) return true;
  }
  return false;
}

static uint32_t checksum(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static uint32_t read_u32(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void write_u32(uint8_t *bytes, uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (i * 8));
}

static bool manifest_read(int bank, uint32_t *generation, uint32_t *size, uint32_t *blob_crc) {
  uint8_t manifest[MANIFEST_SIZE];
  uint32_t key = BANK_BASE + (uint32_t)bank * BANK_STRIDE;
  if (persist_get_size(key) != MANIFEST_SIZE ||
      persist_read_data(key, manifest, sizeof(manifest)) != MANIFEST_SIZE) return false;
  if (read_u32(manifest) != 0x31545349u ||
      read_u32(manifest + 16) != checksum(manifest, 16)) return false;
  *generation = read_u32(manifest + 4);
  *size = read_u32(manifest + 8);
  *blob_crc = read_u32(manifest + 12);
  return *size > 0 && *size <= sizeof(s_buffer);
}

static bool bank_load(int bank, uint32_t size, uint32_t blob_crc, InspectModel *model) {
  for (uint32_t offset = 0; offset < size; offset += CHUNK_SIZE) {
    uint32_t amount = size - offset;
    if (amount > CHUNK_SIZE) amount = CHUNK_SIZE;
    uint32_t key = BANK_BASE + (uint32_t)bank * BANK_STRIDE + 1u + offset / CHUNK_SIZE;
    if (persist_get_size(key) != (int)amount ||
        persist_read_data(key, s_buffer + offset, amount) != (int)amount) return false;
  }
  return checksum(s_buffer, size) == blob_crc && inspect_codec_decode(model, s_buffer, size);
}

void inspect_storage_load(InspectModel *model) {
  s_generation = 0;
  s_bank = -1;
  s_status = "New history";
  s_load_warning = LOAD_CLEAN;
  uint32_t generation[2] = {0}, size[2] = {0}, blob_crc[2] = {0};
  bool valid[2] = {manifest_read(0, &generation[0], &size[0], &blob_crc[0]),
                   manifest_read(1, &generation[1], &size[1], &blob_crc[1])};
  /* Signed difference preserves ordering across uint32 generation wrap. */
  int first = valid[1] && (!valid[0] || (int32_t)(generation[1] - generation[0]) > 0) ? 1 : 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    int bank = attempt == 0 ? first : 1 - first;
    if (valid[bank] && bank_load(bank, size[bank], blob_crc[bank], model)) {
      s_bank = bank;
      s_generation = generation[bank];
      bool damaged_other = !valid[1 - bank] && bank_has_data(1 - bank);
      if (attempt != 0 || damaged_other) {
        s_load_warning = LOAD_RECOVERED;
        s_status = "Older history recovered";
      } else {
        s_status = "History restored";
      }
      return;
    }
  }
  if (persist_exists(BANK_BASE) || persist_exists(BANK_BASE + BANK_STRIDE)) {
    s_load_warning = LOAD_INVALID;
    s_status = "Invalid history ignored";
    APP_LOG(APP_LOG_LEVEL_WARNING, "Stored history invalid; starting empty");
  } else if (bank_has_data(0) || bank_has_data(1)) {
    s_load_warning = LOAD_INTERRUPTED;
    s_status = "Interrupted history ignored";
    APP_LOG(APP_LOG_LEVEL_WARNING, "Stored history interrupted; starting empty");
  }
}

bool inspect_storage_save(const InspectModel *model) {
  size_t size = inspect_codec_encode(model, s_buffer, sizeof(s_buffer));
  if (size == 0) goto failed;
  int bank = s_bank == 0 ? 1 : 0;
  for (size_t offset = 0; offset < size; offset += CHUNK_SIZE) {
    size_t amount = size - offset;
    if (amount > CHUNK_SIZE) amount = CHUNK_SIZE;
    uint32_t key = BANK_BASE + (uint32_t)bank * BANK_STRIDE + 1u + offset / CHUNK_SIZE;
    if (persist_write_data(key, s_buffer + offset, amount) != (int)amount) goto failed;
    uint8_t verify[CHUNK_SIZE];
    if (persist_read_data(key, verify, amount) != (int)amount ||
        memcmp(verify, s_buffer + offset, amount) != 0) goto failed;
  }
  uint8_t manifest[MANIFEST_SIZE];
  write_u32(manifest, 0x31545349u);
  write_u32(manifest + 4, s_generation + 1u);
  write_u32(manifest + 8, (uint32_t)size);
  write_u32(manifest + 12, checksum(s_buffer, size));
  write_u32(manifest + 16, checksum(manifest, 16));
  if (persist_write_data(BANK_BASE + (uint32_t)bank * BANK_STRIDE,
                         manifest, sizeof(manifest)) != (int)sizeof(manifest)) goto failed;
  uint8_t verify_manifest[MANIFEST_SIZE];
  if (persist_read_data(BANK_BASE + (uint32_t)bank * BANK_STRIDE,
                        verify_manifest, sizeof(verify_manifest)) != (int)sizeof(verify_manifest) ||
      memcmp(manifest, verify_manifest, sizeof(manifest)) != 0) goto failed;
  s_generation++;
  s_bank = bank;
  switch (s_load_warning) {
    case LOAD_RECOVERED: s_status = "Saved; recovered older history"; break;
    case LOAD_INVALID: s_status = "Saved; invalid history reset"; break;
    case LOAD_INTERRUPTED: s_status = "Saved; interrupted history reset"; break;
    default: s_status = "History saved"; break;
  }
  return true;
failed:
  s_status = "Save failed; history in RAM";
  APP_LOG(APP_LOG_LEVEL_ERROR, "History persistence failed");
  return false;
}

const char *inspect_storage_status(void) { return s_status; }
