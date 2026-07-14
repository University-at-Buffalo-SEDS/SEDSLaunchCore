#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

BUNDLE_MAGIC = 0x4C43424E
BUNDLE_VERSION = 1
IMAGE_MAGIC = 0x4C43494D
SIGNATURE_MAX_SIZE = 64
HEADER_FORMAT = "<IHHIIIII32s64sIII32s"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT) + 4
ARTIFACT_ALIGNMENT = 256


def align(value: int) -> int:
    return (value + ARTIFACT_ALIGNMENT - 1) & ~(ARTIFACT_ALIGNMENT - 1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a streamed LaunchCore update bundle.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--bootloader", type=Path)
    parser.add_argument("--bootloader-version", type=lambda value: int(value, 0))
    parser.add_argument("--bootloader-signature", type=Path)
    parser.add_argument("--app", type=Path, help="Packaged LaunchCore application image.")
    args = parser.parse_args()
    if args.bootloader is None and args.app is None:
        parser.error("at least one of --bootloader or --app is required")
    if args.bootloader is not None and (args.bootloader_version is None or args.bootloader_signature is None):
        parser.error("bootloader bundles require --bootloader-version and --bootloader-signature")

    bootloader = args.bootloader.read_bytes() if args.bootloader else b""
    signature = args.bootloader_signature.read_bytes() if args.bootloader_signature else b""
    if len(signature) > SIGNATURE_MAX_SIZE:
        parser.error(f"bootloader signature exceeds {SIGNATURE_MAX_SIZE} bytes")
    app = args.app.read_bytes() if args.app else b""
    if app and (len(app) < 48 or struct.unpack_from("<I", app)[0] != IMAGE_MAGIC):
        parser.error("--app must be a packaged LaunchCore image")

    bootloader_offset = align(HEADER_SIZE) if bootloader else 0
    app_offset = align((bootloader_offset + len(bootloader)) if bootloader else HEADER_SIZE) if app else 0
    total_size = (app_offset + len(app)) if app else (bootloader_offset + len(bootloader))
    app_sha256 = app[16:48] if app else bytes(32)
    values = (
        BUNDLE_MAGIC,
        BUNDLE_VERSION,
        HEADER_SIZE,
        total_size,
        bootloader_offset,
        len(bootloader),
        args.bootloader_version or 0,
        zlib.crc32(bootloader) & 0xFFFFFFFF,
        hashlib.sha256(bootloader).digest() if bootloader else bytes(32),
        signature.ljust(SIGNATURE_MAX_SIZE, b"\0"),
        len(signature),
        app_offset,
        len(app),
        app_sha256,
    )
    header_without_crc = struct.pack(HEADER_FORMAT, *values)
    header = header_without_crc + struct.pack("<I", zlib.crc32(header_without_crc) & 0xFFFFFFFF)
    bundle = bytearray(header)
    if bootloader:
        bundle.extend(bytes(bootloader_offset - len(bundle)))
        bundle.extend(bootloader)
    if app:
        bundle.extend(bytes(app_offset - len(bundle)))
        bundle.extend(app)
    args.output.write_bytes(bundle)


if __name__ == "__main__":
    main()
