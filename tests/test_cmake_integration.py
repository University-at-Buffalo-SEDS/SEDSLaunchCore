#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT / "tools"))
import memory_report  # noqa: E402


class CMakeIntegrationTests(unittest.TestCase):
    def test_linkers_are_generated_from_board_config_with_named_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "board_config.h").write_text(
                """#pragma once
#define LAUNCHCORE_INTERNAL_FLASH_BASE 0x08000000u
#define LAUNCHCORE_INTERNAL_FLASH_SIZE (512u * 1024u)
#define LAUNCHCORE_INTERNAL_SRAM_BASE 0x20000000u
#define LAUNCHCORE_INTERNAL_SRAM_SIZE (112u * 1024u)
#define LAUNCHCORE_BOOTLOADER_SIZE (16u * 1024u)
#define TEST_SLOT_A_BASE 0x08004000u
#define TEST_SLOT_A_SIZE 0x00074000u
#define TEST_VECTOR_TABLE 0x08004200u
"""
            )
            (root / "CMakeLists.txt").write_text(
                f"""cmake_minimum_required(VERSION 3.20)
project(linker_test NONE)
include(\"{ROOT / 'cmake/launchcore_stm32.cmake'}\")
launchcore_generate_linker_scripts(
  BSP_CONFIG \"${{CMAKE_SOURCE_DIR}}/board_config.h\"
  LAYOUT_PREFIX TEST
  OUTPUT_DIRECTORY \"${{CMAKE_BINARY_DIR}}/generated\"
  OUT_BOOTLOADER_SCRIPT BOOT_SCRIPT
  OUT_APPLICATION_SCRIPT APP_SCRIPT)
"""
            )
            subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                check=True, capture_output=True, text=True,
            )
            boot = (root / "build/generated/launchcore_bootloader.ld").read_text()
            app = (root / "build/generated/launchcore_application.ld").read_text()
            self.assertIn("BOOTLOADER_FLASH (rx)  : ORIGIN = 0x8000000, LENGTH = 0x4000", boot)
            self.assertIn("BOOTLOADER_RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 0x1c000", boot)
            self.assertIn("FIRMWARE_FLASH (rx)  : ORIGIN = 0x8004200, LENGTH = 0x73e00", app)
            self.assertNotIn("@LC_", boot + app)

    def test_linker_generation_rejects_misaligned_vector_table(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "board_config.h").write_text(
                """#define LAUNCHCORE_INTERNAL_FLASH_BASE 0x08000000u
#define LAUNCHCORE_INTERNAL_FLASH_SIZE 0x80000u
#define LAUNCHCORE_INTERNAL_SRAM_BASE 0x20000000u
#define LAUNCHCORE_INTERNAL_SRAM_SIZE 0x1C000u
#define LAUNCHCORE_BOOTLOADER_SIZE 0x4000u
#define TEST_SLOT_A_BASE 0x08004000u
#define TEST_SLOT_A_SIZE 0x74000u
#define TEST_VECTOR_TABLE 0x08004100u
"""
            )
            (root / "CMakeLists.txt").write_text(
                f"""cmake_minimum_required(VERSION 3.20)
project(linker_test NONE)
include(\"{ROOT / 'cmake/launchcore_stm32.cmake'}\")
launchcore_generate_linker_scripts(BSP_CONFIG \"${{CMAKE_SOURCE_DIR}}/board_config.h\"
  LAYOUT_PREFIX TEST OUTPUT_DIRECTORY \"${{CMAKE_BINARY_DIR}}/generated\"
  OUT_BOOTLOADER_SCRIPT BOOT_SCRIPT OUT_APPLICATION_SCRIPT APP_SCRIPT)
"""
            )
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not aligned", result.stdout + result.stderr)

    def test_aggregate_report_has_requested_labels_and_total(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            boot = root / "boot.bin"
            firmware = root / "firmware.bin"
            boot.write_bytes(bytes(10_040))
            firmware.write_bytes(bytes(470_276))
            old_argv = sys.argv
            sys.argv = [
                "memory_report.py", "--bootloader", str(boot),
                "--firmware", str(firmware), "--bootloader-capacity", "0x4000",
                "--firmware-capacity", "0x73f00", "--total-flash", "0x80000",
            ]
            try:
                output = StringIO()
                with redirect_stdout(output):
                    self.assertEqual(memory_report.main(), 0)
            finally:
                sys.argv = old_argv
            report = output.getvalue()
            self.assertIn("Bootloader", report)
            self.assertIn("Firmware", report)
            self.assertIn("Total linked", report)
            self.assertIn("480316 B", report)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
