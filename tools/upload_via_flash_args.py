import argparse
import os
import shlex
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser(
        description="Upload ESP-IDF images using the build's flash_args file."
    )
    parser.add_argument("--python", required=True)
    parser.add_argument("--esptool", required=True)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", required=True)
    parser.add_argument("--chip", default="esp32s3")
    return parser.parse_args()


def resolve_image_path(build_dir, relative_path):
    candidates = [
        os.path.join(build_dir, relative_path),
        os.path.join(build_dir, os.path.basename(relative_path)),
    ]

    basename = os.path.basename(relative_path)
    if basename == "partition-table.bin":
        candidates.append(os.path.join(build_dir, "partitions.bin"))
    elif basename in ("clock_codex.bin", "firmware.bin"):
        candidates.append(os.path.join(build_dir, "firmware.bin"))

    for candidate in candidates:
        normalized = os.path.normpath(candidate)
        if os.path.exists(normalized):
            return normalized

    raise FileNotFoundError(
        f"Could not resolve image path for '{relative_path}' under '{build_dir}'"
    )


def load_flash_args(build_dir):
    flash_args_path = os.path.join(build_dir, "flash_args")
    with open(flash_args_path, "r", encoding="utf-8") as flash_args_file:
        lines = [line.strip() for line in flash_args_file if line.strip()]

    if not lines:
        raise RuntimeError(f"flash_args is empty: {flash_args_path}")

    common_flags = shlex.split(lines[0], posix=False)
    image_args = []

    for line in lines[1:]:
        parts = shlex.split(line, posix=False)
        if len(parts) != 2:
            raise RuntimeError(f"Unsupported flash_args entry: {line}")

        address, relative_path = parts
        image_path = resolve_image_path(build_dir, relative_path)
        image_args.extend([address, image_path])

    return common_flags, image_args


def main():
    args = parse_args()
    common_flags, image_args = load_flash_args(args.build_dir)

    command = [
        args.python,
        args.esptool,
        "--chip",
        args.chip,
        "--port",
        args.port,
        "--baud",
        args.baud,
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "write_flash",
        *common_flags,
        *image_args,
    ]

    print("Running upload command:")
    print(" ".join(f'"{part}"' if " " in part else part for part in command))

    completed = subprocess.run(command, check=False)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
