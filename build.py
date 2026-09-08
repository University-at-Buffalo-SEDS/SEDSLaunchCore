#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BSP_TEMPLATES = [
    "stm32g4_internal_basic",
    "stm32g4_external_basic",
    "stm32h5_internal_basic",
    "stm32h5_external_basic",
    "stm32h5_single_slot_external_stage",
    "stm32h7_internal_basic",
    "stm32h7_external_basic",
    "stm32u7_internal_basic",
    "stm32u7_external_basic",
]


def run(cmd: list[str], dry_run: bool = False) -> None:
    print("+ " + " ".join(cmd))
    if not dry_run:
        subprocess.check_call(cmd, cwd=ROOT)


def default_build_dir(name: str) -> Path:
    return ROOT / "build" / name


def cache_matches_source(build_dir: Path) -> bool:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return False
    prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            return Path(line.removeprefix(prefix)).resolve() == ROOT
    return False


def configure(args: argparse.Namespace, build_dir: Path) -> None:
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    cmd = ["cmake", "-B", str(build_dir), "-S", str(ROOT)]
    if args.board:
        cmd.append(f"-DLAUNCHCORE_BOARD={args.board}")
    if args.build_type:
        cmd.append(f"-DCMAKE_BUILD_TYPE={args.build_type}")
    if args.toolchain:
        cmd.append(f"-DCMAKE_TOOLCHAIN_FILE={args.toolchain}")
    for cmake_arg in args.cmake_arg:
        cmd.append(cmake_arg)
    run(cmd, args.dry_run)


def build_config(args: argparse.Namespace, build_dir: Path) -> None:
    cmd = ["cmake", "--build", str(build_dir)]
    if args.target:
        cmd.extend(["--target", args.target])
    if args.parallel:
        cmd.extend(["--parallel", str(args.parallel)])
    run(cmd, args.dry_run)


def command_build(args: argparse.Namespace) -> None:
    name = args.name or args.board or "host"
    build_dir = Path(args.build_dir) if args.build_dir else default_build_dir(name)
    configure(args, build_dir)
    build_config(args, build_dir)


def command_test(args: argparse.Namespace) -> None:
    build_dir = Path(args.build_dir) if args.build_dir else default_build_dir(args.name)
    if build_dir.exists() and not cache_matches_source(build_dir):
        print(f"Removing stale CMake build directory: {build_dir}")
        if not args.dry_run:
            shutil.rmtree(build_dir)
    if args.configure or not cache_matches_source(build_dir):
        configure(args, build_dir)
    # Tests may have been added or their executables removed since the last
    # configure. Always bring the requested test tree up to date before CTest.
    build_config(args, build_dir)

    cmd = ["ctest", "--test-dir", str(build_dir)]
    if args.output_on_failure:
        cmd.append("--output-on-failure")
    if args.regex:
        cmd.extend(["-R", args.regex])
    if args.parallel:
        cmd.extend(["--parallel", str(args.parallel)])
    run(cmd, args.dry_run)


def command_all(args: argparse.Namespace) -> None:
    build_args = argparse.Namespace(**vars(args))
    build_args.name = args.name
    command_build(build_args)

    test_args = argparse.Namespace(**vars(args))
    test_args.name = args.name
    test_args.configure = False
    test_args.output_on_failure = True
    test_args.regex = None
    command_test(test_args)


def command_bsp_templates(args: argparse.Namespace) -> None:
    for board in BSP_TEMPLATES:
        build_args = argparse.Namespace(**vars(args))
        build_args.board = board
        build_args.name = board
        build_args.build_dir = str(default_build_dir(board))
        command_build(build_args)


def command_memory_map(args: argparse.Namespace) -> None:
    args.name = args.name or f"{args.board}-memory-map"
    args.target = "launchcore_memory_map"
    command_build(args)
    build_dir = Path(args.build_dir) if args.build_dir else default_build_dir(args.name)
    run([str(build_dir / "launchcore_memory_map")], args.dry_run)


def flash_with_launchcore(args: argparse.Namespace) -> list[str]:
    return [
        sys.executable,
        str(ROOT / "tools" / "launchcore_flash.py"),
        str(args.image),
        "--slot",
        args.slot,
    ]


def flash_with_cube_programmer(args: argparse.Namespace) -> list[str]:
    connect = args.connect or "port=SWD"
    cmd = [args.cube_programmer, "-c", connect, "-w", str(args.image)]
    if args.address:
        cmd.append(args.address)
    if args.verify:
        cmd.append("-v")
    if args.reset:
        cmd.extend(["-rst"])
    if args.external_loader:
        cmd.extend(["--extload", args.external_loader])
    return cmd


def command_flash(args: argparse.Namespace) -> None:
    if args.tool == "launchcore":
        cmd = flash_with_launchcore(args)
    elif args.tool == "stm32cubeprogrammer":
        cmd = flash_with_cube_programmer(args)
    else:
        raise SystemExit(f"unsupported flash tool: {args.tool}")
    run(cmd, args.dry_run)


def command_factory_image(args: argparse.Namespace) -> None:
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "mkfactory.py"),
        "--bootloader",
        str(args.bootloader),
        "--bootloader-base",
        args.bootloader_base,
        "--app",
        str(args.app),
        "--metadata-base",
        args.metadata_base,
        "--output",
        str(args.output),
        "--max-output-size",
        args.max_output_size,
    ]
    if args.app_base:
        cmd.extend(["--app-base", args.app_base])
    run(cmd, args.dry_run)


def add_build_options(parser: argparse.ArgumentParser, board_required: bool = False) -> None:
    parser.add_argument("--board", required=board_required, help="Board port or built-in BSP template name.")
    parser.add_argument("--name", help="Build directory name under build/. Defaults to board or host.")
    parser.add_argument("--build-dir", help="Explicit build directory.")
    parser.add_argument("--build-type", default="Debug", help="CMAKE_BUILD_TYPE value.")
    parser.add_argument("--toolchain", help="CMake toolchain file.")
    parser.add_argument("--target", help="Specific CMake target to build.")
    parser.add_argument("-j", "--parallel", type=int, default=os.cpu_count(), help="Parallel build/test jobs.")
    parser.add_argument("--clean", action="store_true", help="Remove the build directory before configuring.")
    parser.add_argument("--cmake-arg", action="append", default=[], help="Extra argument passed to CMake configure. Repeatable.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without running them.")


def main() -> None:
    parser = argparse.ArgumentParser(description="SEDS LaunchCore build, test, and flash helper.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build", help="Configure and build one configuration.")
    add_build_options(build_parser)
    build_parser.set_defaults(func=command_build)

    test_parser = subparsers.add_parser("test", help="Run the CTest suite.")
    add_build_options(test_parser)
    test_parser.add_argument("--configure", action="store_true", help="Configure and build before running tests.")
    test_parser.add_argument("--output-on-failure", action="store_true", default=True, help="Show failing test output.")
    test_parser.add_argument("-R", "--regex", help="Only run tests matching this regex.")
    test_parser.set_defaults(func=command_test, name="host")

    all_parser = subparsers.add_parser("all", help="Configure, build, and run tests.")
    add_build_options(all_parser)
    all_parser.set_defaults(func=command_all, name="host")

    bsp_parser = subparsers.add_parser("bsp-templates", help="Build every built-in BSP template.")
    add_build_options(bsp_parser)
    bsp_parser.set_defaults(func=command_bsp_templates)

    map_parser = subparsers.add_parser("memory-map", help="Build and print a board's configured memory allocation.")
    add_build_options(map_parser, board_required=True)
    map_parser.set_defaults(func=command_memory_map)

    flash_parser = subparsers.add_parser("flash", help="Flash an image using a selected flashing backend.")
    flash_parser.add_argument("--tool", choices=["launchcore", "stm32cubeprogrammer"], default="launchcore")
    flash_parser.add_argument("--image", required=True, type=Path, help="Image file to flash.")
    flash_parser.add_argument("--slot", choices=["A", "B"], default="A", help="LaunchCore slot for recovery flashing.")
    flash_parser.add_argument("--address", help="Target address for raw programmer flashing, e.g. 0x90000000.")
    flash_parser.add_argument("--connect", help="STM32CubeProgrammer connection string, e.g. port=SWD.")
    flash_parser.add_argument("--cube-programmer", default="STM32_Programmer_CLI", help="STM32CubeProgrammer CLI executable.")
    flash_parser.add_argument("--external-loader", help="STM32 external loader path/name for external flash.")
    flash_parser.add_argument("--verify", action="store_true", help="Verify after flashing when supported.")
    flash_parser.add_argument("--reset", action="store_true", help="Reset after flashing when supported.")
    flash_parser.add_argument("--dry-run", action="store_true", help="Print commands without running them.")
    flash_parser.set_defaults(func=command_flash)

    factory_parser = subparsers.add_parser(
        "factory-image", help="Combine a raw bootloader and packaged application into one flat binary."
    )
    factory_parser.add_argument("--bootloader", required=True, type=Path, help="Raw bootloader binary.")
    factory_parser.add_argument("--bootloader-base", required=True, help="Bootloader load address, e.g. 0x08000000.")
    factory_parser.add_argument("--app", required=True, type=Path, help="Packaged LaunchCore application image.")
    factory_parser.add_argument("--app-base", help="Verify the slot address stored in the app header.")
    factory_parser.add_argument("--metadata-base", required=True, help="Board metadata0 address.")
    factory_parser.add_argument("--output", required=True, type=Path, help="Combined flat binary output.")
    factory_parser.add_argument("--max-output-size", default="0x4000000", help="Maximum flat output size.")
    factory_parser.add_argument("--dry-run", action="store_true", help="Print commands without running them.")
    factory_parser.set_defaults(func=command_factory_image)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
