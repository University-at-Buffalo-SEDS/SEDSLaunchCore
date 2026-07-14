#pragma once

#include "launchcore/metadata.h"
#include "launchcore/update.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool active;
    launchcore_slot_id_t slot;
    uint32_t write_base;
    uint32_t offset;
    bool staged_install;
    launchcore_update_info_t info;
} launchcore_update_state_t;

launchcore_update_state_t *launchcore_update_state(void);
bool launchcore_expected_hash_is_set(const uint8_t hash[32]);
