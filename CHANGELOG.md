# Changelog

## 1.0.0

- Generate bootloader and application linker scripts from the board BSP layout.
- Build bootloader-only, firmware-only, combined factory, and OTA artifacts.
- Support dual-slot, delta-only, external-staging, and recovery-stream update layouts.
- Preserve application settings in bootloader-managed, power-loss-safe storage.
- Validate Cortex-M application vectors and safely restore interrupt state during handoff.
- Report RAM first, followed by readable bootloader, firmware, and total flash usage.
- Provide streamed application and bootloader updates with interruption-safe host tests.
