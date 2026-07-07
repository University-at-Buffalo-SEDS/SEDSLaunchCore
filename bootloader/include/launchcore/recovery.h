#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    LAUNCHCORE_RECOVERY_OK = 0,
    LAUNCHCORE_RECOVERY_ERR_COMMAND,
    LAUNCHCORE_RECOVERY_ERR_ARG,
    LAUNCHCORE_RECOVERY_ERR_STORAGE,
} launchcore_recovery_status_t;

launchcore_recovery_status_t launchcore_recovery_handle_command(const char *command, char *response, size_t response_len);
void launchcore_recovery_run(void);
