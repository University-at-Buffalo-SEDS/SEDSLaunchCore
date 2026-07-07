# Image Format

Each slot starts with `launchcore_image_header_t`, followed by the application
vector table and firmware payload. The header includes image CRC32, SHA-256,
version, XIP/COPY flags, vector table address, entry address, and reserved
signature space.
