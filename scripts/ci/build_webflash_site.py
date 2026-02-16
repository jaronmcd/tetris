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
from typing import Any, Optional


DEFAULT_OFFSETS = {
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


def _parse_offset(value: Any) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        s = value.strip().lower()
        if not s:
            return None
        try:
            return int(s, 16 if s.startswith("0x") else 10)
        except ValueError:
            return None
    return None


def _load_idedata(build_dir: Path) -> dict[str, Any]:
    idedata_path = build_dir / "idedata.json"
    if not idedata_path.exists():
        return {}
    try:
        return json.loads(idedata_path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def _resolve_offsets(idedata: dict[str, Any]) -> dict[str, int]:
    offsets = dict(DEFAULT_OFFSETS)

    extra = idedata.get("extra", {})
    flash_images = extra.get("flash_images", [])
    if isinstance(flash_images, list):
        for entry in flash_images:
            if not isinstance(entry, dict):
                continue
            off = _parse_offset(entry.get("offset"))
            if off is None:
                continue
            name = Path(str(entry.get("path", ""))).name.lower()
            if "bootloader" in name:
                offsets["bootloader"] = off
            elif "partition" in name:
                offsets["partitions"] = off
            elif "boot_app0" in name:
                offsets["boot_app0"] = off

    app_off = _parse_offset(extra.get("application_offset"))
    if app_off is not None:
        offsets["app"] = app_off

    return offsets


def _infer_chip_family(env_name: str, idedata: dict[str, Any]) -> str:
    # Prefer explicit compile defines from idedata when available.
    defines = idedata.get("defines", [])
    if isinstance(defines, list):
        joined = " ".join(str(d).upper() for d in defines)
        if "ESP32S3" in joined:
            return "ESP32-S3"
        if "ESP32S2" in joined:
            return "ESP32-S2"
        if "ESP32C3" in joined:
            return "ESP32-C3"
        if "ESP32C6" in joined:
            return "ESP32-C6"
        if "ESP32H2" in joined:
            return "ESP32-H2"
        if "ESP32" in joined:
            return "ESP32"

    # Fallback to env name conventions.
    env = env_name.lower()
    if "s3" in env:
        return "ESP32-S3"
    if "s2" in env:
        return "ESP32-S2"
    if "c3" in env:
        return "ESP32-C3"
    if "c6" in env:
        return "ESP32-C6"
    if "h2" in env:
        return "ESP32-H2"
    return "ESP32"


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


def _collect_build_for_env(repo_root: Path, out_dir: Path, env_name: str, chip_family_arg: str) -> dict[str, Any]:
    build_dir = repo_root / ".pio" / "build" / env_name

    if not build_dir.exists():
        raise SystemExit(f"Build directory not found: {build_dir} (did you run: pio run -e {env_name}?)")

    idedata = _load_idedata(build_dir)
    offsets = _resolve_offsets(idedata)

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
        raise SystemExit(f"Could not find application binary for env '{env_name}' (expected firmware.bin)")

    bootloader_bin = _glob_first(build_dir, ["bootloader*.bin", "**/bootloader*.bin"])
    partitions_bin = _find_first([
        build_dir / "partitions.bin",
        build_dir / "partition-table.bin",
        build_dir / "partitions.bin",
    ]) or _glob_first(build_dir, ["**/partitions.bin", "**/partition-table.bin"])

    boot_app0_bin = _search_boot_app0(repo_root, build_dir)

    # Keep each environment's files in its own folder to avoid collisions.
    fw_env_dir = out_dir / "firmware" / env_name
    fw_env_dir.mkdir(parents=True, exist_ok=True)

    copied_parts = []

    def copy_part(src: Optional[Path], dest_name: str, offset: int) -> None:
        if not src:
            return
        dest = fw_env_dir / dest_name
        shutil.copy2(src, dest)
        copied_parts.append({"path": f"firmware/{env_name}/{dest_name}", "offset": offset})

    copy_part(bootloader_bin, "bootloader.bin", offsets["bootloader"])
    copy_part(partitions_bin, "partitions.bin", offsets["partitions"])
    copy_part(boot_app0_bin, "boot_app0.bin", offsets["boot_app0"])
    copy_part(firmware_bin, "firmware.bin", offsets["app"])

    if not copied_parts:
        raise SystemExit(f"No firmware parts were copied for env '{env_name}'")

    chip_family = _infer_chip_family(env_name, idedata) if chip_family_arg.lower() == "auto" else chip_family_arg
    print(f"env: {env_name}")
    print(f"  parts: {[p['path'] for p in copied_parts]}")
    print(f"  offsets: {offsets}")
    print(f"  chipFamily: {chip_family}")

    return {
        "chipFamily": chip_family,
        "parts": copied_parts,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--env",
        action="append",
        dest="envs",
        default=None,
        help="PlatformIO environment name. Repeat to include multiple builds.",
    )
    parser.add_argument("--out", default="site", help="Output directory for the static site")
    parser.add_argument(
        "--chip-family",
        default="auto",
        help='ESP Web Tools chip family (e.g. "ESP32-S3"). Default: auto-detect',
    )
    parser.add_argument(
        "--version",
        default=None,
        help="Version string to embed in manifest (defaults to git describe)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    envs = args.envs or ["esp32dev-4mb"]

    # Prepare site output.
    out_dir = Path(args.out).resolve()
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # Copy static UI assets.
    web_src = repo_root / "webflash"
    if not web_src.exists():
        raise SystemExit("Missing webflash/ directory")
    shutil.copytree(web_src, out_dir, dirs_exist_ok=True)

    builds = [_collect_build_for_env(repo_root, out_dir, env_name, args.chip_family) for env_name in envs]

    version = args.version or _git_describe() or "dev"

    manifest = {
        "name": "ESP32 NeoPixel Tetris",
        "version": version,
        "builds": builds,
    }

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    # GitHub Pages: avoid Jekyll processing.
    (out_dir / ".nojekyll").write_text("", encoding="utf-8")

    print(f"Built webflash site: {out_dir}")
    print(f"  envs: {envs}")
    print(f"  version: {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
