#ifndef TEST_PEBBLE_H
#define TEST_PEBBLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define APP_LOG_LEVEL_WARNING 1
#define APP_LOG_LEVEL_ERROR 2
#define APP_LOG(...) ((void)0)

int persist_get_size(uint32_t key);
int persist_read_data(uint32_t key, void *buffer, size_t size);
int persist_write_data(uint32_t key, const void *data, size_t size);
bool persist_exists(uint32_t key);
#endif
