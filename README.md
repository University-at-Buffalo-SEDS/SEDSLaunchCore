# SEDS LaunchCore

SEDS LaunchCore is a small STM32 bootloader framework. The bootloader runs from
internal flash, validates A/B firmware slots in internal or external storage,
then jumps to a confirmed or pending application image.

License: GPLv2.

The current repository includes:

- LaunchCore image header, CRC32, SHA-256, and validation.
- Redundant sequence-numbered metadata with CRC protection.
- A/B slot selection with pending boot and rollback policy.
- Storage abstraction with host mock storage and STM32 board stubs.
- App-side update library that writes the inactive slot and marks it pending.
- Staged bootloader-update manifest flow with a separate bootloader signature hook.
- Minimal recovery command parser.
- Python image and metadata inspection tools.

Build and test on the host:

```sh
python3 build.py all
```

Build with a board port selected:

```sh
python3 build.py build --board stm32h7_extflash_example
```

Built-in basic BSP templates are also selectable through `LAUNCHCORE_BOARD`:

```sh
python3 build.py build --board stm32g4_internal_basic
python3 build.py build --board stm32h5_internal_basic
python3 build.py build --board stm32h7_external_basic
python3 build.py build --board stm32u7_external_basic
```

To build every built-in BSP template:

```sh
python3 build.py bsp-templates
```

Flash helper examples:

```sh
python3 build.py flash --tool launchcore --image app.launchcore.img --slot A
python3 build.py flash --tool stm32cubeprogrammer --image app.launchcore.img \
  --address 0x90000000 --connect port=SWD --verify --reset
```

Available templates:

- `stm32g4_internal_basic`
- `stm32g4_external_basic`
- `stm32h5_internal_basic`
- `stm32h5_external_basic`
- `stm32h7_internal_basic`
- `stm32h7_external_basic`
- `stm32u7_internal_basic`
- `stm32u7_external_basic`

Create and inspect an image:

```sh
python3 tools/mkimage.py --input app.bin --output app.launchcore.img \
  --slot-base 0x90000000 --vector-table 0x90000100 --version 1.0.0 --xip
python3 tools/inspect_image.py app.launchcore.img
```
