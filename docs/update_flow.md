# Update Flow

Applications call `launchcore_update_begin`, stream an already packaged
LaunchCore image with `launchcore_update_write`, then call
`launchcore_update_finish`. The update library chooses the inactive slot,
validates the written image by rereading flash in bounded chunks, marks it
pending only after the full CRC32 and SHA-256 match, and lets the next reset
perform the boot decision. Neither download nor validation requires the full
application image to fit in RAM.

`launchcore_update_info_t.image_size` is the total package size, including the
LaunchCore header. Writes beyond that declared length are rejected, and finish
requires exactly that many bytes before it can mark the slot pending. The
bootloader independently repeats chunked validation on the next boot; userspace
metadata is a completion signal, not a trust decision.

## Update result reporting

Userspace can read the bootloader-owned persistent report after any reset:

```c
launchcore_update_report_t report;
launchcore_status_t status = launchcore_get_update_report(&report);
```

The report identifies an application or bootloader update, whether the
bootloader attempted it, its current result, and a failure reason. `PENDING`
means userspace completed the transfer. `VALIDATED` means the bootloader checked
an application and attempted to boot it. `SUCCESS` is recorded only after the
new application calls `launchcore_confirm_boot()`; bootloader replacement is
recorded successful after verification and flashing complete. `FAILED` remains
available to a fallback application and distinguishes header, size, storage,
CRC32, SHA-256, metadata-hash, signature, flash-install, not-confirmed,
delta-base-mismatch, and decompression failures.

Metadata version 2 stores this report. Version 1 metadata is migrated in memory
when read, preserving slot selection and boot state before the next commit.

## Single-slot installation

When `slot_b_size` is zero, userspace streams the package to `app_staging`. On
reset, the bootloader validates staging before erasing Slot A, copies in bounded
chunks, and validates the installed copy before marking it pending. The install
flag is cleared only after success, so an interrupted copy retries from intact
staging.

Without separate staging, a recovery transport can stream directly through
`launchcore_recovery_install_begin`, `launchcore_recovery_install_write`, and
`launchcore_recovery_install_finish`. This preserves the persistent store but
provides recovery instead of A/B rollback.

## Compressed reversible-delta mode

A board can set `slot_b_is_delta` and `slot_erase_size` in its storage layout.
In this mode Slot A is the only executable application region and Slot B stores
a reversible patch tied to the exact SHA-256 of the complete image currently in
Slot A. Conventional `launchcore_update_begin` is rejected so userspace cannot
mistake the delta region for a bootable slot.

`tools/mkdelta.py` compares erase blocks and stores only changed blocks. Each
old and new block representation independently selects raw or RLE encoding, so
incompressible data does not expand merely to satisfy the codec. By default the
tool refuses to emit a patch that is not smaller than either the raw target or
a maximum-deflate full-image estimate, or that does not fit the configured
delta slot. Applications stream an accepted patch
with `launchcore_delta_update_begin`, `launchcore_delta_update_write`, and
`launchcore_delta_update_finish`.

On reset, the bootloader verifies the exact base hash, then erases and rewrites
changed Slot A blocks while decoding through a bounded buffer. Application is
idempotent: after power loss the bootloader reapplies every changed block from
the intact patch. It validates the complete reconstructed image before the
first boot. The patch retains both old and new block data; if the application
does not confirm before the boot-attempt limit, the bootloader applies the old
blocks and validates the restored image before booting it.

The application itself remains uncompressed in Slot A. Cortex-M instruction
fetches and ordinary data loads do not call bootloader hooks, so transparent
on-demand decompression is not possible for an image larger than RAM. Explicit
compressed assets may use a separate application-facing service, but that is
not part of the executable-image slot contract.
