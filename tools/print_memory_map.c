#include "launchcore/storage.h"
#include <inttypes.h>
#include <stdio.h>

extern const launchcore_storage_driver_t launchcore_board_storage_driver;

static void print_region(const char *name, uint32_t base, uint32_t size)
{
    if (size == 0u) return;
    printf("%-22s 0x%08" PRIX32 " - 0x%08" PRIX32 "  (%" PRIu32 " bytes)\n",
        name, base, base + size - 1u, size);
}

int main(void)
{
    const launchcore_storage_layout_t *layout = launchcore_board_storage_driver.layout();
    puts("LaunchCore memory allocation:");
    print_region("bootloader", layout->bootloader_base, layout->bootloader_size);
    print_region("application slot A", layout->slot_a_base, layout->slot_a_size);
    print_region(layout->slot_b_is_delta ? "compressed delta slot" : "application slot B",
        layout->slot_b_base, layout->slot_b_size);
    print_region("metadata copy 0", layout->metadata0_base, layout->metadata_size);
    print_region("metadata copy 1", layout->metadata1_base, layout->metadata_size);
    print_region("persistent store", layout->persistent_data_base, layout->persistent_data_size);
    print_region("application staging", layout->app_staging_base, layout->app_staging_size);
    print_region("bootloader staging", layout->bootloader_staging_base, layout->bootloader_staging_size);
    return 0;
}
