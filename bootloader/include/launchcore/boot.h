#pragma once

#include <stdint.h>
#include "launchcore/metadata.h"

launchcore_slot_id_t launchcore_select_slot(const launchcore_metadata_t *md);
typedef enum {
    LAUNCHCORE_APP_INSTALL_NONE = 0,
    LAUNCHCORE_APP_INSTALL_READY,
    LAUNCHCORE_APP_INSTALL_REJECTED,
    LAUNCHCORE_APP_INSTALL_RETRY,
} launchcore_app_install_status_t;

launchcore_app_install_status_t launchcore_install_staged_application(launchcore_metadata_t *md);
void launchcore_jump_to_image(uint32_t vector_table_addr);
