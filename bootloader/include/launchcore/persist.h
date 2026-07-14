#pragma once

#include <stddef.h>
#include <stdint.h>

#define LAUNCHCORE_PERSIST_MAX_VALUE_SIZE 256u

typedef enum {
    LAUNCHCORE_PERSIST_OK = 0,
    LAUNCHCORE_PERSIST_NOT_FOUND,
    LAUNCHCORE_PERSIST_INVALID_ARGUMENT,
    LAUNCHCORE_PERSIST_NO_SPACE,
    LAUNCHCORE_PERSIST_STORAGE_ERROR,
    LAUNCHCORE_PERSIST_CORRUPT,
} launchcore_persist_status_t;

launchcore_persist_status_t launchcore_persist_init(void);
launchcore_persist_status_t launchcore_persist_get(uint32_t key, void *value, size_t *value_size);
launchcore_persist_status_t launchcore_persist_set(uint32_t key, const void *value, size_t value_size);
launchcore_persist_status_t launchcore_persist_delete(uint32_t key);
