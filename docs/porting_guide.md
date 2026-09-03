# Porting Guide

Create a concrete board port under `bootloader/boards`. Provide
`board_config.h`, `platform.c`, and a storage driver. For GNU ld builds,
`launchcore_generate_linker_scripts()` can generate the bootloader and
application scripts directly from the addresses in `board_config.h`; a
checked-in `linker_bootloader.ld` is only needed for custom section layouts or
other linker families.
Keep flash erase and write rules inside the storage driver and validate
stack/reset ranges before jumping to an application.

Set the persistent-data base, size, erase size, and write size in the storage
layout. `persistent_data_write_size` must match the flash programming unit and
be a power of two from 4 through 32 bytes; align `persistent_data_base` to both
the erase and write sizes. The region must contain two equal erase-aligned
banks. Linker scripts must name and exclude it from application and bootloader
sections. Verify the compiled map with
`python3 build.py memory-map --board <name>`.

For one full-size application, set `slot_b_size` to zero, make Slot A span the
available application region, and provide separate `app_staging`. If staging is
unavailable, connect the recovery streaming API to the board's serial, USB, CAN,
or network recovery transport.

For the two-region delta policy, keep both slot sizes nonzero, make Slot A the
complete executable region, set `slot_b_is_delta = true`, and set
`slot_erase_size` to the application flash erase unit. Slot B may be smaller
than Slot A but must fit each selected reversible patch. Do not include Slot B
in an application linker script; it contains compressed patch records, not an
image that the processor can execute.

For a new board, start from one of the built-in basic BSP templates in
`bootloader/bsp/templates`:

- `stm32g4_internal_basic`
- `stm32g4_external_basic`
- `stm32h5_internal_basic`
- `stm32h5_external_basic`
- `stm32h5_single_slot_external_stage`
- `stm32h7_internal_basic`
- `stm32h7_external_basic`
- `stm32u7_internal_basic`
- `stm32u7_external_basic`

These templates are intentionally not board examples. They are family/layout
starting points with placeholder flash drivers, conservative vector validation,
and linker memory regions that must be adjusted for the exact part number.

## Generated GNU linker scripts

Include `cmake/launchcore_stm32.cmake` and call:

```cmake
launchcore_generate_linker_scripts(
  BSP_CONFIG "${CMAKE_SOURCE_DIR}/Bootloader/board_config.h"
  LAYOUT_PREFIX BOARD
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/launchcore-linker"
  OUT_BOOTLOADER_SCRIPT LAUNCHCORE_BOOTLOADER_LINKER
  OUT_APPLICATION_SCRIPT LAUNCHCORE_APPLICATION_LINKER)
```

The configuration must define the internal flash/SRAM base and size,
bootloader size, and `${LAYOUT_PREFIX}_SLOT_A_BASE`, `_SLOT_A_SIZE`, and
`_VECTOR_TABLE`. The generated memory regions are named `BOOTLOADER_FLASH`,
`BOOTLOADER_RAM`, `FIRMWARE_FLASH`, and `FIRMWARE_RAM`, so GNU ld's
`--print-memory-usage` output identifies each image.

`launchcore_add_memory_report()` adds a post-link report with Bootloader,
Firmware, and Peak image RAM rows followed by separately labeled Bootloader,
Firmware, and Total flash rows. Values retain exact bytes and also include
readable KiB/MiB units. Binary sizes are used for flash so the combined figure
matches what is emitted for installation; ELF initialized-data and BSS sizes
are used for RAM.
Bootloader and firmware RAM are not added because the images execute at
different times and reuse the same SRAM. The BSP-generated SRAM capacity is
used automatically; ports that provide custom linker scripts must pass
`RAM_CAPACITY` explicitly.

LaunchCore intentionally does not add `--specs=nosys.specs`. The bootloader no
longer depends on hosted I/O, and adding that spec causes recent GNU Arm
toolchains to emit `_close`, `_read`, `_write`, and `_lseek` warnings even when
those functions are discarded.
