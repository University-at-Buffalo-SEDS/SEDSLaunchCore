#include "launchcore/boot.h"
#include "launchcore/image.h"
#include "launchcore/metadata.h"
#include "launchcore/platform.h"
#include "launchcore/recovery.h"
#include "launchcore/storage.h"
#include <stddef.h>

int main(void)
{
    platform_early_init();
    platform_clock_init();
    platform_external_ram_init();
    platform_external_flash_init();
    if (launchcore_storage_init() != LAUNCHCORE_STORAGE_OK) {
        launchcore_recovery_run();
        return 0;
    }

    launchcore_metadata_t md;
    (void)launchcore_metadata_read_latest(&md);
    launchcore_slot_id_t slot = launchcore_select_slot(&md);
    if (slot == LAUNCHCORE_SLOT_NONE) {
        launchcore_recovery_run();
        return 0;
    }

    const launchcore_storage_driver_t *storage = launchcore_storage_driver();
    launchcore_image_header_t header;
    uint32_t base = launchcore_slot_base(slot);
    if (storage->read(base, &header, sizeof(header)) != LAUNCHCORE_STORAGE_OK ||
        launchcore_image_validate_header(&header) != LAUNCHCORE_IMAGE_OK) {
        launchcore_recovery_run();
        return 0;
    }
    if ((header.image_flags & LAUNCHCORE_IMAGE_FLAG_XIP) != 0u) {
        (void)storage->enable_memory_mapped();
    }
    launchcore_jump_to_image(header.vector_table_addr);
    launchcore_recovery_run();
    return 0;
}
