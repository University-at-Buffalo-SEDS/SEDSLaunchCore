#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LAUNCHCORE_RECOVERY_OK = 0,
    LAUNCHCORE_RECOVERY_ERR_COMMAND,
    LAUNCHCORE_RECOVERY_ERR_ARG,
    LAUNCHCORE_RECOVERY_ERR_STORAGE,
    LAUNCHCORE_RECOVERY_ERR_BAD_STATE,
    LAUNCHCORE_RECOVERY_ERR_BAD_IMAGE,
    LAUNCHCORE_RECOVERY_ERR_NO_SPACE,
} launchcore_recovery_status_t;

launchcore_recovery_status_t launchcore_recovery_handle_command(const char *command, char *response, size_t response_len);
launchcore_recovery_status_t launchcore_recovery_install_begin(uint32_t image_size);
launchcore_recovery_status_t launchcore_recovery_install_write(const void *data, size_t len);
launchcore_recovery_status_t launchcore_recovery_install_finish(void);
launchcore_recovery_status_t launchcore_recovery_install_abort(void);
void launchcore_recovery_run(void);
