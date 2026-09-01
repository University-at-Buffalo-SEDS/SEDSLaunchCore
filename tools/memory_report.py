#!/usr/bin/env python3
"""Print a labeled aggregate flash and RAM report for LaunchCore images."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess


def integer(value: str) -> int:
    return int(value, 0)


def percent(used: int, capacity: int) -> str:
    return f"{used * 100.0 / capacity:.2f}%"


def human_size(value: int) -> str:
    """Return exact bytes followed by an IEC-scaled representation."""
    exact = f"{value:,} B"
    if value < 1024:
        return exact
    scaled = float(value)
    unit = "KiB"
    scaled /= 1024.0
    if scaled >= 1024.0:
        scaled /= 1024.0
        unit = "MiB"
    return f"{exact} ({scaled:.2f} {unit})"


def ram_used_from_size(size_tool: Path, elf: Path) -> int:
    """Read Berkeley ``size`` output; initialized data plus BSS occupies RAM."""
    completed = subprocess.run(
        [str(size_tool), str(elf)],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [line.split() for line in completed.stdout.splitlines() if line.strip()]
    for index, fields in enumerate(lines):
        if fields[:3] == ["text", "data", "bss"] and index + 1 < len(lines):
            values = lines[index + 1]
            if len(values) >= 3:
                return int(values[1], 0) + int(values[2], 0)
    raise ValueError(f"could not parse RAM usage from {size_tool} output for {elf}")


def print_table(title: str, rows: tuple[tuple[str, int, int], ...]) -> None:
    print(f"\n{title}")
    print(f"{'Region':<18} {'Used':>25} {'Capacity':>25} {'Used %':>9}")
    print(f"{'-' * 18}  {'-' * 25}  {'-' * 25}  {'-' * 9}")
    for label, used, capacity in rows:
        print(
            f"{label:<18} {human_size(used):>25} "
            f"{human_size(capacity):>25} {percent(used, capacity):>9}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bootloader", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--bootloader-elf", type=Path, required=True)
    parser.add_argument("--firmware-elf", type=Path, required=True)
    parser.add_argument("--size-tool", type=Path, required=True)
    parser.add_argument("--bootloader-capacity", type=integer, required=True)
    parser.add_argument("--firmware-capacity", type=integer, required=True)
    parser.add_argument("--total-flash", type=integer, required=True)
    parser.add_argument("--ram-capacity", type=integer, required=True)
    args = parser.parse_args()

    bootloader_used = args.bootloader.stat().st_size
    firmware_used = args.firmware.stat().st_size
    total_used = bootloader_used + firmware_used
    flash_rows = (
        ("Bootloader flash", bootloader_used, args.bootloader_capacity),
        ("Firmware flash", firmware_used, args.firmware_capacity),
        ("Total flash", total_used, args.total_flash),
    )
    bootloader_ram = ram_used_from_size(args.size_tool, args.bootloader_elf)
    firmware_ram = ram_used_from_size(args.size_tool, args.firmware_elf)
    ram_rows = (
        ("Bootloader RAM", bootloader_ram, args.ram_capacity),
        ("Firmware RAM", firmware_ram, args.ram_capacity),
        ("Peak image RAM", max(bootloader_ram, firmware_ram), args.ram_capacity),
    )
    print_table("LaunchCore device flash usage", flash_rows)
    print_table("LaunchCore device RAM usage (images execute independently)", ram_rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
