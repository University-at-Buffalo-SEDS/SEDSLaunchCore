#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LAUNCHCORE_OK = 0,
    LAUNCHCORE_ERR_NO_SPACE,
    LAUNCHCORE_ERR_BAD_STATE,
    LAUNCHCORE_ERR_BAD_IMAGE,
    LAUNCHCORE_ERR_FLASH,
    LAUNCHCORE_ERR_VERIFY,
    LAUNCHCORE_ERR_METADATA,
} launchcore_status_t;

typedef struct {
    uint32_t image_size;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t version_build;
    uint8_t expected_sha256[32];
} launchcore_update_info_t;

launchcore_status_t launchcore_update_begin(const launchcore_update_info_t *info);
launchcore_status_t launchcore_update_write(const void *data, size_t len);
launchcore_status_t launchcore_update_finish(void);
launchcore_status_t launchcore_update_abort(void);
launchcore_status_t launchcore_confirm_boot(void);
launchcore_status_t launchcore_request_recovery(void);
