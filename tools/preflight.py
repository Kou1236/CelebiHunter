"""Self-contained ELF and metadata checks for the public C RNG artifact."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
ELF = ROOT / "build/rng/RNG.elf"
METADATA = ROOT / "rng/RNG.plgInfo"
REQUIRED = (
    "_start", "main", "raw_device_startup", "ch_loader_abort", "ch_raw_bridge",
    "ch_manual_alias_probe", "__sync_init", "__system_initSyscalls", "srvInit",
    "hidInit", "hidScanInput", "threadCreate", "svcMapProcessMemoryEx",
    "svcUnmapProcessMemoryEx", "svcConvertVAToPA", "svcFlushEntireDataCache",
    "svcInvalidateEntireInstructionCache", "__end__", "__tls_start", "__tls_end",
    "__tdata_align", "__tdata_lma", "__tdata_lma_end",
)
FORBIDDEN = (
    "celebi_auto_service", "celebi_inject_keys", "map_input_hook", "run_hook",
    "DRAW_PATCH", "HID_INPUT_MAP_PATCH", "__libctru_init", "__system_allocateHeaps",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metadata(path: Path) -> dict:
    text = path.read_text(encoding="utf8")
    numbers = []
    for key in ("Major", "Minor", "Revision"):
        match = re.search(r"^\s*" + key + r":\s*(\d+)\s*$", text, re.M)
        require(match is not None, f"Missing metadata {key}")
        numbers.append(int(match[1]))
    require(numbers == [1, 1, 0], "RNG metadata must be version 1.1.0")
    title = re.search(r"^Title:\s*(.+)$", text, re.M)
    require(title is not None and title[1].strip() == "CelebiHunter v1.1.0", "Wrong RNG metadata title")
    summary = re.search(r"^Summary:\s*(.+)$", text, re.M)
    require(summary is not None, "Missing metadata Summary")
    targets = re.search(r"^Targets:\s*\n\s*-\s*0x00172800\s*(?=\n)", text, re.M)
    require(targets is not None, "RNG metadata must target 0004000000172800")
    for key, value in (("Compatibility", "Console"), ("MemorySize", "5MiB"),
                       ("EventsSelfManaged", "true"), ("SwapNotNeeded", "true"),
                       ("UsePrivateMemory", "false")):
        require(re.search(r"^" + key + ":\\s*" + value + r"\s*$", text, re.M) is not None,
                f"Wrong metadata {key}")
    return {"version": "1.1.0", "encoded_version": 0x01010000,
            "title": title[1].strip(), "summary": summary[1].strip(), "sha256": sha(path)}


def read_elf(path: Path) -> tuple[bytes, list[dict], dict[str, dict], list[tuple]]:
    raw = path.read_bytes()
    require(len(raw) >= 52 and raw[:7] == b"\x7fELF\x01\x01\x01", "Not little-endian ELF32")
    eh = struct.unpack_from("<16sHHIIIIIHHHHHH", raw)
    require(eh[1:4] == (2, 40, 1), "RNG ELF must be an ARM executable")
    require(eh[4] == 0x07000100, "Wrong Luma entry address")
    require(eh[9] == 32 and eh[10] == 3, "RNG ELF must contain exactly three load segments")
    require(eh[5] + eh[9] * eh[10] <= len(raw), "Truncated ELF program headers")
    segments = []
    expected_address = eh[4]
    for index, flags in enumerate((5, 4, 6)):
        kind, offset, va, pa, filesz, memsz, actual_flags, alignment = struct.unpack_from(
            "<8I", raw, eh[5] + eh[9] * index)
        require(kind == 1 and actual_flags == flags, f"Wrong load segment {index} flags")
        require(va == expected_address and pa == va, "Noncontiguous loaded ELF segments")
        require(memsz > 0 and memsz % 4 == 0 and filesz % 4 == 0, "Unaligned segment size")
        require(offset + filesz <= len(raw) and filesz <= memsz, "Truncated ELF segment")
        require(flags == 6 or filesz == memsz, "Unexpected zero-fill outside writable data")
        require(alignment == 0x1000, "Wrong ELF load alignment")
        segments.append({"VA": va, "offset": offset, "flags": flags, "file_bytes": filesz,
                         "memory_bytes": memsz,
                         "content_sha256": hashlib.sha256(raw[offset:offset + filesz]).hexdigest()})
        expected_address += memsz
    require(eh[11] == 40 and eh[12] > 0 and eh[6] + 40 * eh[12] <= len(raw), "Missing ELF section table")
    sections = [struct.unpack_from("<10I", raw, eh[6] + 40 * i) for i in range(eh[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        require(section[9] == 16 and section[5] % 16 == 0, "Invalid ELF symbol table")
        require(section[6] < len(sections), "Invalid ELF symbol string table")
        strings = sections[section[6]]
        require(strings[4] + strings[5] <= len(raw) and section[4] + section[5] <= len(raw), "Truncated ELF symbols")
        names = raw[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name_offset, address, size, info, other, shndx = struct.unpack_from("<IIIBBH", raw, offset)
            require(name_offset < len(names), "Invalid ELF symbol name offset")
            end = names.find(b"\0", name_offset)
            require(end >= 0, "Unterminated ELF symbol name")
            name = names[name_offset:end].decode("utf8", errors="strict")
            if name and shndx and (info >> 4 or name not in symbols):
                symbols[name] = {"address": address, "size": size, "section": shndx, "binding": info >> 4}
    require(symbols, "ELF has no defined symbols")
    return raw, segments, symbols, sections


def audit(elf: Path, meta: Path = METADATA) -> dict:
    raw, segments, symbols, sections = read_elf(elf)
    require(all(name in symbols for name in REQUIRED),
            "Missing linked definitions: " + ", ".join(name for name in REQUIRED if name not in symbols))
    require(not any(name in symbols for name in FORBIDDEN),
            "Unexpected entry definitions: " + ", ".join(name for name in FORBIDDEN if name in symbols))
    require(symbols["_start"]["address"] == 0x07000100, "Wrong startup definition")
    alignment = symbols["__tdata_align"]
    section_index = alignment["section"]
    require(section_index < len(sections), "TLS alignment must be stored data, not an absolute symbol")
    section = sections[section_index]
    require(section[1] == 1 and section[2] & 2 and not section[2] & (1 | 4),
            "TLS alignment must be a mapped read-only object")
    rodata = segments[1]
    address = alignment["address"]
    require(rodata["VA"] <= address and address + 4 <= rodata["VA"] + rodata["file_bytes"],
            "TLS alignment word is not initialized within read-only load data")
    value = struct.unpack_from("<I", raw, rodata["offset"] + address - rodata["VA"])[0]
    require(value >= 8 and value & (value - 1) == 0, "Invalid stored TLS alignment")
    require((symbols["__tls_start"]["address"] - 8) & (value - 1) == 0, "ARM TLS header is unaligned")
    bridge = symbols["ch_raw_bridge"]["address"]
    probe = symbols["ch_manual_alias_probe"]["address"]
    require(bridge & 4095 == 0 and (bridge & ~4095) == (probe & ~4095),
            "Raw alias bridge and probe must share one aligned page")
    end = segments[-1]["VA"] + segments[-1]["memory_bytes"]
    require(symbols["__end__"]["address"] == end, "ELF extent differs from __end__")
    extent = end - 0x07000000
    executable_size = (extent + 0x1000) & ~0xfff  # Luma reserves an additional page.
    require(executable_size < 5 * 1024 * 1024, "ELF exceeds the 5MiB loader block")
    return {"status": "passed_rng_ELF_static_admission", "ELF": str(elf), "ELF_sha256": sha(elf),
            "metadata": str(meta), "metadata_sha256": metadata(meta)["sha256"],
            "entry": "0x07000100", "segments": segments, "loader_executable_size": executable_size,
            "heap_under_5MiB_block": 5 * 1024 * 1024 - executable_size,
            "tls_alignment_object": {"address": address, "value": value, "mapped_const_object": True},
            "alias_bridge_page": hex(bridge), "required_symbols": {name: symbols[name] for name in REQUIRED},
            "scope": "Static build verification; no device execution is performed."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=ELF)
    parser.add_argument("--metadata", type=Path, default=METADATA)
    parser.add_argument("--report", type=Path, default=ROOT / "build/rng/preflight_report.json")
    args = parser.parse_args()
    report = audit(args.elf.resolve(), args.metadata.resolve())
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"Verified RNG ELF: {args.elf}")


if __name__ == "__main__":
    main()
