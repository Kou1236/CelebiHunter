"""Package and verify RNG.3gx against its three loaded ELF segments."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess

import preflight
from toolchain import find_tool, roots

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "dist/RNG.3gx"
DEFAULT_CHECKSUM = bytes.fromhex(
    "c0402de90070a0e3046090e4067087e0010050e1fbffff1a0700a0e1c080bde800f020e3")


def verify(elf: Path, meta: Path, output: Path) -> dict:
    ready = preflight.audit(elf, meta)
    settings = preflight.metadata(meta)
    data = output.read_bytes()
    original = elf.read_bytes()
    require = preflight.require
    require(len(data) >= 0x94 and data[:8] == b"3GX$0002", "Invalid 3GX v2 header")
    version, reserved = struct.unpack_from("<2I", data, 8)
    require(version == settings["encoded_version"] and reserved == 0, "Wrong container version")
    infos = struct.unpack_from("<18I", data, 16)
    flags, checksum = infos[8:10]
    require(flags == 0xc3, "Wrong embedded, memory, console, event or swap flags")
    require(infos[10:] == (0,) * 8, "Unexpected reserved container fields")
    texts = []
    used_ranges = [(0, 0x94)]
    for size, offset in zip(infos[:8:2], infos[1:8:2]):
        require(size > 0 and offset >= 0x94 and offset + size <= len(data), "Invalid metadata range")
        content = data[offset:offset + size]
        require(content.endswith(b"\0") and b"\0" not in content[:-1], "Invalid metadata string")
        texts.append(content[:-1].decode("utf8"))
        used_ranges.append((offset, offset + size))
    require(texts[1] == settings["title"], "Wrong embedded title")
    require(texts[2] == settings["summary"], "Wrong embedded summary")
    exe = struct.unpack_from("<10I", data, 88)
    code_off, rodata_off, data_off = exe[:3]
    code_size, rodata_size, data_size, bss_size = exe[3:7]
    sizes = [segment["file_bytes"] for segment in ready["segments"]]
    expected_bss = ready["segments"][2]["memory_bytes"] - sizes[2]
    require((code_size, rodata_size, data_size, bss_size) == (*sizes, expected_bss),
            "Container loaded sizes differ from ELF")
    require(code_off % 16 == 0 and rodata_off == code_off + code_size
            and data_off == rodata_off + rodata_size, "Invalid container load layout")
    contents = []
    for index, (offset, size, segment) in enumerate(zip(exe[:3], exe[3:6], ready["segments"])):
        require(offset >= 0x94 and offset + size <= len(data), "Invalid loaded segment range")
        content = data[offset:offset + size]
        expected = original[segment["offset"]:segment["offset"] + segment["file_bytes"]]
        require(content == expected, f"Loaded segment {index} differs from ELF")
        contents.append(content)
        used_ranges.append((offset, offset + size))
    initialized = b"".join(contents)
    calculated = sum(struct.unpack(f"<{len(initialized) // 4}I", initialized)) & 0xffffffff
    require(checksum == calculated, "Wrong loaded-data checksum")
    for offset in exe[7:10]:
        require(offset >= 0x94 and offset + len(DEFAULT_CHECKSUM) <= len(data), "Invalid checksum payload range")
        require(data[offset:offset + len(DEFAULT_CHECKSUM)] == DEFAULT_CHECKSUM, "Unexpected checksum/swap payload")
        used_ranges.append((offset, offset + len(DEFAULT_CHECKSUM)))
    count, titles_offset = struct.unpack_from("<2I", data, 128)
    require(count == 1 and titles_offset >= 0x94 and titles_offset + 4 <= len(data), "Wrong target count/range")
    require(struct.unpack_from("<I", data, titles_offset)[0] == 0x00172800, "Wrong target Title ID")
    used_ranges.append((titles_offset, titles_offset + 4))
    symbol_count, symbols_offset, names_offset = struct.unpack_from("<3I", data, 136)
    require(symbol_count > 0 and symbols_offset >= data_off + data_size, "Missing container symbols")
    require(symbols_offset + symbol_count * 12 <= len(data)
            and names_offset == symbols_offset + symbol_count * 12 and names_offset < len(data),
            "Invalid container symbol table")
    used_ranges.extend([(symbols_offset, names_offset), (names_offset, len(data))])
    for index in range(symbol_count):
        address, size, symflags, name = struct.unpack_from("<IHHI", data, symbols_offset + index * 12)
        require(names_offset + name < len(data) and data.find(b"\0", names_offset + name) >= 0,
                "Invalid container symbol name")
    ordered = sorted(used_ranges)
    require(all(previous[1] <= current[0] for previous, current in zip(ordered, ordered[1:])),
            "Overlapping container ranges")
    mapped_extent = 0x100 + code_size + rodata_size + data_size + bss_size
    executable_size = (mapped_extent + 0x1000) & ~0xfff
    require(executable_size == ready["loader_executable_size"], "Container mapped extent differs from ELF")
    require(preflight.sha(elf) == ready["ELF_sha256"] and preflight.sha(meta) == settings["sha256"],
            "ELF or metadata changed during package verification")
    require(not any(value in data for value in (b"PokeReader", b"pokereader", b".jsonl", b"Kou", b"58854")),
            "Final container branding/privacy audit failed")
    return {"status": "passed_rng_3GX_container_verification", "package": str(output),
            "package_sha256": preflight.sha(output), "package_bytes": len(data),
            "ELF": str(elf), "ELF_sha256": ready["ELF_sha256"], "metadata_sha256": settings["sha256"],
            "version": settings["version"], "title": settings["title"], "flags": hex(flags),
            "target": "0004000000172800", "three_loaded_segments_equal_ELF": True,
            "default_checksum": hex(checksum), "checksum_and_swap_payloads_match_default": True,
            "BSS_bytes": bss_size, "loader_entry": "0x07000100",
            "loader_executable_bytes": executable_size, "symbol_count": symbol_count,
            "metadata_text": texts, "device_execution_performed": False}


def build_and_verify(elf: Path, meta: Path, output: Path, tool: Path) -> dict:
    ready = preflight.audit(elf, meta)
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(tool), "-s", str(elf), str(meta), str(output)]
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    log = ROOT / "build/rng/packager.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(result.stdout + result.stderr, encoding="utf8")
    preflight.require(result.returncode == 0, f"3gxtool failed ({result.returncode}): {result.stderr}")
    preflight.require(preflight.sha(elf) == ready["ELF_sha256"]
                      and preflight.sha(meta) == ready["metadata_sha256"], "Packaging inputs changed")
    report = verify(elf, meta, output)
    report.update(packager=str(tool), packager_sha256=preflight.sha(tool), command=command)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--elf", type=Path, default=preflight.ELF)
    parser.add_argument("--metadata", type=Path, default=preflight.METADATA)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--packager", type=Path)
    parser.add_argument("--report", type=Path, default=ROOT / "build/rng/package_verification.json")
    args = parser.parse_args()
    elf, meta, output = (path.resolve() for path in (args.elf, args.metadata, args.output))
    if args.verify_only:
        report = verify(elf, meta, output)
    else:
        tool = args.packager.resolve() if args.packager else find_tool("3gxtool", roots())
        report = build_and_verify(elf, meta, output, tool)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"Verified {output.name}: {report['package_sha256'].upper()}")


if __name__ == "__main__":
    main()
