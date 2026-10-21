#include "provider.h"

static uint32_t s_heap_peak;

void inspect_provider_read(InspectSnapshot *snapshot) {
  BatteryChargeState battery = battery_state_service_peek();
  snapshot->observation = (InspectObservation) {
    .timestamp = (uint32_t)time(NULL),
    .battery_percent = battery.charge_percent,
    .charging = battery.is_charging,
    .plugged = battery.is_plugged,
    .connected = connection_service_peek_pebble_app_connection()
  };
  snapshot->pebblekit_connected = connection_service_peek_pebblekit_connection();
  snapshot->heap_used = (uint32_t)heap_bytes_used();
  snapshot->heap_free = (uint32_t)heap_bytes_free();
  if (snapshot->heap_used > s_heap_peak) s_heap_peak = snapshot->heap_used;
  snapshot->heap_peak = s_heap_peak;
  snapshot->model = watch_info_get_model();
  snapshot->firmware = watch_info_get_firmware_version();
}

const char *inspect_provider_model_name(WatchInfoModel model) {
  switch (model) {
    case WATCH_INFO_MODEL_PEBBLE_ORIGINAL: return "Pebble";
    case WATCH_INFO_MODEL_PEBBLE_STEEL: return "Pebble Steel";
    case WATCH_INFO_MODEL_PEBBLE_TIME: return "Pebble Time";
    case WATCH_INFO_MODEL_PEBBLE_TIME_STEEL: return "Pebble Time Steel";
    case WATCH_INFO_MODEL_PEBBLE_TIME_ROUND_14: return "Time Round 14mm";
    case WATCH_INFO_MODEL_PEBBLE_TIME_ROUND_20: return "Time Round 20mm";
    case WATCH_INFO_MODEL_PEBBLE_2_HR: return "Pebble 2 HR";
    case WATCH_INFO_MODEL_PEBBLE_2_SE: return "Pebble 2 SE";
    case WATCH_INFO_MODEL_PEBBLE_TIME_2: return "Pebble Time 2";
    case WATCH_INFO_MODEL_COREDEVICES_P2D: return "Pebble 2 Duo";
    case WATCH_INFO_MODEL_COREDEVICES_PT2: return "Pebble Time 2";
    case WATCH_INFO_MODEL_COREDEVICES_PR2: return "Pebble Round 2";
    default: return "Unknown (public API)";
  }
}
