# Changelog

## 1.1.0

- Add opt-in strict persistent journal inspection (`launchcore_persist_probe_strict`), which reports a
  damaged or foreign bank as CORRUPT instead of treating it as empty.
- Add strict journal reads and writes (`launchcore_persist_get_strict`, `launchcore_persist_set_strict`,
  `launchcore_persist_delete_strict`) and explicit first-use provisioning
  (`launchcore_persist_provision_virgin_strict`).
- Add `launchcore_persist_repair_interrupted_strict`, which repairs a store left by a write interrupted
  by power loss, or by bytes from an older flash layout, without dropping any record a write reported
  as stored.
- The legacy persistent-store API and the image format are unchanged.

## 1.0.0

- Generate bootloader and application linker scripts from the board BSP layout.
- Build bootloader-only, firmware-only, combined factory, and OTA artifacts.
- Support dual-slot, delta-only, external-staging, and recovery-stream update layouts.
- Preserve application settings in bootloader-managed, power-loss-safe storage.
- Validate Cortex-M application vectors and safely restore interrupt state during handoff.
- Report RAM first, followed by readable bootloader, firmware, and total flash usage.
- Provide streamed application and bootloader updates with interruption-safe host tests.
