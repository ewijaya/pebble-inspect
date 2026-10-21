#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_format.h"

int main(void) {
  char text[64];
  uint32_t total = 99;
  uint16_t percent = 99;
  assert(!inspect_heap_summary(0, 0, &total, &percent));
  assert(total == 99 && percent == 99);
  assert(inspect_heap_summary(0, 1024, &total, &percent) && total == 1024 && percent == 0);
  assert(inspect_heap_summary(1024, 0, &total, &percent) && total == 1024 && percent == 1000);
  assert(inspect_heap_summary(1, 2, &total, &percent) && total == 3 && percent == 333);
  assert(inspect_heap_summary(1944, 104880, &total, &percent) && total == 106824 && percent == 18);
  assert(inspect_heap_summary(UINT32_MAX, 0, &total, &percent) && total == UINT32_MAX && percent == 1000);
  assert(!inspect_heap_summary(UINT32_MAX, 1, &total, &percent));
  assert(!inspect_heap_summary(1, 1, NULL, &percent));
  inspect_format_kib(text, sizeof(text), 0); assert(strcmp(text, "0.0 KiB") == 0);
  inspect_format_kib(text, sizeof(text), 1024); assert(strcmp(text, "1.0 KiB") == 0);
  inspect_format_kib(text, sizeof(text), 1944); assert(strcmp(text, "1.9 KiB") == 0);
  inspect_format_kib(text, sizeof(text), 104880); assert(strcmp(text, "102.4 KiB") == 0);
  inspect_format_kib(text, sizeof(text), UINT32_MAX); assert(strcmp(text, "4194304.0 KiB") == 0);
  inspect_format_duration(text, sizeof(text), 0); assert(strcmp(text, "0h 00m") == 0);
  inspect_format_duration(text, sizeof(text), 3599); assert(strcmp(text, "0h 59m") == 0);
  inspect_format_duration(text, sizeof(text), 3600); assert(strcmp(text, "1h 00m") == 0);
  inspect_format_duration(text, sizeof(text), UINT32_MAX); assert(strcmp(text, "1193046h 28m") == 0);
  inspect_format_age(text, sizeof(text), 1000, 1000); assert(strcmp(text, "Updated 0s ago") == 0);
  inspect_format_age(text, sizeof(text), 1059, 1000); assert(strcmp(text, "Updated 59s ago") == 0);
  inspect_format_age(text, sizeof(text), 1060, 1000); assert(strcmp(text, "Updated 1m ago") == 0);
  inspect_format_age(text, sizeof(text), 1300, 1000); assert(strcmp(text, "Updated 5m ago") == 0);
  inspect_format_age(text, sizeof(text), 1301, 1000); assert(strcmp(text, "Stale: 5m ago") == 0);
  inspect_format_age(text, sizeof(text), 999, 1000); assert(strcmp(text, "Clock changed") == 0);
  inspect_format_age(text, sizeof(text), UINT32_MAX, 0); assert(strcmp(text, "Stale: 71582788m ago") == 0);
  setenv("TZ", "UTC", 1); tzset();
  inspect_format_timestamp(text, sizeof(text), 0); assert(strcmp(text, "01/01 00:00") == 0);
  inspect_format_timestamp(text, sizeof(text), 1735689600u); assert(strcmp(text, "01/01 00:00") == 0);
  struct { char before; char small[4]; char after; } bounded = {.before = 'A', .after = 'Z'};
  inspect_format_kib(bounded.small, sizeof(bounded.small), UINT32_MAX);
  assert(bounded.before == 'A' && bounded.after == 'Z' && bounded.small[3] == '\0');
  inspect_format_duration(bounded.small, sizeof(bounded.small), UINT32_MAX);
  assert(bounded.before == 'A' && bounded.after == 'Z' && bounded.small[3] == '\0');
  inspect_format_age(bounded.small, sizeof(bounded.small), UINT32_MAX, 0);
  assert(bounded.before == 'A' && bounded.after == 'Z' && bounded.small[3] == '\0');
  inspect_format_timestamp(bounded.small, sizeof(bounded.small), 0);
  assert(bounded.before == 'A' && bounded.after == 'Z' && bounded.small[3] == '\0');
  puts("UI formatting tests passed");
  return 0;
}
