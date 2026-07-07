#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path

FORMAT = "<IHHI8BII32s32sII"
SIZE = struct.calcsize(FORMAT)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    args = parser.parse_args()
    data = args.dump.read_bytes()[:SIZE]
    fields = struct.unpack(FORMAT, data)
    names = [
        "magic", "version", "size", "sequence", "active_slot", "confirmed_slot", "pending_slot", "update_slot",
        "slot_a_state", "slot_b_state", "boot_attempts", "max_boot_attempts", "slot_a_version", "slot_b_version",
        "slot_a_hash", "slot_b_hash", "flags", "metadata_crc32",
    ]
    for name, value in zip(names, fields):
        if isinstance(value, bytes):
            value = value.hex()
        print(f"{name}: {value}")
    print(f"crc_ok: {zlib.crc32(data[:SIZE - 4]) & 0xFFFFFFFF == fields[-1]}")


if __name__ == "__main__":
    main()
