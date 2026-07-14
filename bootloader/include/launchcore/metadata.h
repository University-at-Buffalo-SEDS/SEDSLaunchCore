#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LAUNCHCORE_METADATA_MAGIC 0x4C434D44u
#define LAUNCHCORE_METADATA_VERSION 2u
#define LAUNCHCORE_METADATA_FLAG_RECOVERY_REQUESTED (1u << 0)
#define LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING (1u << 1)
#define LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP (1u << 2)
#define LAUNCHCORE_METADATA_FLAG_APP_INSTALL_PENDING (1u << 3)
#define LAUNCHCORE_METADATA_FLAG_DELTA_UPDATE_PENDING (1u << 4)
#define LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_AVAILABLE (1u << 5)
#define LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_PENDING (1u << 6)
#define LAUNCHCORE_METADATA_FLAG_DELTA_APPLY_STARTED (1u << 7)
#define LAUNCHCORE_METADATA_FLAG_DELTA_ROLLBACK_STARTED (1u << 8)

typedef enum {
    LAUNCHCORE_SLOT_A = 0,
    LAUNCHCORE_SLOT_B = 1,
    LAUNCHCORE_SLOT_NONE = 0xFF,
} launchcore_slot_id_t;

typedef enum {
    LAUNCHCORE_SLOT_EMPTY = 0,
    LAUNCHCORE_SLOT_WRITING,
    LAUNCHCORE_SLOT_PENDING,
    LAUNCHCORE_SLOT_CONFIRMED,
    LAUNCHCORE_SLOT_VALID_ROLLBACK,
    LAUNCHCORE_SLOT_BAD,
} launchcore_slot_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t confirmed_slot;
    uint8_t pending_slot;
    uint8_t update_slot;
    uint8_t slot_a_state;
    uint8_t slot_b_state;
    uint8_t boot_attempts;
    uint8_t max_boot_attempts;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint8_t slot_a_hash[32];
    uint8_t slot_b_hash[32];
    uint32_t flags;
    uint8_t update_kind;
    uint8_t update_attempted;
    uint8_t update_result;
    uint8_t update_reason;
    uint32_t staged_app_size;
    uint32_t staged_app_version;
    uint8_t staged_app_hash[32];
    uint32_t staged_app_addr;
    uint32_t staged_bundle_base;
    uint32_t staged_bundle_size;
    uint32_t metadata_crc32;
} launchcore_metadata_t;

typedef enum {
    LAUNCHCORE_METADATA_OK = 0,
    LAUNCHCORE_METADATA_ERR_INVALID,
    LAUNCHCORE_METADATA_ERR_STORAGE,
    LAUNCHCORE_METADATA_ERR_VERIFY,
} launchcore_metadata_status_t;

void launchcore_metadata_init_default(launchcore_metadata_t *md);
uint32_t launchcore_metadata_crc(const launchcore_metadata_t *md);
bool launchcore_metadata_is_valid(const launchcore_metadata_t *md);
const launchcore_metadata_t *launchcore_metadata_select_newest(const launchcore_metadata_t *a, const launchcore_metadata_t *b);
launchcore_metadata_status_t launchcore_metadata_read_latest(launchcore_metadata_t *out);
launchcore_metadata_status_t launchcore_metadata_commit(const launchcore_metadata_t *next);
launchcore_slot_state_t launchcore_metadata_slot_state(const launchcore_metadata_t *md, launchcore_slot_id_t slot);
void launchcore_metadata_set_slot_state(launchcore_metadata_t *md, launchcore_slot_id_t slot, launchcore_slot_state_t state);
bool launchcore_slot_id_is_valid(launchcore_slot_id_t slot);
