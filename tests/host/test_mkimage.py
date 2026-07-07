import pathlib
import subprocess
import sys


def test_mkimage_round_trip(tmp_path: pathlib.Path) -> None:
    app = tmp_path / "app.bin"
    img = tmp_path / "app.launchcore.img"
    app.write_bytes(b"\x00\x20\x00\x20\x05\x01\x00\x90payload")
    subprocess.check_call([
        sys.executable,
        "tools/mkimage.py",
        "--input",
        str(app),
        "--output",
        str(img),
        "--slot-base",
        "0x90000000",
        "--vector-table",
        "0x90000100",
        "--version",
        "1.2.3",
        "--xip",
    ])
    assert img.exists()
