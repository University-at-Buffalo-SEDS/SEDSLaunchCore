#include "launchcore/metadata.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include <string.h>

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
} response_builder_t;

static void response_init(response_builder_t *builder, char *response, size_t response_len)
{
    builder->data = response;
    builder->capacity = response_len;
    builder->length = 0u;
    response[0] = '\0';
}

static void response_append_char(response_builder_t *builder, char value)
{
    if (builder->length + 1u >= builder->capacity) return;
    builder->data[builder->length++] = value;
    builder->data[builder->length] = '\0';
}

static void response_append(response_builder_t *builder, const char *value)
{
    while (*value != '\0') response_append_char(builder, *value++);
}

static void response_append_u32(response_builder_t *builder, uint32_t value)
{
    char digits[10];
    size_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count > 0u) response_append_char(builder, digits[--count]);
}

static void response_append_hex32(response_builder_t *builder, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned int shift = 28u;; shift -= 4u) {
        response_append_char(builder, hex[(value >> shift) & 0xFu]);
        if (shift == 0u) break;
    }
}

static void response_set(response_builder_t *builder, const char *value)
{
    response_append(builder, value);
}

static launchcore_slot_id_t parse_slot(char c)
{
    if (c == 'A' || c == 'a') return LAUNCHCORE_SLOT_A;
    if (c == 'B' || c == 'b') return LAUNCHCORE_SLOT_B;
    return LAUNCHCORE_SLOT_NONE;
}

launchcore_recovery_status_t launchcore_recovery_handle_command(const char *command, char *response, size_t response_len)
{
    if (command == NULL || response == NULL || response_len == 0u) return LAUNCHCORE_RECOVERY_ERR_ARG;
    response_builder_t builder;
    response_init(&builder, response, response_len);
    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    const launchcore_storage_layout_t *layout = storage->layout();

    if (strncmp(command, "HELLO", 5) == 0) {
        response_set(&builder, "OK LAUNCHCORE");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "GET_INFO", 8) == 0) {
        response_set(&builder, "OK A=");
        response_append_hex32(&builder, layout->slot_a_base);
        response_append_char(&builder, '/');
        response_append_u32(&builder, layout->slot_a_size);
        response_append(&builder, " B=");
        response_append_hex32(&builder, layout->slot_b_base);
        response_append_char(&builder, '/');
        response_append_u32(&builder, layout->slot_b_size);
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "GET_METADATA", 12) == 0) {
        launchcore_metadata_t md;
        launchcore_metadata_init_default(&md);
        launchcore_metadata_status_t st = launchcore_metadata_read_latest(&md);
        response_set(&builder, "OK status=");
        response_append_u32(&builder, (uint32_t)st);
        response_append(&builder, " seq=");
        response_append_u32(&builder, md.sequence);
        response_append(&builder, " active=");
        response_append_u32(&builder, md.active_slot);
        response_append(&builder, " confirmed=");
        response_append_u32(&builder, md.confirmed_slot);
        response_append(&builder, " pending=");
        response_append_u32(&builder, md.pending_slot);
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "ERASE_SLOT ", 11) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[11]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        if (!launchcore_storage_range_preserves_persistent(launchcore_slot_base(slot), launchcore_slot_size(slot))) {
            return LAUNCHCORE_RECOVERY_ERR_ARG;
        }
        if (storage->erase(launchcore_slot_base(slot), launchcore_slot_size(slot)) != LAUNCHCORE_STORAGE_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
        response_set(&builder, "OK");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "VERIFY_SLOT ", 12) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[12]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        response_set(&builder, "OK slot=");
        response_append_char(&builder, slot == LAUNCHCORE_SLOT_A ? 'A' : 'B');
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "SET_PENDING ", 12) == 0) {
        launchcore_slot_id_t slot = parse_slot(command[12]);
        if (!launchcore_slot_id_is_valid(slot)) return LAUNCHCORE_RECOVERY_ERR_ARG;
        launchcore_metadata_t md;
        launchcore_metadata_init_default(&md);
        (void)launchcore_metadata_read_latest(&md);
        md.pending_slot = (uint8_t)slot;
        md.update_slot = LAUNCHCORE_SLOT_NONE;
        md.boot_attempts = 0u;
        launchcore_metadata_set_slot_state(&md, slot, LAUNCHCORE_SLOT_PENDING);
        if (launchcore_metadata_commit(&md) != LAUNCHCORE_METADATA_OK) return LAUNCHCORE_RECOVERY_ERR_STORAGE;
        response_set(&builder, "OK");
        return LAUNCHCORE_RECOVERY_OK;
    }
    if (strncmp(command, "RESET", 5) == 0) {
        response_set(&builder, "OK RESET");
        return LAUNCHCORE_RECOVERY_OK;
    }
    response_set(&builder, "ERR COMMAND");
    return LAUNCHCORE_RECOVERY_ERR_COMMAND;
}

void launchcore_recovery_run(void)
{
}
