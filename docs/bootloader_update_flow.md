# Bootloader Update Flow

LaunchCore supports bootloader updates as a separate staged flow from normal
application A/B updates.

1. The application downloads a bundle containing a bootloader image, a
   bootloader-update manifest, and the next application image.
2. The application writes the bootloader image and app image to staging storage
   in chunks through the storage driver.
3. Metadata is marked with `BOOTLOADER_UPDATE_PENDING`.
4. On reset, the current bootloader rereads the staged image in bounded chunks
   and validates the manifest, CRC32, SHA-256, and bootloader-specific signature.
5. Only after the separate bootloader signature succeeds does the current
   bootloader erase/program the protected bootloader region.
6. The current bootloader sets `BOOTLOADER_UPDATE_CONTINUE_APP` and resets.
7. The new bootloader starts, validates the staged application image using its
   normal app image policy, installs or marks it pending, then clears the
   continuation flag.

The streamed bundle API accepts app-only, bootloader-only, and combined
bundles. Use `launchcore_bundle_update_begin/write/finish` in userspace and
`tools/mkbundle.py` when packaging. A combined bundle installs a bootloader only
when its version is newer than `LAUNCHCORE_BOOTLOADER_VERSION`; application
installation is deferred until after the bootloader reset boundary.

The bootloader image and application image intentionally use separate trust
decisions. A valid application signature must not authorize bootloader code, and
a valid bootloader signature must not bypass application image validation.
Neither image must fit in RAM during download or validation.

Hardware ports must implement:

- `platform_verify_bootloader_signature`
- `platform_flash_bootloader_image`
- exact write-protection/option-byte policy for the bootloader region

The default weak implementations fail closed.
