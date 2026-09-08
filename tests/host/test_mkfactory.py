import pathlib
import subprocess
import struct
import sys
import zlib


def make_app(tmp_path: pathlib.Path, slot_base: str = "0x08010000") -> pathlib.Path:
    payload = tmp_path / "app.bin"
    image = tmp_path / "app.launchcore.img"
    payload.write_bytes(b"\x00\x20\x00\x20\x01\x01\x01\x08payload")
    subprocess.check_call([
        sys.executable,
        "tools/mkimage.py",
        "--input",
        str(payload),
        "--output",
        str(image),
        "--slot-base",
        slot_base,
        "--vector-table",
        hex(int(slot_base, 0) + 0x200),
        "--version",
        "1.0.0",
        "--xip",
    ])
    return image


def test_mkfactory_places_both_images_and_erased_gap(tmp_path: pathlib.Path) -> None:
    bootloader = tmp_path / "bootloader.bin"
    output = tmp_path / "factory.bin"
    bootloader.write_bytes(b"bootloader")
    app = make_app(tmp_path)

    subprocess.check_call([
        sys.executable,
        "build.py",
        "factory-image",
        "--bootloader",
        str(bootloader),
        "--bootloader-base",
        "0x08000000",
        "--app",
        str(app),
        "--metadata-base",
        "0x08070000",
        "--output",
        str(output),
    ])

    data = output.read_bytes()
    assert data[:len(b"bootloader")] == b"bootloader"
    assert data[len(b"bootloader"):0x10000] == b"\xff" * (0x10000 - len(b"bootloader"))
    app_data = app.read_bytes()
    assert data[0x10000:0x10000 + len(app_data)] == app_data
    metadata = data[0x70000:]
    assert metadata[:4] == b"DMCL"
    assert struct.unpack_from("<H", metadata, 4)[0] == 2
    assert struct.unpack_from("<H", metadata, 6)[0] == len(metadata)
    assert metadata[13] == 0  # confirmed_slot: A
    assert metadata[16] == 3  # slot_a_state: confirmed
    assert zlib.crc32(metadata[:-4]) & 0xFFFFFFFF == struct.unpack_from("<I", metadata, len(metadata) - 4)[0]


def test_mkfactory_rejects_sparse_external_flash_layout(tmp_path: pathlib.Path) -> None:
    bootloader = tmp_path / "bootloader.bin"
    output = tmp_path / "factory.bin"
    bootloader.write_bytes(b"bootloader")
    app = make_app(tmp_path, "0x90000000")

    result = subprocess.run([
        sys.executable,
        "tools/mkfactory.py",
        "--bootloader",
        str(bootloader),
        "--bootloader-base",
        "0x08000000",
        "--app",
        str(app),
        "--metadata-base",
        "0x08070000",
        "--output",
        str(output),
    ], capture_output=True, text=True)

    assert result.returncode != 0
    assert "probably on different flash devices" in result.stderr
    assert not output.exists()
