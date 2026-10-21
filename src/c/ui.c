#include "ui.h"
#include "ui_format.h"
#include "capabilities.h"
#include "storage.h"

enum { PAGE_OVERVIEW, PAGE_BATTERY, PAGE_CONNECTION, PAGE_MEMORY,
       PAGE_DEVICE, PAGE_CAPABILITIES, PAGE_COUNT };
static const char *s_titles[PAGE_COUNT] = {
  "Overview", "Battery", "Connection", "Inspect Memory", "Device", "Capabilities"
};
static Window *s_window;
static Layer *s_header;
static TextLayer *s_hero, *s_body, *s_footer;
static ScrollLayer *s_scroll;
static SimpleMenuLayer *s_menu;
static SimpleMenuItem s_items[PAGE_COUNT];
static SimpleMenuSection s_section;
static const InspectModel *s_model;
static const InspectSnapshot *s_snapshot;
static InspectNavigationHandler s_navigation_handler;
static int s_page;
static bool s_menu_visible;
static char s_text[6144], s_headline[64];

/* SDK supports snprintf but deliberately rejects vsnprintf. */
#define append(...) do { \
  size_t text_used = strlen(s_text); \
  if (text_used < sizeof(s_text) - 1) \
    snprintf(s_text + text_used, sizeof(s_text) - text_used, __VA_ARGS__); \
} while (0)

static const char *event_name(InspectEventKind kind) {
  switch (kind) {
    case INSPECT_EVENT_SESSION_START: return "Session started";
    case INSPECT_EVENT_SESSION_STOP: return "Session stopped";
    case INSPECT_EVENT_CONNECTED: return "Phone connected";
    case INSPECT_EVENT_DISCONNECTED: return "Phone disconnected";
    case INSPECT_EVENT_GAP: return "Unobserved gap";
    case INSPECT_EVENT_TIME_RESET: return "Clock changed";
    case INSPECT_EVENT_CHARGING: return "Charging / plugged";
    case INSPECT_EVENT_UNPLUGGED: return "Charger removed";
    default: return "Last seen; end unknown";
  }
}

static void build_text(void) {
  const InspectObservation *current = &s_snapshot->observation;
  char age[40];
  uint32_t now = (uint32_t)time(NULL);
  inspect_format_age(age, sizeof(age), now, current->timestamp);
  s_text[0] = '\0';
  s_headline[0] = '\0';
  switch (s_page) {
    case PAGE_OVERVIEW:
      snprintf(s_headline, sizeof(s_headline), "%u%% battery", current->battery_percent);
      append("Charge: %s  |  Plug: %s\nPhone: %s\nInspect heap: %lu B\n%s\nForeground only\n\n",
             current->charging ? "yes" : "no", current->plugged ? "yes" : "no",
             current->connected ? "connected" : "disconnected",
             (unsigned long)s_snapshot->heap_used, age);
      append("PebbleKit: %s\nFree Inspect heap: %lu B\n\n",
             s_snapshot->pebblekit_connected ? "connected" : "disconnected",
             (unsigned long)s_snapshot->heap_free);
      append("LIVE IN INSPECT\nMinute samples + service events. History covers foreground sessions only.\n\n%s\n%u battery samples / %lu sessions\n%lu unobserved gaps",
             inspect_storage_status(), s_model->sample_count,
             (unsigned long)s_model->session_count, (unsigned long)s_model->gap_count);
      break;
    case PAGE_BATTERY: {
      InspectEstimate estimate;
      inspect_model_estimate(s_model, now, &estimate);
      snprintf(s_headline, sizeof(s_headline), "%u%% charge", current->battery_percent);
      append("Charge: %s | Plug: %s\n%s\nREMAINING TIME\n", current->charging ? "yes" : "no",
             current->plugged ? "yes" : "no", age);
      if (estimate.status == INSPECT_ESTIMATE_READY) {
        char remaining[24], span[24];
        inspect_format_duration(remaining, sizeof(remaining), estimate.remaining_seconds);
        inspect_format_duration(span, sizeof(span), estimate.span_seconds);
        append("About %s\n%u%% drop across %s\n%u samples in one observed segment. Assumes similar future use. Rough linear estimate; actual life varies.\n\n",
               remaining, estimate.drop_percent, span, estimate.sample_count);
      } else if (estimate.status == INSPECT_ESTIMATE_CHARGING) {
        append("Unavailable\nPlugged or charging.\n\n");
      } else if (estimate.status == INSPECT_ESTIMATE_STALE) {
        append("Stale data\nKeep Inspect open to collect fresh readings.\n\n");
      } else if (estimate.status == INSPECT_ESTIMATE_UNSUPPORTED) {
        append("Unavailable\nObservations cannot support a reliable estimate.\n\n");
      } else {
        append("Insufficient data\nNeeds 3+ samples, 2+ hours and a 3%%+ drop in one unplugged foreground segment.\n\n");
      }
      append("OBSERVED HISTORY (%u / %u)\nOldest entries are replaced. Minute checks; stored at 15-minute intervals or a state change.\n\n",
             s_model->sample_count, INSPECT_SAMPLE_CAPACITY);
      for (int i = (int)s_model->sample_count - 1; i >= 0; --i) {
        const InspectSample *sample = &s_model->samples[i];
        if (i == (int)s_model->sample_count - 1 || sample->segment != s_model->samples[i + 1].segment) {
          append("\nSegment %lu%s\n", (unsigned long)sample->segment,
                 i == (int)s_model->sample_count - 1 ? " (newest)" : " / gap or state break");
        }
        char when[24]; inspect_format_timestamp(when, sizeof(when), sample->timestamp);
        append("%s  %u%%%s\n", when, sample->battery_percent,
               sample->charging ? " charging" : sample->plugged ? " plugged" : "");
      }
      append("\nNo readings while closed or covered. Battery percentage is reported in coarse steps. No health or capacity reading.");
      break;
    }
    case PAGE_CONNECTION: {
      char online[24], offline[24];
      inspect_format_duration(online, sizeof(online), s_model->connected_seconds);
      inspect_format_duration(offline, sizeof(offline), s_model->disconnected_seconds);
      snprintf(s_headline, sizeof(s_headline), "Phone %s", current->connected ? "online" : "offline");
      append("Phone app: %s\nPebbleKit: %s\n%s\n\nPHONE OBSERVATIONS\nConnected: %s\nDisconnected: %s\nObserved drops: %lu\nSessions: %lu  /  Gaps: %lu\n\n",
             current->connected ? "Connected" : "Disconnected",
             s_snapshot->pebblekit_connected ? "Connected" : "Disconnected", age, online, offline,
             (unsigned long)s_model->drop_count, (unsigned long)s_model->session_count,
             (unsigned long)s_model->gap_count);
      append("Durations and drops follow the SDK's reported state during foreground intervals. Firmware filters brief outages; this is not a raw radio log. No inference across gaps.\n\nRECENT EVENTS (%u / %u)\n", s_model->event_count, INSPECT_EVENT_CAPACITY);
      for (int i = (int)s_model->event_count - 1; i >= 0; --i) {
        char when[24]; inspect_format_timestamp(when, sizeof(when), s_model->events[i].timestamp);
        append("%s\n%s\n", when, event_name(s_model->events[i].kind));
      }
      append("\nPebbleKit is a distinct live API state; companion routing differs between iOS and Android. This app sends no phone messages.");
      break;
    }
    case PAGE_MEMORY: {
      uint32_t total;
      uint16_t percent;
      char used_text[24], free_text[24], total_text[24];
      inspect_format_kib(used_text, sizeof(used_text), s_snapshot->heap_used);
      inspect_format_kib(free_text, sizeof(free_text), s_snapshot->heap_free);
      if (inspect_heap_summary(s_snapshot->heap_used, s_snapshot->heap_free, &total, &percent)) {
        inspect_format_kib(total_text, sizeof(total_text), total);
        snprintf(s_headline, sizeof(s_headline), "%u.%u%% used", percent / 10u, percent % 10u);
      } else {
        snprintf(s_headline, sizeof(s_headline), "Use unavailable");
        snprintf(total_text, sizeof(total_text), "Unavailable");
      }
      append("Used: %s\nFree: %s\nTotal: %s\n%s\n\nINSPECT HEAP ONLY\n1 KiB = 1024 bytes. Total = used + free. Use%% = used / total.\n\nEXACT BYTES\nUsed: %lu\nFree: %lu\nSampled peak: %lu\n\n",
             used_text, free_text, total_text, age, (unsigned long)s_snapshot->heap_used,
             (unsigned long)s_snapshot->heap_free, (unsigned long)s_snapshot->heap_peak);
      append("Public heap_bytes_used / free values. Peak is the highest sampled use since this launch, not a continuous high-water mark.\n\n");
      append("Sampled on entry, navigation, minute ticks and service events.\n\nSCOPE\nIncludes Inspect heap allocations only. Does not measure system RAM, stack, firmware, or another app. Opening Inspect replaces the running watchface.\n\nRepeated navigation reuses the same layers and buffers.");
      break;
    }
    case PAGE_DEVICE:
      snprintf(s_headline, sizeof(s_headline), "Public identity");
      append("MODEL\n%s\nFirmware: %u.%u.%u\nPublic model ID: %d\n\n", inspect_provider_model_name(s_snapshot->model),
             s_snapshot->firmware.major, s_snapshot->firmware.minor, s_snapshot->firmware.patch, (int)s_snapshot->model);
      append("SOURCE\nwatch_info_get_model()\nwatch_info_get_firmware_version()\n\nThese are values reported by the public SDK. Unknown models remain unknown. Firmware build hash, serial number and system uptime are unavailable here.");
      break;
    case PAGE_CAPABILITIES:
      snprintf(s_headline, sizeof(s_headline), "Public SDK");
      append("%s", inspect_capabilities_text());
      break;
  }
}

static void header_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorCyan);
  graphics_fill_rect(ctx, GRect(10, 10, 5, 5), 1, GCornersAll);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "INSPECT", fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(21, 3, 100, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  char index[16];
  snprintf(index, sizeof(index), s_menu_visible ? "SCREENS" : "%02d / 06", s_page + 1);
  graphics_draw_text(ctx, index, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(bounds.size.w - 75, 3, 65, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, s_menu_visible ? "Choose a screen" : s_titles[s_page],
                     fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(10, 20, bounds.size.w - 20, 32),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void inspect_ui_refresh(void) {
  if (!s_window || !s_body) return;
  build_text();
  text_layer_set_text(s_hero, s_headline);
  text_layer_set_text(s_body, s_text);
  int16_t width = layer_get_bounds(window_get_root_layer(s_window)).size.w;
  /* Measure the whole string in a local box. TextLayer's convenience measure
   * uses its global frame and persistent cache, which can depend on the
   * current scroll offset. The ScrollLayer needs the full document height. */
  GSize content = graphics_text_layout_get_content_size(
    s_text, fonts_get_system_font(FONT_KEY_GOTHIC_24), GRect(0, 0, width - 25, 16000),
    GTextOverflowModeWordWrap, GTextAlignmentLeft);
  layer_set_frame(text_layer_get_layer(s_body), GRect(10, 0, width - 25, content.h + 8));
  scroll_layer_set_content_size(s_scroll, GSize(width, content.h + 10));
  layer_mark_dirty(s_header);
}

static void menu_show(void);
static void select_click(ClickRecognizerRef recognizer, void *context) { menu_show(); }
static void detail_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, select_click);
}

static void menu_selected(int index, void *context) {
  s_page = index;
  s_menu_visible = false;
  layer_set_hidden(simple_menu_layer_get_layer(s_menu), true);
  layer_set_hidden(text_layer_get_layer(s_hero), false);
  layer_set_hidden(scroll_layer_get_layer(s_scroll), false);
  text_layer_set_text(s_footer, "SELECT menu | Scroll");
  scroll_layer_set_click_config_onto_window(s_scroll, s_window);
  scroll_layer_set_content_offset(s_scroll, GPointZero, false);
  if (s_navigation_handler) s_navigation_handler();
  inspect_ui_refresh();
}

static void menu_up(ClickRecognizerRef recognizer, void *context) {
  menu_layer_set_selected_next(simple_menu_layer_get_menu_layer(s_menu), true,
                               MenuRowAlignCenter, true);
}

static void menu_down(ClickRecognizerRef recognizer, void *context) {
  menu_layer_set_selected_next(simple_menu_layer_get_menu_layer(s_menu), false,
                               MenuRowAlignCenter, true);
}

static void menu_open(ClickRecognizerRef recognizer, void *context) {
  menu_selected(simple_menu_layer_get_selected_index(s_menu), NULL);
}

static void menu_exit(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void menu_click_config(void *context) {
  /* Configure all four buttons explicitly. Native menu configuration leaves
   * the detail screen's BACK subscription intact on this shared window. */
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, menu_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, menu_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, menu_open);
  window_single_click_subscribe(BUTTON_ID_BACK, menu_exit);
}

static void menu_show(void) {
  s_menu_visible = true;
  layer_set_hidden(simple_menu_layer_get_layer(s_menu), false);
  layer_set_hidden(text_layer_get_layer(s_hero), true);
  layer_set_hidden(scroll_layer_get_layer(s_scroll), true);
  text_layer_set_text(s_footer, "UP/DOWN | SELECT");
  window_set_click_config_provider_with_context(s_window, menu_click_config, NULL);
  if (s_navigation_handler) s_navigation_handler();
  layer_mark_dirty(s_header);
}

bool inspect_ui_init(const InspectModel *model, const InspectSnapshot *snapshot,
                     InspectNavigationHandler navigation_handler) {
  s_model = model;
  s_snapshot = snapshot;
  s_navigation_handler = navigation_handler;
  s_window = window_create();
  if (!s_window) return false;
  window_set_background_color(s_window, GColorOxfordBlue);
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  s_header = layer_create(GRect(0, 0, bounds.size.w, 54));
  s_hero = text_layer_create(GRect(10, 50, bounds.size.w - 20, 38));
  s_scroll = scroll_layer_create(GRect(0, 92, bounds.size.w, bounds.size.h - 116));
  s_body = text_layer_create(GRect(10, 0, bounds.size.w - 25, 16000));
  s_footer = text_layer_create(GRect(5, bounds.size.h - 23, bounds.size.w - 10, 23));
  for (int i = 0; i < PAGE_COUNT; ++i) {
    s_items[i] = (SimpleMenuItem){.title = s_titles[i], .callback = menu_selected};
  }
  s_section = (SimpleMenuSection){.items = s_items, .num_items = PAGE_COUNT};
  s_menu = simple_menu_layer_create(GRect(0, 55, bounds.size.w, bounds.size.h - 76), s_window, &s_section, 1, NULL);
  if (!s_header || !s_hero || !s_scroll || !s_body || !s_footer || !s_menu) {
    inspect_ui_deinit();
    return false;
  }
  layer_set_update_proc(s_header, header_draw);
  TextLayer *texts[] = {s_hero, s_body, s_footer};
  for (size_t i = 0; i < ARRAY_LENGTH(texts); ++i) {
    text_layer_set_background_color(texts[i], GColorClear);
    text_layer_set_text_color(texts[i], GColorWhite);
  }
  text_layer_set_font(s_hero, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_color(s_hero, GColorCyan);
  text_layer_set_font(s_body, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_overflow_mode(s_body, GTextOverflowModeWordWrap);
  text_layer_set_font(s_footer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_footer, GTextAlignmentCenter);
  text_layer_set_text_color(s_footer, GColorLightGray);
  text_layer_set_text(s_footer, "SELECT menu | Scroll");
  Layer *root = window_get_root_layer(s_window);
  layer_add_child(root, s_header);
  layer_add_child(root, text_layer_get_layer(s_hero));
  layer_add_child(root, scroll_layer_get_layer(s_scroll));
  scroll_layer_add_child(s_scroll, text_layer_get_layer(s_body));
  layer_add_child(root, text_layer_get_layer(s_footer));
  layer_add_child(root, simple_menu_layer_get_layer(s_menu));
  layer_set_hidden(simple_menu_layer_get_layer(s_menu), true);
  menu_layer_set_highlight_colors(simple_menu_layer_get_menu_layer(s_menu), GColorCyan, GColorBlack);
  scroll_layer_set_callbacks(s_scroll, (ScrollLayerCallbacks){.click_config_provider = detail_click_config});
  scroll_layer_set_click_config_onto_window(s_scroll, s_window);
  inspect_ui_refresh();
  window_stack_push(s_window, true);
  return true;
}

void inspect_ui_deinit(void) {
  if (s_menu) simple_menu_layer_destroy(s_menu);
  if (s_body) text_layer_destroy(s_body);
  if (s_scroll) scroll_layer_destroy(s_scroll);
  if (s_hero) text_layer_destroy(s_hero);
  if (s_footer) text_layer_destroy(s_footer);
  if (s_header) layer_destroy(s_header);
  if (s_window) window_destroy(s_window);
  s_menu = NULL; s_body = NULL; s_scroll = NULL; s_hero = NULL;
  s_footer = NULL; s_header = NULL; s_window = NULL;
}
