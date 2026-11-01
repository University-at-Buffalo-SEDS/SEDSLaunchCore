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

/* Opt-in inspection only: callers initialize the storage driver beforehand
 * and serialize against writers. Probe BEFORE ANY legacy persist_init/get/set
 * or application persistent_store_init, since those may format storage.
 * These functions call layout/read only;
 * they never initialize, format, compact, erase or write storage. No result
 * authenticates a device or detects a self-consistent whole-storage rollback.
 * VIRGIN/ABSENT/DELETED do not establish permission to enable legacy control.
 * Conservative CORRUPT includes a nonblank interrupted GC target. */
typedef enum {
    LAUNCHCORE_PERSIST_HEALTH_UNKNOWN = 0,
    LAUNCHCORE_PERSIST_HEALTH_VIRGIN,
    LAUNCHCORE_PERSIST_HEALTH_HEALTHY,
    LAUNCHCORE_PERSIST_HEALTH_CORRUPT,
    LAUNCHCORE_PERSIST_HEALTH_AMBIGUOUS,
} launchcore_persist_health_t;
typedef enum {
    LAUNCHCORE_PERSIST_RECORD_NOT_QUERIED = 0,
    LAUNCHCORE_PERSIST_RECORD_ABSENT,
    LAUNCHCORE_PERSIST_RECORD_PRESENT,
    LAUNCHCORE_PERSIST_RECORD_DELETED,
} launchcore_persist_record_state_t;
typedef struct {
    launchcore_persist_health_t health;
    launchcore_persist_record_state_t record;
    uint32_t bank_generation;
    uint32_t max_sequence;
} launchcore_persist_strict_info_t;
/* info/value/value_size storage must not overlap. info is required and may
 * change on any return. On every error, get leaves
 * value AND *value_size unchanged (including NO_SPACE); info reports health.
 * Healthy absent/deleted and virgin get return NOT_FOUND. No automatic repair.
 * Probe/get report CORRUPT for ambiguous generations/sequence exhaustion too,
 * distinguished by info.health. An IO failure reports STORAGE_ERROR/UNKNOWN. */
launchcore_persist_status_t launchcore_persist_probe_strict(
    launchcore_persist_strict_info_t *info);
launchcore_persist_status_t launchcore_persist_get_strict(uint32_t key,
    void *value, size_t *value_size, launchcore_persist_strict_info_t *info);

/* HEALTHY means structural journal integrity, not authorized provisioning.
 * get additionally checks the requested key against an adjacent predecessor.
 * Sequence/value semantics remain application-owned, not authentication. */
