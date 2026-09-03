# Built-In BSP Templates

This directory contains basic BSP templates for STM32 families and memory
layouts. They are not concrete board ports. Use them as starting points when
creating a board-specific directory under `bootloader/boards`.

Templates can be built directly during bring-up by passing their directory name
as `LAUNCHCORE_BOARD`:

```sh
python3 build.py build --board stm32g4_internal_basic
python3 build.py build --board stm32h5_internal_basic
python3 build.py build --board stm32h7_external_basic
python3 build.py build --board stm32u7_external_basic
```

All built-in templates can be checked with:

```sh
python3 build.py bsp-templates
```

Template inventory:

- `templates/stm32g4_internal_basic`
- `templates/stm32g4_external_basic`
- `templates/stm32h5_internal_basic`
- `templates/stm32h5_external_basic`
- `templates/stm32h5_single_slot_external_stage`
- `templates/stm32h7_internal_basic`
- `templates/stm32h7_external_basic`
- `templates/stm32u7_internal_basic`
- `templates/stm32u7_external_basic`

Before using one on hardware, fill in the exact flash programming operations,
clock setup, recovery-button policy, system reset, watchdog feed, and linker
memory sizes for the target part and board.
