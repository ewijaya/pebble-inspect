#include "codec.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static InspectObservation observation(uint32_t t, uint8_t percent, bool connected) {
  InspectObservation o = { .timestamp = t, .battery_percent = percent,
                           .connected = connected };
  return o;
}

static void observe(InspectModel *m, uint32_t t, uint8_t percent, bool connected) {
  InspectObservation o = observation(t, percent, connected);
  inspect_model_observe(m, &o);
}

static InspectEstimate estimate(const InspectModel *m, uint32_t now) {
  InspectEstimate e;
  inspect_model_estimate(m, now, &e);
  return e;
}

static void test_connection_scope(void) {
  InspectModel m;
  inspect_model_init(&m);
  assert(estimate(&m, 0).status == INSPECT_ESTIMATE_INSUFFICIENT);
  observe(&m, 1000, 80, true);
  assert(m.session_count == 1 && !m.drop_count && !m.connected_seconds);
  observe(&m, 1060, 80, false);
  assert(m.connected_seconds == 60 && m.drop_count == 1);
  observe(&m, 1120, 80, true);
  assert(m.disconnected_seconds == 60 && m.drop_count == 1);
  inspect_model_stop(&m, 1150);
  assert(!m.active && m.connected_seconds == 90);
  inspect_model_stop(&m, 1180);
  assert(m.connected_seconds == 90);
  observe(&m, 10000, 70, false);
  assert(m.session_count == 2 && m.gap_count == 1 && m.drop_count == 1);
  assert(m.disconnected_seconds == 60 && m.connected_seconds == 90);
  observe(&m, 10300, 70, true);
  assert(m.disconnected_seconds == 360);
  observe(&m, 10601, 70, false);
  assert(m.gap_count == 2 && m.drop_count == 1);
  assert(m.connected_seconds == 90);
  observe(&m, 9000, 70, true);
  assert(m.time_reset_count == 1 && m.gap_count == 3);
  assert(m.disconnected_seconds == 360);
  inspect_model_stop(&m, 8000);
  assert(m.time_reset_count == 2 && m.gap_count == 4);

  inspect_model_init(&m);
  observe(&m, 1000, 80, true);
  inspect_model_stop(&m, 1200);
  observe(&m, 1100, 80, true);
  assert(m.time_reset_count == 1 && m.connected_seconds == 200);
  observe(&m, UINT32_MAX - 1, 80, false);
  assert(m.gap_count == 2 && m.drop_count == 0 && m.connected_seconds == 200);
  observe(&m, 1, 80, true);
  assert(m.time_reset_count == 2 && m.gap_count == 3);
}

static void build_discharge(InspectModel *m, uint32_t start) {
  inspect_model_init(m);
  for (uint32_t minute = 0; minute <= 120; ++minute) {
    observe(m, start + minute * 60, (uint8_t)(80 - minute / 40), true);
  }
}

static void test_estimates(void) {
  InspectModel m;
  build_discharge(&m, 1000);
  InspectEstimate e = estimate(&m, 8200);
  assert(e.status == INSPECT_ESTIMATE_READY && e.span_seconds == 7200);
  assert(e.drop_percent == 3 && e.sample_count >= 3);
  assert(e.remaining_seconds == 77u * 7200u / 3u);
  assert(e.from_timestamp == 1000 && e.to_timestamp == 8200);
  assert(estimate(&m, 8500).status == INSPECT_ESTIMATE_READY);
  assert(estimate(&m, 8501).status == INSPECT_ESTIMATE_STALE);
  assert(estimate(&m, 10001).status == INSPECT_ESTIMATE_STALE);
  assert(estimate(&m, 8199).status == INSPECT_ESTIMATE_STALE);
  observe(&m, 8260, 78, true); /* A rise invalidates all earlier discharge. */
  assert(estimate(&m, 8260).status == INSPECT_ESTIMATE_INSUFFICIENT);
  assert(m.samples[m.sample_count - 1].segment == m.segment);
  InspectObservation o = observation(8320, 79, true);
  o.charging = true;
  o.plugged = true;
  inspect_model_observe(&m, &o);
  assert(estimate(&m, 8320).status == INSPECT_ESTIMATE_CHARGING);
  o.timestamp += 60;
  o.charging = false; /* Plugged at full/paused is still unsuitable. */
  inspect_model_observe(&m, &o);
  assert(estimate(&m, o.timestamp).status == INSPECT_ESTIMATE_CHARGING);
  o.timestamp += 60;
  o.plugged = false;
  inspect_model_observe(&m, &o);
  assert(estimate(&m, o.timestamp).status == INSPECT_ESTIMATE_INSUFFICIENT);

  build_discharge(&m, 1000);
  observe(&m, 8501, 76, true);
  assert(estimate(&m, 8501).status == INSPECT_ESTIMATE_INSUFFICIENT);
  build_discharge(&m, 1000);
  inspect_model_stop(&m, 8200);
  assert(estimate(&m, 8200).status == INSPECT_ESTIMATE_STALE);
  observe(&m, 8200, 77, true);
  assert(estimate(&m, 8200).status == INSPECT_ESTIMATE_INSUFFICIENT);

  inspect_model_init(&m);
  for (uint32_t minute = 0; minute <= 240; ++minute) {
    observe(&m, minute * 60, 50, true);
  }
  assert(estimate(&m, 14400).status == INSPECT_ESTIMATE_INSUFFICIENT);
  for (uint32_t minute = 241; minute <= 260; ++minute) {
    observe(&m, minute * 60, (uint8_t)(50 - (minute - 240) / 10), true);
  }
  assert(estimate(&m, 15600).status == INSPECT_ESTIMATE_INSUFFICIENT);

  inspect_model_init(&m);
  for (uint32_t minute = 0; minute <= 119; ++minute) {
    observe(&m, minute * 60, (uint8_t)(80 - minute / 30), true);
  }
  assert(estimate(&m, 7140).status == INSPECT_ESTIMATE_INSUFFICIENT);

  /* A bounded forecast refuses an implausibly slow 97-day extrapolation. */
  inspect_model_init(&m);
  observe(&m, 1000, 100, true);
  m.sample_count = 3;
  m.samples[1] = m.samples[0];
  m.samples[1].timestamp = 1000 + 86400;
  m.samples[1].battery_percent = 99;
  m.samples[2] = m.samples[1];
  m.samples[2].timestamp = 1000 + 3 * 86400;
  m.samples[2].battery_percent = 97;
  m.current.timestamp = m.last_observed = m.samples[2].timestamp;
  m.current.battery_percent = 97;
  assert(estimate(&m, m.last_observed).status == INSPECT_ESTIMATE_UNSUPPORTED);
}

static void test_bounds_and_saturation(void) {
  InspectModel m;
  inspect_model_init(&m);
  observe(&m, 0, 80, true);
  m.connected_seconds = UINT32_MAX - 10;
  m.drop_count = UINT32_MAX;
  observe(&m, 60, 80, false);
  assert(m.connected_seconds == UINT32_MAX && m.drop_count == UINT32_MAX);
  m.disconnected_seconds = UINT32_MAX - 10;
  observe(&m, 120, 80, true);
  observe(&m, 180, 80, false);
  assert(m.disconnected_seconds == UINT32_MAX && m.drop_count == UINT32_MAX);
  m.session_count = UINT32_MAX;
  m.gap_count = UINT32_MAX;
  m.time_reset_count = UINT32_MAX;
  m.segment = UINT32_MAX;
  inspect_model_stop(&m, 180);
  observe(&m, 120, 80, true);
  assert(m.session_count == UINT32_MAX && m.gap_count == UINT32_MAX);
  assert(m.time_reset_count == UINT32_MAX && m.segment == 1 && m.sample_count == 1);
  for (uint32_t i = 1; i <= 3000; ++i) {
    observe(&m, 120 + i * 60, (uint8_t)(80 - (i % 50)), (i % 2) != 0);
  }
  assert(m.sample_count == INSPECT_SAMPLE_CAPACITY);
  assert(m.event_count == INSPECT_EVENT_CAPACITY);
  assert(m.samples[m.sample_count - 1].timestamp == m.last_observed);
  assert(m.events[m.event_count - 1].timestamp == m.last_observed);
  InspectModel before = m;
  observe(&m, m.last_observed + 60, 101, true);
  assert(memcmp(&before, &m, sizeof(m)) == 0);
}

static uint32_t checksum(const uint8_t *data, size_t size) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned j = 0; j < 8; ++j) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

static void fix_checksum(uint8_t *data, size_t size) {
  uint32_t crc = checksum(data, size - 4);
  for (unsigned i = 0; i < 4; ++i) {
    data[size - 4 + i] = (uint8_t)(crc >> (8 * i));
  }
}

static void rejected_unchanged(const uint8_t *data, size_t size) {
  InspectModel m;
  build_discharge(&m, 1000);
  InspectModel before = m;
  assert(!inspect_codec_decode(&m, data, size));
  assert(memcmp(&m, &before, sizeof(m)) == 0);
}

static void test_codec(void) {
  uint8_t data[INSPECT_CODEC_MAX_SIZE];
  uint8_t second[INSPECT_CODEC_MAX_SIZE];
  uint8_t corrupt[INSPECT_CODEC_MAX_SIZE];
  InspectModel m, restored;
  inspect_model_init(&m);
  size_t size = inspect_codec_encode(&m, data, sizeof(data));
  assert(size == 56 && memcmp(data, "PINS\x01", 5) == 0);
  assert(inspect_codec_decode(&restored, data, size));
  assert(!restored.has_observation && !restored.active);
  build_discharge(&m, 0x12345678);
  size = inspect_codec_encode(&m, data, sizeof(data));
  assert(size > 56 && size <= 680);
  assert(data[14] == 0x78 && data[15] == 0x56 && data[16] == 0x34 && data[17] == 0x12);
  assert(inspect_codec_decode(&restored, data, size));
  assert(!restored.active && restored.connected_seconds == m.connected_seconds);
  assert(restored.events[restored.event_count - 1].kind == INSPECT_EVENT_UNKNOWN_END);
  assert(restored.events[restored.event_count - 1].timestamp == m.last_observed);
  assert(restored.sample_count == m.sample_count && restored.segment == m.segment);
  observe(&restored, m.last_observed + 60, 77, false);
  assert(restored.drop_count == m.drop_count && restored.gap_count == m.gap_count + 1);
  assert(restored.connected_seconds == m.connected_seconds);
  inspect_model_stop(&m, m.last_observed);
  size = inspect_codec_encode(&m, data, sizeof(data));
  assert(inspect_codec_decode(&restored, data, size));
  assert(inspect_codec_encode(&restored, second, sizeof(second)) == size);
  assert(memcmp(data, second, size) == 0);
  memset(second, 0xa5, sizeof(second));
  assert(inspect_codec_encode(&m, second, size - 1) == 0);
  assert(second[0] == 0xa5);
  for (size_t i = 0; i < size; ++i) {
    memcpy(corrupt, data, size);
    corrupt[i] ^= 0x40;
    rejected_unchanged(corrupt, size);
    rejected_unchanged(data, i);
  }
  const size_t offsets[] = {4, 5, 12, 13, 50, 51, 60, 61};
  const uint8_t values[] = {2, 0x80, 101, 0x80, 49, 25, 101, 0x80};
  for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
    memcpy(corrupt, data, size);
    corrupt[offsets[i]] = values[i];
    fix_checksum(corrupt, size);
    rejected_unchanged(corrupt, size);
  }
  memcpy(corrupt, data, size);
  corrupt[52 + m.sample_count * 10 + 4] = 255; /* Unknown event kind. */
  fix_checksum(corrupt, size);
  rejected_unchanged(corrupt, size);
  memcpy(corrupt, data, size);
  corrupt[18] ^= 1; /* last_observed must match the actual snapshot timestamp. */
  fix_checksum(corrupt, size);
  rejected_unchanged(corrupt, size);
  assert(!inspect_codec_decode(&restored, NULL, size));
  assert(!inspect_codec_decode(&restored, data, sizeof(data) + 1));

  for (uint32_t i = 0; i < 4000; ++i) {
    uint32_t t = m.last_observed + ((i % 79 == 0) ? 301 : 60);
    observe(&m, t, (uint8_t)(100 - (i % 101)), (i % 3) == 0);
    if (i % 31 == 0) {
      inspect_model_stop(&m, t);
    }
    size = inspect_codec_encode(&m, data, sizeof(data));
    assert(size > 0 && size <= 680);
    assert(inspect_codec_decode(&restored, data, size));
    assert(restored.sample_count == m.sample_count);
    assert(restored.drop_count == m.drop_count);
  }
}

int main(void) {
  test_connection_scope();
  test_estimates();
  test_bounds_and_saturation();
  test_codec();
  puts("core: connection scope, forecasts, bounds, saturation, codec and corruption passed");
  return 0;
}
