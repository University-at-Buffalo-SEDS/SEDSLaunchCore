#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    subprocess.check_call([sys.executable, str(root / "build.py"), "bsp-templates"])


if __name__ == "__main__":
    main()
