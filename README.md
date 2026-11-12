# SEDS LaunchCore

Current stable release: **v1.1.0**. CMake consumers should fetch the
`v1.1.0` tag rather than a development commit. Public interfaces and image
format changes are recorded in [`CHANGELOG.md`](CHANGELOG.md).

SEDS LaunchCore is a small STM32 bootloader framework. The bootloader runs from
internal flash, validates A/B firmware slots in internal or external storage,
then jumps to a confirmed or pending application image.

License: GPLv2.

The current repository includes:

- The bootloader core itself, including startup, slot selection, image
  validation, metadata handling, recovery, and the final jump to user code
  under `bootloader/src/`.
- LaunchCore image header, CRC32, SHA-256, and validation.
- Redundant sequence-numbered metadata with CRC protection.
- A/B slot selection with pending boot and rollback policy.
- Optional two-slot reversible-delta policy: one complete executable image plus
  one block-compressed forward/reverse patch.
- Storage abstraction with host mock storage and STM32 board stubs.
- App-side update library that writes the inactive slot and marks it pending.
- Bounded-memory streaming writes and chunked CRC32/SHA-256 validation for both
  application and staged bootloader images.
- Persistent bootloader-owned update results and failure reasons readable by
  fallback userspace.
- Bootloader-managed, power-loss-safe persistent key/value storage.
- Full-flash single-slot installation through external staging or bootloader
  recovery streaming when A/B capacity is unavailable.
- Streamed app-only, bootloader-only, and combined bootloader-plus-app bundles.
- Staged bootloader-update manifest flow with a separate bootloader signature hook.
- Minimal recovery command parser.
- Python image and metadata inspection tools.

Build and test on the host:

```sh
python3 build.py all
```

To run the tests directly, including configuring and building `build/host` when
it does not exist yet or was created before the repository was moved:

```sh
python3 build.py test
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

Print a board's compiled memory allocation:

```sh
python3 build.py memory-map --board stm32h5_single_slot_external_stage
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
- `stm32h5_single_slot_external_stage`
- `stm32h7_internal_basic`
- `stm32h7_external_basic`
- `stm32u7_internal_basic`
- `stm32u7_external_basic`

Create and inspect an image:

```sh
python3 tools/mkimage.py --input app.bin --output app.launchcore.img \
  --slot-base 0x90000000 --vector-table 0x90000200 --version 1.0.0 --xip
python3 tools/inspect_image.py app.launchcore.img
```

Bundle a signed newer bootloader with an application:

```sh
python3 tools/mkbundle.py --bootloader launchcore_bootloader.bin \
  --bootloader-version 2 --bootloader-signature bootloader.sig \
  --app app.launchcore.img --output update.launchcore.bundle
```

Create one factory-flash binary containing a bootloader and any separately
built, packaged application image:

```sh
python3 build.py factory-image --bootloader launchcore_bootloader.bin \
  --bootloader-base 0x08000000 --app app-internal.launchcore.img \
  --metadata-base 0x08070000 \
  --output launchcore_factory.bin
```

The application address is read from its LaunchCore image header. The command
also creates the initial metadata record needed to boot the application as a
confirmed Slot A image on a blank device. It fills gaps with `0xFF`, verifies
that the regions do not overlap, validates the packaged application, and
defaults to a 64 MiB output-size safety limit. Use the board's `metadata copy 0`
address from `python3 build.py memory-map --board <board>` for
`--metadata-base`. A flat `.bin` is therefore
intended for regions on the same flash device. For layouts with internal
bootloader flash and an application at an external address such as
`0x90000000`, program the artifacts separately or use the streamed combined
update bundle above; padding that range into a flat binary would be impractical.

Create a delta only when it is smaller than a full packaged image:

```sh
python3 tools/mkdelta.py --base current.launchcore.img \
  --target next.launchcore.img --erase-size 0x2000 \
  --slot-size 0x100000 --delta-slot-size 0x80000 \
  --output next.launchcore.delta
```

Delta-capable userspace streams that file with
`launchcore_delta_update_begin/write/finish`. The bootloader decompresses and
applies it on reset; Slot A remains an ordinary uncompressed executable image.
