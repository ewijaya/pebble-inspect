#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool inspect_heap_summary(uint32_t used, uint32_t free_bytes,
                          uint32_t *total, uint16_t *used_tenths_percent);
void inspect_format_kib(char *buffer, size_t size, uint32_t bytes);

void inspect_format_duration(char *buffer, size_t size, uint32_t seconds);
void inspect_format_timestamp(char *buffer, size_t size, uint32_t timestamp);
void inspect_format_age(char *buffer, size_t size, uint32_t now, uint32_t timestamp);
