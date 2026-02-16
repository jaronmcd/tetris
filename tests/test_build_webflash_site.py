from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


def _load_module():
    repo_root = Path(__file__).resolve().parents[1]
    mod_path = repo_root / "scripts" / "ci" / "build_webflash_site.py"
    spec = spec_from_file_location("build_webflash_site", mod_path)
    module = module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


def test_parse_offset_variants():
    m = _load_module()
    assert m._parse_offset(4096) == 4096
    assert m._parse_offset("0x1000") == 4096
    assert m._parse_offset("65536") == 65536
    assert m._parse_offset("") is None
    assert m._parse_offset("not-a-number") is None
    assert m._parse_offset(None) is None


def test_resolve_offsets_uses_idedata_when_present():
    m = _load_module()
    idedata = {
        "extra": {
            "flash_images": [
                {"offset": "0x0000", "path": "/tmp/bootloader.bin"},
                {"offset": "0x8000", "path": "/tmp/partitions.bin"},
                {"offset": "0xe000", "path": "/tmp/boot_app0.bin"},
            ],
            "application_offset": "0x10000",
        }
    }
    offsets = m._resolve_offsets(idedata)
    assert offsets["bootloader"] == 0x0000
    assert offsets["partitions"] == 0x8000
    assert offsets["boot_app0"] == 0xE000
    assert offsets["app"] == 0x10000


def test_resolve_offsets_falls_back_to_defaults():
    m = _load_module()
    offsets = m._resolve_offsets({})
    assert offsets == m.DEFAULT_OFFSETS


def test_infer_chip_family_prefers_defines_then_env_name():
    m = _load_module()
    assert m._infer_chip_family("esp32-s3-4mb", {"defines": ["ARDUINO_ESP32S3_DEV"]}) == "ESP32-S3"
    assert m._infer_chip_family("my-c3-env", {"defines": []}) == "ESP32-C3"
    assert m._infer_chip_family("plain-esp32", {"defines": []}) == "ESP32"


def test_collect_build_for_env_writes_env_scoped_manifest_paths(tmp_path):
    m = _load_module()
    repo_root = tmp_path / "repo"
    build_dir = repo_root / ".pio" / "build" / "esp32dev-4mb"
    out_dir = tmp_path / "site"
    build_dir.mkdir(parents=True, exist_ok=True)

    (build_dir / "firmware.bin").write_bytes(b"app")
    (build_dir / "bootloader.bin").write_bytes(b"boot")
    (build_dir / "partitions.bin").write_bytes(b"part")
    (build_dir / "idedata.json").write_text('{"defines":["ARDUINO_ESP32_DEV"]}', encoding="utf-8")

    original_search_boot_app0 = m._search_boot_app0
    m._search_boot_app0 = lambda _repo_root, _build_dir: None
    try:
        build = m._collect_build_for_env(repo_root, out_dir, "esp32dev-4mb", "auto")
    finally:
        m._search_boot_app0 = original_search_boot_app0

    assert build["chipFamily"] == "ESP32"
    assert build["parts"] == [
        {"path": "firmware/esp32dev-4mb/bootloader.bin", "offset": 0x1000},
        {"path": "firmware/esp32dev-4mb/partitions.bin", "offset": 0x8000},
        {"path": "firmware/esp32dev-4mb/firmware.bin", "offset": 0x10000},
    ]
