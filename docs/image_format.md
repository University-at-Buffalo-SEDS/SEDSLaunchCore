# Image Format

Each slot starts with `launchcore_image_header_t`, followed by the application
vector table and firmware payload. The header includes image CRC32, SHA-256,
version, XIP/COPY flags, vector table address, entry address, and reserved
signature space.

The payload begins exactly at `slot_base_addr + header_size`. Packaging defaults
to a `0x200`-byte header so Cortex-M vector tables have a safe VTOR alignment;
boards with a stricter requirement must pass it with `--vector-alignment`.
