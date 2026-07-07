# Porting Guide

Create a concrete board port under `bootloader/boards`. Provide
`board_config.h`, `platform.c`, a storage driver, and `linker_bootloader.ld`.
Keep flash erase and write rules inside the storage driver and validate
stack/reset ranges before jumping to an application.

For a new board, start from one of the built-in basic BSP templates in
`bootloader/bsp/templates`:

- `stm32g4_internal_basic`
- `stm32g4_external_basic`
- `stm32h7_internal_basic`
- `stm32h7_external_basic`
- `stm32u7_internal_basic`
- `stm32u7_external_basic`

These templates are intentionally not board examples. They are family/layout
starting points with placeholder flash drivers, conservative vector validation,
and linker memory regions that must be adjusted for the exact part number.
