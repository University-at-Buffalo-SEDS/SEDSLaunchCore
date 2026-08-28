# SEDS LaunchCore Repository Specification

**Project name:** SEDS LaunchCore
**Primary target:** STM32 microcontrollers
**Status:** Current repository specification

SEDS LaunchCore is a small STM32 bootloader framework. The bootloader runs from
internal flash, initializes the board resources needed to access firmware
storage, validates a LaunchCore image, selects a confirmed or pending firmware
slot, and jumps to the application.

The repository supports internal-flash and external-flash slot layouts, an
application-side update library, redundant metadata, rollback after failed
pending boots, a minimal recovery command parser, and staged bootloader update
metadata with board-controlled signature and flash hooks.

## 1. Goals

- Keep the internal-flash bootloader deterministic, portable, and small.
- Support A/B application slots in internal flash or external memory-mapped
  flash.
- Keep slot selection, metadata format, image validation, and rollback policy
  owned by LaunchCore.
- Let the application own download transport while using the LaunchCore update
  library to write the inactive slot.
- Isolate board-specific clock, storage, recovery, reset, watchdog, vector
  validation, and bootloader flashing behavior behind BSP interfaces.
- Keep image and metadata formats shared by firmware, host tools, tests, and
  update code.

## 2. Non-Goals

- Networking in the bootloader.
- A filesystem in the bootloader.
- User interface code in the bootloader.
- Delta update support.
- Production key management.
- A universal STM32 flash driver. BSP templates contain placeholders and must
  be completed for each board.

## 3. Repository Layout

```text
README.md
LICENSE
CMakeLists.txt
build.py
docs/
bootloader/
  include/launchcore/
  src/
  boards/
  bsp/templates/
update_lib/
  include/launchcore/
  src/
tools/
examples/
tests/
```

Built-in BSP templates:

- `stm32g4_internal_basic`
- `stm32g4_external_basic`
- `stm32h5_internal_basic`
- `stm32h5_external_basic`
- `stm32h7_internal_basic`
- `stm32h7_external_basic`
- `stm32u7_internal_basic`
- `stm32u7_external_basic`

Concrete board ports live under `bootloader/boards/<board_name>`. Built-in
templates live under `bootloader/bsp/templates/<template_name>` and can be
selected directly with `LAUNCHCORE_BOARD`.

## 4. Boot Flow

Normal boot:

```text
Reset
  -> platform_early_init()
  -> platform_clock_init()
  -> platform_external_ram_init()
  -> platform_external_flash_init()
  -> launchcore_storage_init()
  -> read newest valid metadata copy
  -> enter recovery if requested or required
  -> select pending or confirmed slot
  -> read and validate LaunchCore image header
  -> validate payload CRC and SHA-256
  -> enable memory-mapped storage when required
  -> validate application vector through the BSP
  -> deinitialize bootloader-owned hardware
  -> set VTOR and MSP
  -> jump to application reset handler
```

Pending image policy:

- A pending slot is preferred while `boot_attempts < max_boot_attempts`.
- The application confirms a successful pending boot by calling
  `launchcore_confirm_boot()`.
- A confirmed pending slot becomes the new confirmed slot.
- The old confirmed slot remains available as rollback.
- An invalid or exhausted pending slot is marked bad and the bootloader falls
  back to the confirmed slot when one exists.
- If no valid slot can be selected, recovery mode is entered.

## 5. Image Format

Every bootable image begins with `launchcore_image_header_t`, followed by the
application vector table and image payload.

Current constants:

```c
#define LAUNCHCORE_IMAGE_MAGIC 0x4C43494Du
#define LAUNCHCORE_IMAGE_HEADER_VERSION 1u
#define LAUNCHCORE_HASH_SHA256_SIZE 32u
#define LAUNCHCORE_SIGNATURE_MAX_SIZE 64u
```

Current image flags:

```c
LAUNCHCORE_IMAGE_FLAG_XIP
LAUNCHCORE_IMAGE_FLAG_COPY_RAM
LAUNCHCORE_IMAGE_FLAG_SIGNED
LAUNCHCORE_IMAGE_FLAG_BOOTLOADER
LAUNCHCORE_IMAGE_FLAG_APPLICATION
```

Header fields:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t image_sha256[32];
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    uint32_t version_build;
    uint32_t image_flags;
    uint32_t slot_base_addr;
    uint32_t vector_table_addr;
    uint32_t load_addr;
    uint32_t entry_addr;
    uint32_t min_bootloader_version;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t signature[64];
    uint32_t header_crc32;
} launchcore_image_header_t;
```

Validation requirements:

- `magic` and `header_version` must match supported values.
- `header_size` must be at least the compiled header size.
- `header_crc32` must validate with the CRC field cleared for calculation.
- `image_crc32` and `image_sha256` must match the payload.
- `vector_table_addr` and `entry_addr` must be accepted by the board port.
- XIP images require storage that reports executable addresses and can enter
  memory-mapped mode.

## 6. Metadata Format

Metadata is stored redundantly in two storage locations. The valid copy with the
highest sequence number is authoritative.

Current metadata flags:

```c
LAUNCHCORE_METADATA_FLAG_RECOVERY_REQUESTED
LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_PENDING
LAUNCHCORE_METADATA_FLAG_BOOTLOADER_UPDATE_CONTINUE_APP
```

Current slot IDs:

```c
LAUNCHCORE_SLOT_A
LAUNCHCORE_SLOT_B
LAUNCHCORE_SLOT_NONE
```

Current slot states:

```c
LAUNCHCORE_SLOT_EMPTY
LAUNCHCORE_SLOT_WRITING
LAUNCHCORE_SLOT_PENDING
LAUNCHCORE_SLOT_CONFIRMED
LAUNCHCORE_SLOT_VALID_ROLLBACK
LAUNCHCORE_SLOT_BAD
```

Metadata fields:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint8_t active_slot;
    uint8_t confirmed_slot;
    uint8_t pending_slot;
    uint8_t update_slot;
    uint8_t slot_a_state;
    uint8_t slot_b_state;
    uint8_t boot_attempts;
    uint8_t max_boot_attempts;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint8_t slot_a_hash[32];
    uint8_t slot_b_hash[32];
    uint32_t flags;
    uint32_t metadata_crc32;
} launchcore_metadata_t;
```

Power-loss rule:

1. Read both metadata copies.
2. Select the newest valid copy.
3. Create a new record with `sequence + 1`.
4. Write the new record to the older or erased metadata location.
5. Verify the written copy.
6. Do not erase the only valid copy before a newer valid copy exists.

## 7. Storage Interface

Board storage drivers implement `launchcore_storage_driver_t`:

```c
typedef struct {
    launchcore_storage_status_t (*init)(void);
    launchcore_storage_status_t (*erase)(uint32_t addr, uint32_t len);
    launchcore_storage_status_t (*write)(uint32_t addr, const void *data, uint32_t len);
    launchcore_storage_status_t (*read)(uint32_t addr, void *data, uint32_t len);
    launchcore_storage_status_t (*enable_memory_mapped)(void);
    bool (*is_executable_addr)(uint32_t addr);
    const launchcore_storage_layout_t *(*layout)(void);
} launchcore_storage_driver_t;
```

The storage layout includes application slots, metadata copies, bootloader
region, bootloader staging region, optional application staging region, and an
XIP capability flag.

```c
typedef struct {
    uint32_t slot_a_base;
    uint32_t slot_a_size;
    uint32_t slot_b_base;
    uint32_t slot_b_size;
    uint32_t metadata0_base;
    uint32_t metadata1_base;
    uint32_t metadata_size;
    uint32_t bootloader_base;
    uint32_t bootloader_size;
    uint32_t bootloader_staging_base;
    uint32_t bootloader_staging_size;
    uint32_t app_staging_base;
    uint32_t app_staging_size;
    uint32_t persistent_data_base;
    uint32_t persistent_data_size;
    uint32_t persistent_data_erase_size;
    uint32_t slot_erase_size;
    bool slot_b_is_delta;
    bool supports_xip;
} launchcore_storage_layout_t;
```

The persistent region contains two equal erase-aligned journal banks and must
not overlap any region erased during application or bootloader installation.
`slot_b_size == 0` selects single-slot installation through `app_staging`.
`slot_b_is_delta` instead selects a two-region policy where Slot A contains the
complete executable application and Slot B contains a compressed reversible
delta. `slot_erase_size` is mandatory in that mode.

## 8. Board Port Interface

Every board port or BSP template provides:

```text
board_config.h
platform.c
linker_bootloader.ld
storage_internal_flash.c or storage_external_flash.c/storage_qspi.c
external_ram.c when needed
```

Current platform hooks:

```c
void platform_early_init(void);
void platform_clock_init(void);
void platform_external_ram_init(void);
void platform_external_flash_init(void);
void platform_deinit_before_jump(void);
bool platform_recovery_requested(void);
void platform_system_reset(void);
uint32_t platform_get_reset_reason(void);
void platform_feed_watchdog(void);
bool platform_validate_app_vector(uint32_t vector_table_addr,
                                  uint32_t stack_pointer,
                                  uint32_t reset_handler);
bool platform_verify_bootloader_signature(const uint8_t hash[32],
                                          const uint8_t *signature,
                                          uint32_t signature_len);
bool platform_flash_bootloader_image(uint32_t source_addr,
                                     uint32_t image_size);
```

BSP templates are compile-time starting points. Their erase, write, read,
signature, reset, watchdog, and bootloader-flash hooks are placeholders until a
concrete board port supplies hardware behavior.

## 9. Memory Layouts

Internal-flash layouts keep the bootloader at `0x08000000`, place two
application slots after the bootloader when capacity permits, and store two
metadata copies in reserved flash pages near the end of the region.

External-flash layouts keep the bootloader in internal flash and place
application slots, bootloader staging, and metadata in external memory-mapped
flash. H7 templates use `0x90000000`, U7 templates use `0x70000000`, and H5
external templates use `0x90000000`.

All memory sizes, erase sizes, TrustZone/security splits, external loader
requirements, and cache/MPU policies must be finalized per concrete board.

## 10. Application Update Library

The public update API is:

```c
launchcore_status_t launchcore_update_begin(const launchcore_update_info_t *info);
launchcore_status_t launchcore_update_write(const void *data, size_t len);
launchcore_status_t launchcore_update_finish(void);
launchcore_status_t launchcore_update_abort(void);
launchcore_status_t launchcore_confirm_boot(void);
launchcore_status_t launchcore_request_recovery(void);
```

Behavior:

- `launchcore_update_begin()` reads metadata, chooses the inactive slot,
  verifies capacity, erases it, and marks it `WRITING`.
- `launchcore_update_write()` streams the image into the selected slot and
  rejects writes beyond the slot capacity.
- `launchcore_update_finish()` validates the image, marks the slot `PENDING`,
  sets `pending_slot`, and resets boot attempts.
- `launchcore_update_abort()` clears an incomplete update state.
- `launchcore_confirm_boot()` confirms the currently running pending image.
- `launchcore_request_recovery()` records a recovery request in metadata.
- `launchcore_get_update_report()` returns the bootloader-owned attempted,
  result, and failure-reason state retained for fallback userspace.
- `launchcore_bundle_update_begin/write/finish()` streams app-only,
  bootloader-only, or combined bundles to staging.
- `launchcore_delta_update_begin/write/finish()` streams an exact-base,
  reversible block patch to a delta-designated Slot B.

Persistent settings use `launchcore_persist_init/get/set/delete`. Values are
bounded by `LAUNCHCORE_PERSIST_MAX_VALUE_SIZE`; records and bank headers carry
CRCs and sequence/generation counters.

## 11. Bootloader Update Flow

Bootloader updates use `launchcore_bootloader_update_manifest_t`. The manifest
contains the staged bootloader image address, size, CRC32, SHA-256, signature
field, optional paired application image information, required current
bootloader version, required new bootloader version, and manifest CRC.

Required behavior:

- Validate manifest magic, version, size, and CRC.
- Verify staged bootloader CRC32 and SHA-256.
- Call `platform_verify_bootloader_signature()` before accepting a bootloader
  image.
- Call `platform_flash_bootloader_image()` to perform the board-specific
  replacement operation.
- Use metadata flags to track pending bootloader update state and whether
  application installation must continue after the bootloader update.

Default or template implementations must fail closed for signature verification
and bootloader flashing.

Combined bundles install a strictly newer signed bootloader first, set the
continuation flag, and reset. The new bootloader then validates and installs the
staged application. Bootloader-only bundles stop after replacement; app-only
bundles proceed directly to normal application installation.

## 12. Recovery Protocol

The recovery parser exposes:

```c
launchcore_recovery_status_t launchcore_recovery_handle_command(
    const char *command,
    char *response,
    size_t response_len);
void launchcore_recovery_run(void);
```

Binary recovery transports may install a full single-slot image with
`launchcore_recovery_install_begin/write/finish`. This path runs entirely in the
bootloader, preserves the persistent partition, and does not provide A/B
rollback.

Supported command strings are intentionally minimal and board-transport
independent. Recovery mode is entered when requested by the board, requested by
metadata, required by invalid metadata with no valid image, or required because
no bootable slot can be selected.

## 13. Build and Test

Host build and tests:

```sh
python3 build.py all
```

Build one board or template:

```sh
python3 build.py build --board stm32h5_internal_basic
python3 build.py build --board stm32h5_external_basic
python3 build.py build --board stm32h7_external_basic
```

Cross builds with `cmake/arm-none-eabi.cmake` infer the ARM CPU from the board
family:

- STM32G4: `cortex-m4` / ARMv7E-M
- STM32H7: `cortex-m7` / ARMv7E-M
- STM32H5: `cortex-m33` / ARMv8-M
- STM32U7: `cortex-m33` / ARMv8-M

Custom board names that do not start with one of those family prefixes must set
`-DLAUNCHCORE_CPU=<cpu>`.

Build every built-in BSP template:

```sh
python3 build.py bsp-templates
```

Create and inspect an image:

```sh
python3 tools/mkimage.py --input app.bin --output app.launchcore.img \
  --slot-base 0x90000000 --vector-table 0x90000200 --version 1.0.0 --xip
python3 tools/inspect_image.py app.launchcore.img
```

## 14. Coding Rules

- C11 for firmware code.
- Python 3.11 or newer for host tools.
- Public functions use the `launchcore_` prefix.
- Use fixed-width integer types for serialized data and hardware addresses.
- Avoid dynamic allocation, recursion, and unbounded loops in the bootloader.
- Keep board-specific behavior under `bootloader/boards` or
  `bootloader/bsp/templates`.
- Keep file formats backward-compatible or versioned.

## 15. Acceptance Criteria

The repository is in good standing when:

- Host build and CTest pass.
- Every built-in BSP template configures and builds.
- Image header, metadata, and slot-selection tests pass.
- `mkimage.py` creates headers accepted by `inspect_image.py` and the C
  validation code.
- The update library chooses the inactive slot and marks finished updates
  pending.
- Bootloader update validation fails closed without a board signature hook.
- Concrete board ports validate stack pointer and reset handler ranges before
  jumping.
