#pragma once

#include <stdbool.h>
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

/* Opt-in verified writes. Caller must initialize STORAGE ONLY, then hold an
 * exclusive checked storage-owner lock through selection, write and readback.
 * Source value must remain immutable and must not alias info. No operation
 * initializes storage or invokes legacy formatting/repair. Existing healthy
 * journal only: virgin set/delete return NOT_FOUND. Corrupt, ambiguous or IO
 * health fails closed. Errors may have durably written part/all of an intent;
 * they never promise rollback. GC preserves the old active bank; an interrupted
 * target can intentionally make subsequent strict health CORRUPT. No automatic
 * recovery/reformat. Latest tombstones and unrelated keys are preserved.
 * Provisioning is a DISTINCT explicit operator-authorized virgin-only operation;
 * this API cannot establish that authorization. Missing/deleted keys do not
 * authorize provisioning or enabling legacy control. Arbitrary whole-flash
 * rollback remains undetectable. info may change on every return. */
launchcore_persist_status_t launchcore_persist_set_strict(uint32_t key,
    const void *value, size_t value_size, launchcore_persist_strict_info_t *info);
launchcore_persist_status_t launchcore_persist_delete_strict(uint32_t key,
    launchcore_persist_strict_info_t *info);
launchcore_persist_status_t launchcore_persist_provision_virgin_strict(uint32_t key,
    const void *value, size_t value_size, launchcore_persist_strict_info_t *info);

/* Opt-in boot repair of interrupted writes, for the exclusive storage owner
 * before any other storage user exists. It changes storage only when strict
 * health is CORRUPT/AMBIGUOUS because of a power loss inside a LaunchCore
 * write, and it never discards a record that a write reported as stored:
 * - a bank that holds no journal commit (interrupted provisioning,
 *   compaction or commit, or bytes left by an older flash layout) is erased;
 *   with no committed bank at all the store returns to VIRGIN;
 * - a bank with a commit word under a damaged header (an interrupted erase of
 *   the non-active bank) is erased only beside a clean committed bank;
 * - an older committed bank beside a clean newer bank is erased;
 * - a torn final record of the newest bank (every later byte erased within
 *   one maximal record) is dropped by compacting the intact records into the
 *   other bank, committing it, then erasing the torn bank.
 * A corrupt record followed by data, equal or gapped generations, exhausted
 * generations/sequences and IO errors are left unchanged. Power loss inside
 * the repair leaves a state this function repairs on the next call. Returns
 * the strict probe of the result; *repaired (optional) reports a change. */
launchcore_persist_status_t launchcore_persist_repair_interrupted_strict(
    launchcore_persist_strict_info_t *info, bool *repaired);
