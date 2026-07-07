#include "launchcore/metadata.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include <stdio.h>
#include <string.h>

static launchcore_slot_id_t parse_slot(char c)
{
    if (c == 'A' || c == 'a') return LAUNCHCORE_SLOT_A;
    if (c == 'B' || c == 'b') return LAUNCHCORE_SLOT_B;
    return LAUNCHCORE_SLOT_NONE;
}

launchcore_recovery_status_t launchcore_recovery_handle_command(const char *command, char *response, size_t response_len)
{
    if (command == NULL || response == NULL || response_len == 0u) return LAUNCHCORE_RECOVERY_ERR_ARG;
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();

    if (strncmp(command, "HELLO", 5) == 0) {
        snprintf(response, response_len, "OK LAUNCHCORE");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "GET_INFO", 8) == 0) {
        snprintf(response, response_len, "OK A=%08X/%u B=%08X/%u", layout->slot_a_base, layout->slot_a_size, layout->slot_b_base, layout->slot_b_size);
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "GET_METADATA", 12) == 0) {
        launchcore_metadata_t md;
        launchcore_metadata_status_t st = launchcore_metadata_read_latest(&md);
        snprintf(response, response_len, "OK status=%d seq=%u active=%u confirmed=%u pending=%u", st, md.sequence, md.active_slot, md.confirmed_slot, md.pending_slot);
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "ERASE_SLOT ", 11) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[11]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        if (storage->erase(launchcore_slot_base(slot), launchcore_slot_size(slot)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
        snprintf(response, response_len, "OK");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "VERIFY_SLOT ", 12) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[12]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        snprintf(response, response_len, "OK slot=%c", slot == LAUNCHCORE_SLOT_A ? 'A' : 'B');
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "SET_PENDING ", 12) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[12]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        launchcore_metadata_t md;
        (void)launchcore_metadata_read_latest(&md);
        md.pending_slot = (uint8_t)slot;
        md.update_slot = LAUNCHCORE_SLOT_NONE;
        md.boot_attempts = 0u;
        launchcore_metadata_set_slot_state(&md, slot, LAUNCHCORE_SLOT_PENDING);
        if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
        snprintf(response, response_len, "OK");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "RESET", 5) == 0) {
        snprintf(response, response_len, "OK RESET");
        return LAUNCHCORE_RECOVERY_OK;
    }
    snprintf(response, response_len, "ERR COMMAND");
    return LAUNCHCORE_RECOVERY_ERR_COMMAND;
}

void launchcore_recovery_run(void)
{
}
