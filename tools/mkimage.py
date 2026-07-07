#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

IMAGE_MAGIC = 0x4C43494D
HEADER_VERSION = 1
HEADER_FORMAT_WITHOUT_CRC = "<IHHII32sIIIIIIIIIIII64s"
HEADER_STRUCT_SIZE = struct.calcsize(HEADER_FORMAT_WITHOUT_CRC) + 4
DEFAULT_HEADER_SIZE = 0x100
FLAG_XIP = 1 << 0
FLAG_COPY_RAM = 1 << 1


def parse_u32(text: str) -> int:
    return int(text, 0)


def parse_version(text: str) -> tuple[int, int, int, int]:
    parts = [int(p) for p in text.split(".")]
    if not 1 <= len(parts) <= 4:
        raise argparse.ArgumentTypeError("version must be major.minor.patch[.build]")
    return tuple((parts + [0, 0, 0, 0])[:4])


def header_without_crc(payload: bytes, args: argparse.Namespace) -> bytes:
    major, minor, patch, build = parse_version(args.version)
    flags = 0
    if args.xip:
        flags |= FLAG_XIP
    if args.copy_ram:
        flags |= FLAG_COPY_RAM
    entry = args.entry if args.entry is not None else struct.unpack_from("<I", payload, 4)[0]
    values = [
        IMAGE_MAGIC,
        HEADER_VERSION,
        args.header_size,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
        hashlib.sha256(payload).digest(),
        major,
        minor,
        patch,
        build,
        flags,
        args.slot_base,
        args.vector_table,
        args.load_addr,
        entry,
        args.min_bootloader_version,
        0,
        0,
        bytes(64),
    ]
    return struct.pack(HEADER_FORMAT_WITHOUT_CRC, *values)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--slot-base", required=True, type=parse_u32)
    parser.add_argument("--vector-table", required=True, type=parse_u32)
    parser.add_argument("--load-addr", type=parse_u32, default=0)
    parser.add_argument("--entry", type=parse_u32)
    parser.add_argument("--version", required=True)
    parser.add_argument("--header-size", type=parse_u32, default=DEFAULT_HEADER_SIZE)
    parser.add_argument("--min-bootloader-version", type=parse_u32, default=1)
    parser.add_argument("--xip", action="store_true")
    parser.add_argument("--copy-ram", action="store_true")
    args = parser.parse_args()
    if args.header_size < HEADER_STRUCT_SIZE:
        raise SystemExit(f"--header-size must be at least {HEADER_STRUCT_SIZE}")

    payload = args.input.read_bytes()
    if len(payload) < 8:
        raise SystemExit("input must contain at least an initial stack pointer and reset vector")
    hdr = header_without_crc(payload, args)
    header_crc = zlib.crc32(hdr) & 0xFFFFFFFF
    packed_header = hdr + struct.pack("<I", header_crc)
    padding = bytes(args.header_size - len(packed_header))
    args.output.write_bytes(packed_header + padding + payload)


if __name__ == "__main__":
    main()
