#!/usr/bin/env python3
"""Print a labeled aggregate flash report for a LaunchCore device image."""

from __future__ import annotations

import argparse
from pathlib import Path


def integer(value: str) -> int:
    return int(value, 0)


def percent(used: int, capacity: int) -> str:
    return f"{used * 100.0 / capacity:.2f}%"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--bootloader-capacity", type=integer, required=True)
    parser.add_argument("--firmware-capacity", type=integer, required=True)
    parser.add_argument("--total-flash", type=integer, required=True)
    args = parser.parse_args()

    bootloader_used = args.bootloader.stat().st_size
    firmware_used = args.firmware.stat().st_size
    total_used = bootloader_used + firmware_used
    rows = (
        ("Bootloader", bootloader_used, args.bootloader_capacity),
        ("Firmware", firmware_used, args.firmware_capacity),
        ("Total linked", total_used, args.total_flash),
    )
    print("\nLaunchCore device flash usage")
    print(f"{'Region':<14} {'Used':>12} {'Capacity':>12} {'Used %':>9}")
    for label, used, capacity in rows:
        print(f"{label:<14} {used:>9} B {capacity:>9} B {percent(used, capacity):>9}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
