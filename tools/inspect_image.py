#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

HEADER_FORMAT = "<IHHII32sIIIIIIIIIIII64sI"
HEADER_STRUCT_SIZE = struct.calcsize(HEADER_FORMAT)
HEADER_CRC_OFFSET = HEADER_STRUCT_SIZE - 4


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    data = args.image.read_bytes()
    fields = struct.unpack_from(HEADER_FORMAT, data)
    payload = data[fields[2]:fields[2] + fields[3]]
    names = [
        "magic", "header_version", "header_size", "image_size", "image_crc32", "image_sha256",
        "version_major", "version_minor", "version_patch", "version_build", "image_flags",
        "slot_base_addr", "vector_table_addr", "load_addr", "entry_addr", "min_bootloader_version",
        "reserved0", "reserved1", "signature", "header_crc32",
    ]
    info = dict(zip(names, fields))
    for key in names:
        if key in {"image_sha256", "signature"}:
            value = info[key].hex()
        elif isinstance(info[key], int):
            value = f"0x{info[key]:08X}" if key.endswith("addr") or key in {"magic", "image_crc32", "header_crc32"} else str(info[key])
        else:
            value = info[key]
        print(f"{key}: {value}")
    print(f"header_crc_ok: {zlib.crc32(data[:HEADER_CRC_OFFSET]) & 0xFFFFFFFF == info['header_crc32']}")
    print(f"image_crc_ok: {zlib.crc32(payload) & 0xFFFFFFFF == info['image_crc32']}")
    print(f"image_sha256_ok: {hashlib.sha256(payload).digest() == info['image_sha256']}")


if __name__ == "__main__":
    main()
