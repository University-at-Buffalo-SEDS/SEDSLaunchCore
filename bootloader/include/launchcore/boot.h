#pragma once

#include <stdint.h>
#include "launchcore/metadata.h"

launchcore_slot_id_t launchcore_select_slot(const launchcore_metadata_t *md);
void launchcore_jump_to_image(uint32_t vector_table_addr);
