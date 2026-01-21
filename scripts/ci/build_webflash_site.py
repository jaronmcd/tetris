#!/usr/bin/env python3
"""Build a static ESP Web Tools site for one-click flashing.

This script is intended for CI usage (GitHub Actions), but can also be run
locally after a PlatformIO build.

It:
- collects the binaries produced by PlatformIO
- copies a static web flasher UI
- generates a manifest.json compatible with ESP Web Tools

ESP Web Tools manifest example + offsets:
- https://github.com/esphome/esp-web-tools
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Optional


OFFSETS = {
    "bootloader": 0x1000,  # 4096
    "partitions": 0x8000,  # 32768
    "boot_app0": 0xE000,  # 57344
    "app": 0x10000,  # 65536
}


def _git_describe() -> Optional[str]:
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
        return out or None
    except Exception:
        return None


def _find_first(paths: list[Path]) -> Optional[Path]:
    for p in paths:
        if p and p.exists() and p.is_file():
            return p
    return None


def _glob_first(base: Path, patterns: list[str]) -> Optional[Path]:
    for pat in patterns:
        matches = sorted(base.glob(pat))
        for m in matches:
            if m.is_file():
                return m
    return None


def _search_boot_app0(repo_root: Path, build_dir: Path) -> Optional[Path]:
    # Common locations first.
    direct = _find_first([
        build_dir / "boot_app0.bin",
        build_dir / "boot_app0.bin",
    ])
    if direct:
        return direct

    # Then search PlatformIO packages (GitHub Actions uses ~/.platformio).
    candidates: list[Path] = []

    home = Path(os.path.expanduser("~"))
    candidates.extend(
        [
            home / ".platformio" / "packages",
            repo_root / ".platformio" / "packages",
        ]
    )

    for base in candidates:
        if not base.exists():
            continue
        found = _glob_first(base, ["**/boot_app0.bin"])
        if found:
            return found

    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", default="esp32-s3-4mb", help="PlatformIO environment name")
    parser.add_argument("--out", default="site", help="Output directory for the static site")
    parser.add_argument(
        "--version",
        default=None,
        help="Version string to embed in manifest (defaults to git describe)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    build_dir = repo_root / ".pio" / "build" / args.env

    if not build_dir.exists():
        raise SystemExit(f"Build directory not found: {build_dir} (did you run: pio run -e {args.env}?)")

    firmware_bin = _find_first(
        [
            build_dir / "firmware.bin",
            build_dir / "program.bin",
        ]
    )
    if not firmware_bin:
        # Fall back to any *.bin that looks like an app image.
        firmware_bin = _glob_first(build_dir, ["**/firmware.bin", "**/program.bin"])

    if not firmware_bin:
        raise SystemExit("Could not find application binary (expected firmware.bin)")

    bootloader_bin = _glob_first(build_dir, ["bootloader*.bin", "**/bootloader*.bin"])
    partitions_bin = _find_first([
        build_dir / "partitions.bin",
        build_dir / "partition-table.bin",
        build_dir / "partitions.bin",
    ]) or _glob_first(build_dir, ["**/partitions.bin", "**/partition-table.bin"])

    boot_app0_bin = _search_boot_app0(repo_root, build_dir)

    # Prepare site output.
    out_dir = Path(args.out).resolve()
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Copy static UI assets.
    web_src = repo_root / "webflash"
    if not web_src.exists():
        raise SystemExit("Missing webflash/ directory")

    shutil.copy2(web_src / "index.html", out_dir / "index.html")

    # Firmware files
    fw_dir = out_dir / "firmware"
    fw_dir.mkdir(parents=True, exist_ok=True)

    copied_parts = []

    def copy_part(src: Optional[Path], dest_name: str, offset: int) -> None:
        if not src:
            return
        dest = fw_dir / dest_name
        shutil.copy2(src, dest)
        copied_parts.append({"path": f"firmware/{dest_name}", "offset": offset})

    copy_part(bootloader_bin, "bootloader.bin", OFFSETS["bootloader"])
    copy_part(partitions_bin, "partitions.bin", OFFSETS["partitions"])
    copy_part(boot_app0_bin, "boot_app0.bin", OFFSETS["boot_app0"])
    copy_part(firmware_bin, "firmware.bin", OFFSETS["app"])

    if not copied_parts:
        raise SystemExit("No firmware parts were copied; refusing to generate an empty manifest")

    version = args.version or _git_describe() or "dev"

    manifest = {
        "name": "ESP32 NeoPixel Tetris",
        "version": version,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": copied_parts,
            }
        ],
    }

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # GitHub Pages: avoid Jekyll processing.
    (out_dir / ".nojekyll").write_text("", encoding="utf-8")

    print(f"Built webflash site: {out_dir}")
    print(f"  parts: {[p['path'] for p in copied_parts]}")
    print(f"  version: {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
