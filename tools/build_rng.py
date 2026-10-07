"""Build the 34-module C RNG implementation and its Luma 3GX loader."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess

import package
import preflight
from toolchain import ARCH, ROOT, Toolchain

RNG = ROOT / "rng"
BUILD = ROOT / "build/rng"
DIST = ROOT / "dist"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory() -> tuple[dict, dict[str, str]]:
    manifest = json.loads((RNG / "build_sources.json").read_bytes())
    preflight.require(manifest["version"] == "1.3.0", "Wrong RNG source manifest version")
    runtime, loader = manifest["runtime_sources"], manifest["loader_sources"]
    preflight.require(len(runtime) == 34 and len(loader) == 3, "Expected 34 runtime and three loader sources")
    preflight.require(manifest["sources"] == runtime + loader and len(set(runtime + loader)) == 37,
                      "Source order/inventory is inconsistent")
    names = [*manifest["sources"], *manifest["headers"], manifest["linker_script"], *manifest["licenses"]]
    hashes = {}
    for name in names:
        path = (RNG / name).resolve()
        preflight.require(path.is_relative_to(RNG.resolve()) and path.is_file(), f"Missing/invalid RNG input: {name}")
        hashes[name] = sha(path)
    expected = manifest.get("source_sha256", {})
    preflight.require(set(expected) == set(names), "Source manifest hashes do not cover the complete build closure")
    preflight.require(hashes == expected, "RNG source/header inventory changed; update build_sources.json intentionally")
    hashes["build_sources.json"] = sha(RNG / "build_sources.json")
    return manifest, hashes


def symbols(output: str) -> dict[str, dict]:
    found = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+([a-zA-Z])\s+(\S+)$", line)
        if match:
            found[match[3]] = {"address": int(match[1], 16), "type": match[2]}
    return found


def build() -> Path:
    BUILD.mkdir(parents=True, exist_ok=True)
    report_path = BUILD / "build_report.json"
    commands, warnings = [], []
    report = {"status": "pending_rng_build", "commands": commands, "warnings": warnings}

    def save() -> None:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")

    def invoke(args: list[object]) -> str:
        command = [str(value) for value in args]
        commands.append(command)
        result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
        if result.stderr:
            warnings.append(result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout

    save()
    try:
        manifest, hashes = inventory()
        sdk = Toolchain()
        report.update(source_sha256=hashes, toolchain_sha256={str(path): sha(path) for path in
                      [sdk.gcc, sdk.nm, sdk.size, sdk.readelf, sdk.objdump, sdk.packager, *sdk.libraries]})
        includes = [f"-I{sdk.libctru / 'include'}"]
        for name in manifest["include_dirs"]:
            path = (RNG / name).resolve()
            preflight.require(path.is_relative_to(RNG.resolve()) and path.is_dir(), "Invalid include directory")
            includes.append(f"-I{path}")
        # Runtime flags and source order match the accepted device build.
        common = [*ARCH, "-Os", "-ffreestanding", "-Wall", "-Wextra", "-Werror", *includes]
        runtime_objects = []
        for index, name in enumerate(manifest["runtime_sources"]):
            source = RNG / name
            obj = BUILD / "objects" / f"{index:02d}_{source.parent.name}_{source.name}.o"
            obj.parent.mkdir(parents=True, exist_ok=True)
            formatting = ["-Wno-misleading-indentation"] if source.name in (
                "solver.c", "source_observer.c", "aligned_waiting.c") else []
            invoke([sdk.gcc, *common, *formatting, "-c", source, "-o", obj])
            runtime_objects.append(obj)
        undefined = invoke([sdk.nm, "-u", runtime_objects[0]])
        preflight.require(all(name not in undefined for name in (
            "provider", "lease", "inject", "virtual_keys", "rt_input_due")),
            "Coordinator links an unexpected virtual-input path")
        # Loader compile/link flags are retained from loader/build_link_check.py.
        loader_flags = ["-std=c11", *ARCH, "-Os", "-ffreestanding", "-fno-common",
                        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror", *includes]
        loader_objects = []
        for name in manifest["loader_sources"]:
            source = RNG / name
            obj = BUILD / "objects" / ("loader_" + source.name + ".o")
            invoke([sdk.gcc, *loader_flags, "-c", source, "-o", obj])
            loader_objects.append(obj)
        inputs = [*loader_objects, *runtime_objects]
        (BUILD / "input_undefined_symbols.txt").write_text(
            invoke([sdk.nm, "-A", "-u", *inputs]), encoding="utf8")
        elf = BUILD / "RNG.elf"
        mapfile = BUILD / "RNG.map"
        invoke([sdk.gcc, *loader_flags, "-nostartfiles", "-nostdlib",
                "-Wl,--gc-sections,--no-undefined", f"-Wl,-Map={mapfile}", "-Wl,--entry=_start",
                "-T", RNG / manifest["linker_script"], *inputs,
                "-Wl,--start-group", *sdk.libraries, "-Wl,--end-group", "-o", elf])
        final_undefined = invoke([sdk.nm, "-u", elf])
        preflight.require(not final_undefined.strip(), "ELF retains unresolved symbols: " + final_undefined)
        linked = symbols(invoke([sdk.nm, "-n", elf]))
        exports = {}
        for obj in runtime_objects:
            names = sorted(symbols(invoke([sdk.nm, "-g", "--defined-only", obj])))
            missing = [name for name in names if name not in linked]
            preflight.require(not missing, f"Runtime definitions discarded in {obj.name}: {', '.join(missing)}")
            exports[obj.name] = names
        structure = invoke([sdk.readelf, "-h", "-l", "-S", "-A", elf])
        structure += "\n" + invoke([sdk.size, "-A", elf])
        (BUILD / "elf_structure.txt").write_text(structure, encoding="utf8")
        (BUILD / "elf_disassembly.txt").write_text(invoke([sdk.objdump, "-d", elf]), encoding="utf8")
        admission = preflight.audit(elf)
        (BUILD / "preflight_report.json").write_text(json.dumps(admission, indent=2) + "\n", encoding="utf8")
        # Admit a stable local source closure; no research-output manifest is required.
        preflight.require(all(sha(RNG / name) == digest for name, digest in hashes.items()),
                          "RNG inputs changed during compilation")
        output = DIST / "RNG.3gx"
        packaged = package.build_and_verify(elf, preflight.METADATA, output, sdk.packager)
        preflight.require(all(sha(RNG / name) == digest for name, digest in hashes.items()),
                          "RNG inputs changed during packaging")
        (BUILD / "package_verification.json").write_text(json.dumps(packaged, indent=2) + "\n", encoding="utf8")
        report.update(status="passed_rng_build_and_container_verification", runtime_source_count=34,
                      loader_source_count=3, object_sha256={obj.name: sha(obj) for obj in inputs},
                      all_runtime_exported_definitions_retained=True, runtime_exported_definitions=exports,
                      ELF_sha256=sha(elf), map_sha256=sha(mapfile), package_sha256=sha(output),
                      package_bytes=output.stat().st_size, device_execution_performed=False)
        save()
        print(f"BUILT {output.name} ({output.stat().st_size} bytes)")
        print(f"SHA256 {sha(output).upper()}")
        return output
    except Exception as error:
        report.update(status="failed_rng_build", error=str(error))
        save()
        raise


if __name__ == "__main__":
    build()
