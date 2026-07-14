# Porting Guide

Create a concrete board port under `bootloader/boards`. Provide
`board_config.h`, `platform.c`, a storage driver, and `linker_bootloader.ld`.
Keep flash erase and write rules inside the storage driver and validate
stack/reset ranges before jumping to an application.

Set the persistent-data base, size, and erase size in the storage layout. It
must contain two equal erase-aligned banks. Linker scripts must name and exclude
this region from application and bootloader sections. Verify the compiled map
with `python3 build.py memory-map --board <name>`.

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
