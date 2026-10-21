#ifdef PBL_SDK_3
#include <pebble.h>
#else
#include <stdio.h>
#include <time.h>
#endif
#include "ui_format.h"

bool inspect_heap_summary(uint32_t used, uint32_t free_bytes,
                          uint32_t *total, uint16_t *used_tenths_percent) {
  uint64_t sum = (uint64_t)used + free_bytes;
  if (!total || !used_tenths_percent || sum == 0 || sum > UINT32_MAX) return false;
  *total = (uint32_t)sum;
  *used_tenths_percent = (uint16_t)(((uint64_t)used * 1000u + sum / 2u) / sum);
  return true;
}

void inspect_format_kib(char *buffer, size_t size, uint32_t bytes) {
  uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10u + 512u) / 1024u);
  snprintf(buffer, size, "%lu.%lu KiB", (unsigned long)(tenths / 10u),
           (unsigned long)(tenths % 10u));
}

void inspect_format_duration(char *buffer, size_t size, uint32_t seconds) {
  snprintf(buffer, size, "%luh %02lum", (unsigned long)(seconds / 3600),
           (unsigned long)(seconds / 60 % 60));
}

void inspect_format_timestamp(char *buffer, size_t size, uint32_t timestamp) {
  time_t value = (time_t)timestamp;
  struct tm *local = localtime(&value);
  if (!local || strftime(buffer, size, "%m/%d %H:%M", local) == 0) {
    snprintf(buffer, size, "Time unavailable");
  }
}

void inspect_format_age(char *buffer, size_t size, uint32_t now, uint32_t timestamp) {
  if (now < timestamp) {
    snprintf(buffer, size, "Clock changed");
  } else if (now - timestamp > 300u) {
    snprintf(buffer, size, "Stale: %lum ago", (unsigned long)((now - timestamp) / 60));
  } else if (now - timestamp >= 60u) {
    snprintf(buffer, size, "Updated %lum ago", (unsigned long)((now - timestamp) / 60));
  } else {
    snprintf(buffer, size, "Updated %lus ago", (unsigned long)(now - timestamp));
  }
}
