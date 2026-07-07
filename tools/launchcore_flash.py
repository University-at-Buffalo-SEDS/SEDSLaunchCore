#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Placeholder flashing helper for board bring-up.")
    parser.add_argument("image", type=Path)
    parser.add_argument("--slot", choices=["A", "B"], required=True)
    args = parser.parse_args()
    print(f"ready to flash {args.image} to slot {args.slot}; connect board-specific transport here")


if __name__ == "__main__":
    main()
