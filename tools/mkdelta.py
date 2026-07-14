#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

MAGIC = 0x4C434450
VERSION = 1
HEADER_NO_CRC = "<IHHIIIIII32s32s"
RECORD_NO_CRC = "<IIIIIIBBH"
HEADER_SIZE = struct.calcsize(HEADER_NO_CRC) + 4
RECORD_SIZE = struct.calcsize(RECORD_NO_CRC) + 4
CODEC_RAW = 0
CODEC_RLE = 1
IMAGE_MAGIC = 0x4C43494D


def number(value: str) -> int:
    return int(value, 0)


def rle_encode(data: bytes) -> bytes:
    encoded = bytearray()
    literal = bytearray()

    def flush_literal() -> None:
        while literal:
            count = min(len(literal), 128)
            encoded.append(count - 1)
            encoded.extend(literal[:count])
            del literal[:count]

    offset = 0
    while offset < len(data):
        run = 1
        while offset + run < len(data) and data[offset + run] == data[offset] and run < 130:
            run += 1
        if run >= 3:
            flush_literal()
            encoded.extend((0x80 | (run - 3), data[offset]))
            offset += run
        else:
            literal.append(data[offset])
            offset += 1
            if len(literal) == 128:
                flush_literal()
    flush_literal()
    return bytes(encoded)


def compress(data: bytes) -> tuple[int, bytes]:
    encoded = rle_encode(data)
    return (CODEC_RLE, encoded) if len(encoded) < len(data) else (CODEC_RAW, data)


def image_slot(image: bytes, name: str) -> int:
    if len(image) < 72:
        raise ValueError(f"{name} is too small to be a packaged image")
    magic, _, header_size, payload_size = struct.unpack_from("<IHHI", image)
    if magic != IMAGE_MAGIC or header_size > len(image) or payload_size != len(image) - header_size:
        raise ValueError(f"{name} is not a complete packaged LaunchCore image")
    return struct.unpack_from("<I", image, 68)[0]


def main() -> None:
    parser = argparse.ArgumentParser(description="Create a reversible, block-compressed LaunchCore delta.")
    parser.add_argument("--base", required=True, type=Path, help="Currently installed packaged image.")
    parser.add_argument("--target", required=True, type=Path, help="New packaged image.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--erase-size", required=True, type=number)
    parser.add_argument("--slot-size", required=True, type=number, help="Size of executable Slot A.")
    parser.add_argument("--delta-slot-size", type=number, help="Reject a patch that cannot fit Slot B.")
    parser.add_argument("--force", action="store_true", help="Write a delta even when a full image is smaller.")
    args = parser.parse_args()

    base = args.base.read_bytes()
    target = args.target.read_bytes()
    try:
        base_slot = image_slot(base, "base")
        target_slot = image_slot(target, "target")
    except ValueError as error:
        parser.error(str(error))
    if base_slot != target_slot:
        parser.error("base and target must be linked for the same executable slot")
    if args.erase_size <= 0 or args.slot_size <= 0 or args.slot_size % args.erase_size:
        parser.error("slot size must be a positive multiple of erase size")
    if max(len(base), len(target)) > args.slot_size:
        parser.error("base or target image exceeds Slot A")

    records = bytearray()
    changed = 0
    for offset in range(0, args.slot_size, args.erase_size):
        old = base[offset:offset + args.erase_size].ljust(args.erase_size, b"\xff")
        new = target[offset:offset + args.erase_size].ljust(args.erase_size, b"\xff")
        if old == new:
            continue
        old_codec, old_data = compress(old)
        new_codec, new_data = compress(new)
        values = (offset, args.erase_size, len(old_data), len(new_data),
                  zlib.crc32(old) & 0xFFFFFFFF, zlib.crc32(new) & 0xFFFFFFFF,
                  old_codec, new_codec, 0)
        record = struct.pack(RECORD_NO_CRC, *values)
        records.extend(record)
        records.extend(struct.pack("<I", zlib.crc32(record) & 0xFFFFFFFF))
        records.extend(old_data)
        records.extend(new_data)
        changed += 1

    if changed == 0:
        parser.error("base and target images are identical")
    total_size = HEADER_SIZE + len(records)
    values = (MAGIC, VERSION, HEADER_SIZE, total_size, args.erase_size, len(base), len(target),
              changed, HEADER_SIZE, hashlib.sha256(base).digest(), hashlib.sha256(target).digest())
    header = struct.pack(HEADER_NO_CRC, *values)
    patch = header + struct.pack("<I", zlib.crc32(header) & 0xFFFFFFFF) + records
    if args.delta_slot_size is not None and len(patch) > args.delta_slot_size:
        parser.error(f"delta is {len(patch)} bytes and exceeds the {args.delta_slot_size}-byte Slot B")
    full_deflate = len(zlib.compress(target, 9))
    if not args.force and len(patch) >= min(len(target), full_deflate):
        parser.error(f"delta is {len(patch)} bytes versus {len(target)} raw and an estimated "
                     f"{full_deflate} compressed full image; use the smaller full-image method")
    args.output.write_bytes(patch)
    print(f"delta={len(patch)} raw_full={len(target)} deflate_full_estimate={full_deflate} changed_blocks={changed}")


if __name__ == "__main__":
    main()
