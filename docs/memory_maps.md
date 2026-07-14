# Memory Maps

The external-flash example uses internal flash for the bootloader and QSPI NOR
for Slot A, Slot B, assets, and redundant metadata. The internal-flash example
uses internal flash for all regions when capacity permits.

## Capacity modes

Conventional A/B requires both slots to hold a complete packaged application.
Boards with similar releases can instead designate Slot B as a compressed
reversible-delta region. Slot A then spans the contiguous executable capacity;
Slot B needs to hold only the effective patch. This is opportunistic: arbitrary
firmware pairs can differ too much to fit, so packaging must compare the
compressed patch with full-image alternatives for every release.

Set `slot_b_is_delta = true` and set `slot_erase_size` to the physical erase
unit. Slot A and Slot B must not overlap each other, metadata, the bootloader,
or persistent data. The printed map labels Slot B as `compressed delta slot` so
it is not mistaken for an executable region.

Boards that need nearly all internal application flash for one image should use
a single internal application region with external staging storage. The update
is streamed to staging, validated there by the bootloader, and copied into the
internal region in erase-sized chunks. The staged copy must remain intact until
installation and first-boot confirmation are complete so interrupted installs
can resume or recover. LaunchCore selects this policy when `slot_b_size` is zero
and `app_staging_size` is nonzero. The
`stm32h5_single_slot_external_stage` template is a reference layout.

Internal-flash-only boards can use nearly the full application region through
`launchcore_recovery_install_begin/write/finish`. The bootloader is running
while this API overwrites the application, so no second full image is required.
This preserves persistent data but cannot automatically roll back to the
overwritten application. Delta packages can reduce typical transfer and scratch
requirements, but cannot guarantee that arbitrary firmware pairs fit. An
ineffective delta must use a full-image staging or recovery path supported by
that board.

## Persistent application data

Persistent variables must occupy a dedicated flash partition outside the
bootloader, application slots, metadata copies, and staging regions. Board
linker scripts must exclude that partition, and storage drivers must ensure no
application or bootloader update erase operation overlaps it.

LaunchCore provides a power-loss-safe two-bank journal with record CRCs and
sequence numbers. See `docs/persistent_store.md`.

Print the exact compiled allocation for a board with:

```sh
python3 build.py memory-map --board stm32h5_single_slot_external_stage
```

The report and built-in linker scripts include bootloader, applications,
metadata, persistent store, and staging regions.
