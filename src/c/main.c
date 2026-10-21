#include <pebble.h>
#include "model.h"
#include "provider.h"
#include "storage.h"
#include "ui.h"

static InspectModel s_model;
static InspectSnapshot s_snapshot;
static uint32_t s_last_save;
static bool s_dirty;
static bool s_ui_ready;

static void flush_history(void) {
  if (!s_dirty) return;
  if (inspect_storage_save(&s_model)) {
    s_dirty = false;
    s_last_save = s_snapshot.observation.timestamp;
  }
}

static void observe(void) {
  if (s_model.active) {
    inspect_model_observe(&s_model, &s_snapshot.observation);
    s_dirty = true;
  }
  if (s_ui_ready) inspect_ui_refresh();
}

static void navigation(void) {
  inspect_provider_read(&s_snapshot);
  observe();
  APP_LOG(APP_LOG_LEVEL_INFO, "Navigation heap used=%lu free=%lu peak=%lu",
          (unsigned long)s_snapshot.heap_used, (unsigned long)s_snapshot.heap_free,
          (unsigned long)s_snapshot.heap_peak);
}

static void tick(struct tm *tick_time, TimeUnits units_changed) {
  inspect_provider_read(&s_snapshot);
  observe();
  uint32_t now = s_snapshot.observation.timestamp;
  if (now < s_last_save || now - s_last_save >= 900u) flush_history();
}

static void battery_changed(BatteryChargeState state) {
  inspect_provider_read(&s_snapshot);
  s_snapshot.observation.battery_percent = state.charge_percent;
  s_snapshot.observation.charging = state.is_charging;
  s_snapshot.observation.plugged = state.is_plugged;
  observe();
}

static void phone_changed(bool connected) {
  inspect_provider_read(&s_snapshot);
  s_snapshot.observation.connected = connected;
  observe();
}

static void pebblekit_changed(bool connected) {
  inspect_provider_read(&s_snapshot);
  s_snapshot.pebblekit_connected = connected;
  observe();
}

static void will_focus(bool focused) {
  if (focused || !s_model.active) return;
  inspect_provider_read(&s_snapshot);
  inspect_model_observe(&s_model, &s_snapshot.observation);
  inspect_model_stop(&s_model, s_snapshot.observation.timestamp);
  s_dirty = true;
  flush_history();
}

static void did_focus(bool focused) {
  if (!focused || s_model.active) return;
  inspect_provider_read(&s_snapshot);
  inspect_model_start(&s_model, &s_snapshot.observation);
  s_dirty = true;
  flush_history();
  if (s_ui_ready) inspect_ui_refresh();
}

int main(void) {
  inspect_model_init(&s_model);
  inspect_storage_load(&s_model);
  inspect_provider_read(&s_snapshot);
  inspect_model_start(&s_model, &s_snapshot.observation);
  s_dirty = true;
  s_last_save = s_snapshot.observation.timestamp;
  s_ui_ready = inspect_ui_init(&s_model, &s_snapshot, navigation);
  if (s_ui_ready) {
    navigation();
    /* Persist the open session once so an abrupt termination can be recorded
     * as unknown on the next launch; ordinary samples remain batched. */
    flush_history();
    inspect_ui_refresh();
    APP_LOG(APP_LOG_LEVEL_INFO, "Inspect entry heap=%lu free=%lu",
            (unsigned long)s_snapshot.heap_used, (unsigned long)s_snapshot.heap_free);
    tick_timer_service_subscribe(MINUTE_UNIT, tick);
    battery_state_service_subscribe(battery_changed);
    connection_service_subscribe((ConnectionHandlers){
      .pebble_app_connection_handler = phone_changed,
      .pebblekit_connection_handler = pebblekit_changed
    });
    app_focus_service_subscribe_handlers((AppFocusHandlers){
      .will_focus = will_focus, .did_focus = did_focus
    });
    app_event_loop();
    app_focus_service_unsubscribe();
    connection_service_unsubscribe();
    battery_state_service_unsubscribe();
    tick_timer_service_unsubscribe();
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "UI allocation failed");
  }
  inspect_provider_read(&s_snapshot);
  if (s_model.active) {
    inspect_model_observe(&s_model, &s_snapshot.observation);
    inspect_model_stop(&s_model, s_snapshot.observation.timestamp);
    s_dirty = true;
  }
  flush_history();
  s_ui_ready = false;
  inspect_ui_deinit();
}
