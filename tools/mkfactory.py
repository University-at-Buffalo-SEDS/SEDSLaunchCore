#!/usr/bin/env python3
"""Create a flat factory image containing a bootloader and application image."""

import argparse
import hashlib
import struct
import zlib
from pathlib import Path


IMAGE_MAGIC = 0x4C43494D
IMAGE_HEADER_FORMAT_WITHOUT_CRC = "<IHHII32sIIIIIIIIIIII64s"
IMAGE_HEADER_STRUCT_SIZE = struct.calcsize(IMAGE_HEADER_FORMAT_WITHOUT_CRC) + 4
METADATA_MAGIC = 0x4C434D44
METADATA_VERSION = 2
METADATA_FORMAT_WITHOUT_CRC = "<IHHI8BII32s32sI4BII32sIII"
METADATA_SIZE = struct.calcsize(METADATA_FORMAT_WITHOUT_CRC) + 4
DEFAULT_MAX_OUTPUT_SIZE = 64 * 1024 * 1024


def parse_u32(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("address must fit in an unsigned 32-bit value")
    return value


def positive_size(text: str) -> int:
    value = int(text, 0)
    if value <= 0:
        raise argparse.ArgumentTypeError("size must be greater than zero")
    return value


def application_info(image: bytes) -> tuple[int, int, bytes]:
    if len(image) < IMAGE_HEADER_STRUCT_SIZE:
        raise ValueError("application is too short to contain a LaunchCore image header")
    magic, header_version, header_size, payload_size, payload_crc = struct.unpack_from("<IHHII", image)
    if magic != IMAGE_MAGIC:
        raise ValueError("application must be a packaged LaunchCore image")
    if header_version != 1:
        raise ValueError(f"unsupported application header version {header_version}")
    if header_size < IMAGE_HEADER_STRUCT_SIZE or header_size + payload_size != len(image):
        raise ValueError("application header size does not match the file size")
    stored_header_crc = struct.unpack_from("<I", image, IMAGE_HEADER_STRUCT_SIZE - 4)[0]
    calculated_header_crc = zlib.crc32(image[:IMAGE_HEADER_STRUCT_SIZE - 4]) & 0xFFFFFFFF
    if stored_header_crc != calculated_header_crc:
        raise ValueError("application header CRC32 is invalid")
    payload = image[header_size:]
    if zlib.crc32(payload) & 0xFFFFFFFF != payload_crc:
        raise ValueError("application payload CRC32 is invalid")
    if hashlib.sha256(payload).digest() != image[16:48]:
        raise ValueError("application payload SHA-256 is invalid")
    major, minor, patch, build = struct.unpack_from("<IIII", image, 48)
    slot_base = struct.unpack_from("<I", image, 68)[0]
    packed_version = ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | ((patch & 0xFF) << 8) | (build & 0xFF)
    return slot_base, packed_version, image[16:48]


def initial_metadata(app_version: int, app_hash: bytes) -> bytes:
    values = (
        METADATA_MAGIC,
        METADATA_VERSION,
        METADATA_SIZE,
        1,  # sequence
        0,  # active_slot: A
        0,  # confirmed_slot: A
        0xFF,  # pending_slot: none
        0xFF,  # update_slot: none
        3,  # slot_a_state: confirmed
        0,  # slot_b_state: empty
        0,  # boot_attempts
        3,  # max_boot_attempts
        app_version,
        0,
        app_hash,
        bytes(32),
        0,  # flags
        0,  # update_kind
        0,  # update_attempted
        0,  # update_result
        0,  # update_reason
        0,  # staged_app_size
        0,  # staged_app_version
        bytes(32),
        0,  # staged_app_addr
        0,  # staged_bundle_base
        0,  # staged_bundle_size
    )
    record = struct.pack(METADATA_FORMAT_WITHOUT_CRC, *values)
    return record + struct.pack("<I", zlib.crc32(record) & 0xFFFFFFFF)


def checked_end(base: int, size: int, name: str) -> int:
    end = base + size
    if end > 0x100000000:
        raise ValueError(f"{name} extends beyond the 32-bit address space")
    return end


def create_factory_image(
    bootloader: bytes,
    bootloader_base: int,
    app: bytes,
    app_base: int,
    metadata: bytes,
    metadata_base: int,
    max_output_size: int,
) -> bytes:
    segments = [
        (bootloader_base, bootloader, "bootloader"),
        (app_base, app, "application"),
        (metadata_base, metadata, "metadata"),
    ]
    for base, data, name in segments:
        checked_end(base, len(data), name)
        if base < bootloader_base:
            raise ValueError(f"{name} address must be at or above the bootloader base")
    ordered = sorted(segments)
    for (left_base, left_data, left_name), (right_base, _, right_name) in zip(ordered, ordered[1:]):
        if left_base + len(left_data) > right_base:
            raise ValueError(f"{left_name} and {right_name} address ranges overlap")

    output_end = max(base + len(data) for base, data, _ in segments)
    output_size = output_end - bootloader_base
    if output_size > max_output_size:
        raise ValueError(
            f"flat image would be {output_size} bytes (limit {max_output_size}); "
            "the regions are probably on different flash devices"
        )

    output = bytearray(b"\xff" * output_size)
    for base, data, _ in segments:
        offset = base - bootloader_base
        output[offset:offset + len(data)] = data
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Create one flat factory-flash binary containing LaunchCore and an application."
    )
    parser.add_argument("--bootloader", required=True, type=Path, help="Raw bootloader binary.")
    parser.add_argument("--bootloader-base", required=True, type=parse_u32)
    parser.add_argument("--app", required=True, type=Path, help="Packaged LaunchCore application image.")
    parser.add_argument(
        "--app-base",
        type=parse_u32,
        help="Expected application slot address. Defaults to slot_base_addr in the application header.",
    )
    parser.add_argument(
        "--metadata-base",
        required=True,
        type=parse_u32,
        help="Board metadata0 address; initialized to a confirmed Slot A record.",
    )
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--max-output-size",
        type=positive_size,
        default=DEFAULT_MAX_OUTPUT_SIZE,
        help="Safety limit for a flat output file (default: 64 MiB).",
    )
    args = parser.parse_args()

    bootloader = args.bootloader.read_bytes()
    if not bootloader:
        parser.error("--bootloader must not be empty")
    app = args.app.read_bytes()
    try:
        header_app_base, app_version, app_hash = application_info(app)
        app_base = header_app_base if args.app_base is None else args.app_base
        if app_base != header_app_base:
            raise ValueError(
                f"--app-base 0x{app_base:08x} does not match application header "
                f"slot_base_addr 0x{header_app_base:08x}"
            )
        output = create_factory_image(
            bootloader,
            args.bootloader_base,
            app,
            app_base,
            initial_metadata(app_version, app_hash),
            args.metadata_base,
            args.max_output_size,
        )
    except ValueError as error:
        parser.error(str(error))
    args.output.write_bytes(output)


if __name__ == "__main__":
    main()
