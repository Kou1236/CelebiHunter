"""Build and audit the two public CelebiHunter 3GX releases."""
from __future__ import annotations

import hashlib
import os
import pathlib
import re
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"


def toolchain_roots() -> list[pathlib.Path]:
    roots: list[pathlib.Path] = []
    if configured := os.environ.get("CELEBIHUNTER_TOOLCHAIN_ROOT"):
        roots.append(pathlib.Path(configured))
    roots.append(ROOT / ".toolchain")
    return [path for path in roots if path.is_dir()]


TOOLCHAIN_ROOTS = toolchain_roots()


def find(pattern: str, command: str | None = None) -> pathlib.Path:
    for base in TOOLCHAIN_ROOTS:
        matches = sorted(base.glob(pattern))
        if matches:
            return matches[0]
    if command and (resolved := shutil.which(command)):
        return pathlib.Path(resolved)
    raise SystemExit(f"Missing build dependency {command or pattern!r}. "
                     "Install it or set CELEBIHUNTER_TOOLCHAIN_ROOT.")


GCC = find("**/bin/arm-none-eabi-gcc.exe", "arm-none-eabi-gcc")
suffix = GCC.suffix
GXX = GCC.with_name("arm-none-eabi-g++" + suffix)
OBJDUMP = GCC.with_name("arm-none-eabi-objdump" + suffix)
THREEGX = find("**/3gxtool.exe", "3gxtool")
CARGO = find("**/cargo.exe", "cargo")


def find_libctru() -> pathlib.Path:
    if devkitpro := os.environ.get("DEVKITPRO"):
        path = pathlib.Path(devkitpro) / "libctru"
        if path.is_dir():
            return path
    for base in TOOLCHAIN_ROOTS:
        for pattern in ("libctru", "**/libctru"):
            for path in sorted(base.glob(pattern)):
                if (path / "include/3ds.h").is_file():
                    return path
    raise SystemExit("Missing libctru. Install devkitARM/libctru or set "
                     "DEVKITPRO/CELEBIHUNTER_TOOLCHAIN_ROOT.")


LIBCTRU = find_libctru()


def vendor_dir() -> pathlib.Path | None:
    candidates = []
    if configured := os.environ.get("CELEBIHUNTER_VENDOR_DIR"):
        candidates.append(pathlib.Path(configured))
    candidates.append(ROOT / "vendor")
    candidates.extend(path.parent / "vendor" for path in TOOLCHAIN_ROOTS)
    return next((path for path in candidates if path.is_dir()), None)


def run(args: list[object], *, env: dict[str, str] | None = None) -> None:
    command = [str(value) for value in args]
    print(" ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True, env=env)


def symbols(elf: pathlib.Path) -> str:
    return subprocess.check_output([str(OBJDUMP), "-t", str(elf)], text=True)


def disassembly(elf: pathlib.Path) -> str:
    return subprocess.check_output([str(OBJDUMP), "-d", str(elf)], text=True)


def symbol_body(elf: pathlib.Path, symbol: str) -> str:
    marker = f"<{symbol}>:"
    dump = disassembly(elf)
    if marker not in dump:
        raise SystemExit(f"Missing required symbol: {symbol}")
    return dump.split(marker, 1)[1].split("\n\n", 1)[0]


def resolve_symbol(elf: pathlib.Path, fragment: str) -> str:
    matches = []
    for line in symbols(elf).splitlines():
        fields = line.split()
        if fields and fragment in fields[-1]:
            matches.append(fields[-1])
    if len(matches) != 1:
        raise SystemExit(f"Expected one symbol containing {fragment!r}, found {matches}")
    return matches[0]


def verify_stack(elf: pathlib.Path, symbol: str, limit: int) -> None:
    reservation = 0
    for line in symbol_body(elf, symbol).splitlines()[:40]:
        if match := re.search(r"\bsub\s+sp,\s*sp,\s*#(0x[0-9a-f]+|[0-9]+)", line, re.I):
            reservation += int(match.group(1), 0)
    print(f"STACK {symbol} reserves {reservation} bytes (limit {limit})")
    if reservation > limit:
        raise SystemExit(f"Unsafe stack frame in {symbol}: {reservation} bytes")


def build_rust(mode: str, mode_build: pathlib.Path) -> pathlib.Path:
    target_dir = mode_build / "cargo"
    cargo_config = mode_build / "cargo-config.toml"
    cargo_args: list[object] = []
    if vendor := vendor_dir():
        cargo_config.write_text(
            "[source.crates-io]\nreplace-with = \"vendored-sources\"\n"
            f"[source.vendored-sources]\ndirectory = {str(vendor)!r}\n",
            encoding="utf-8",
        )
        cargo_args.extend(("--offline", "--config", cargo_config))

    env = os.environ.copy()
    for base in TOOLCHAIN_ROOTS:
        if (base / "cargo").is_dir() and CARGO.is_relative_to(base):
            env.setdefault("CARGO_HOME", str(base / "cargo"))
        if (base / "rustup").is_dir() and CARGO.is_relative_to(base):
            env.setdefault("RUSTUP_HOME", str(base / "rustup"))
    env["PATH"] = str(CARGO.parent) + os.pathsep + env.get("PATH", "")
    remaps = [f"--remap-path-prefix={ROOT}=/src"]
    remaps.extend(f"--remap-path-prefix={path.parent}=/deps" for path in TOOLCHAIN_ROOTS)
    env["RUSTFLAGS"] = " ".join(
        value for value in (env.get("RUSTFLAGS", ""), *remaps) if value
    )
    run([
        CARGO, "+nightly-2024-03-21-x86_64-pc-windows-gnu", "build",
        *cargo_args, "--release", "-Z", "build-std=core,alloc",
        "--target", "armv6k-nintendo-3ds", "--target-dir", target_dir,
        "--manifest-path", ROOT / "core/Cargo.toml", "--features", mode,
    ], env=env)
    archive = target_dir / "armv6k-nintendo-3ds/release/libcelebi_hunter.a"
    if not archive.is_file():
        raise SystemExit(f"Rust archive was not produced: {archive}")
    return archive


def compile_elf(mode_build: pathlib.Path, rust_lib: pathlib.Path) -> pathlib.Path:
    arch = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp", "-mfloat-abi=hard", "-mtp=soft", "-marm"]
    common = [
        *arch, "-Os", "-mword-relocations", "-fomit-frame-pointer",
        "-ffunction-sections", "-fdata-sections", "-fno-strict-aliasing",
        "-D__3DS__", "-DCELEBIHUNTER_MINIMAL_CTRU",
        f"-I{ROOT / '3gx/includes'}", f"-I{LIBCTRU / 'include'}",
    ]
    objects: list[pathlib.Path] = []
    for source in sorted((ROOT / "3gx/sources").iterdir()):
        if source.suffix.lower() not in {".c", ".s"}:
            continue
        obj = mode_build / f"{source.stem}.o"
        args: list[object] = [GCC, *common]
        if source.suffix.lower() == ".s":
            args.extend(("-x", "assembler-with-cpp"))
        args.extend(("-c", source, "-o", obj))
        run(args)
        objects.append(obj)

    svc_obj = mode_build / "libctru_svc.o"
    run([
        GCC, *arch, "-x", "assembler-with-cpp", f"-I{LIBCTRU / 'include'}",
        "-c", LIBCTRU / "source/svc.s", "-o", svc_obj,
    ])
    objects.append(svc_obj)

    elf = mode_build / "CelebiHunter.elf"
    run([
        GXX, "-nostartfiles", "-T", ROOT / "3gx/3gx.ld", *arch, "-Os",
        "-Wl,--gc-sections,--strip-discarded,--strip-debug,-z,noexecstack",
        *objects, rust_lib, "-o", elf,
    ])
    return elf


def verify_mode(elf: pathlib.Path, mode: str) -> None:
    expected = 1 if mode == "reset" else 3
    if not re.search(r"\bmov\s+r0,\s*#1\b", symbol_body(elf, "celebi_auto_enabled")):
        raise SystemExit("Automatic runtime marker is disabled")
    if not re.search(rf"\bmov\s+r0,\s*#{expected}\b", symbol_body(elf, "celebi_auto_mode")):
        raise SystemExit(f"Wrong runtime mode marker for {mode}")

    table = symbols(elf)
    data = elf.read_bytes()
    removed_crash_paths = (
        "capture_final_context", "HOST_FINAL_CONTEXT", "read_into",
        "apply_phase_barrier", "inject_guest_cycles",
    )
    if present := [value for value in removed_crash_paths if value in table]:
        raise SystemExit("Removed crash path linked: " + ", ".join(present))

    forbidden_common = (
        b"PokeReader", b"pokereader", b"Kou", b"58854", b".jsonl",
        b"Last DVs", b"RESOLVER SELECT", b"X/Y: change",
    )
    if present := [value for value in forbidden_common if value in data]:
        raise SystemExit(f"Forbidden release strings in {mode}: {present}")
    if "celebi_log_write" in table or "celebi_code_audit_capture" in table:
        raise SystemExit(f"Filesystem logger linked into {mode}")

    if mode == "reset":
        if present := [value for value in ("begin_full_phase_probe", "start_for_dvs") if value in table]:
            raise SystemExit("Reset links RNG resolver entry points: " + ", ".join(present))
        if "DivTracker" in table or b"GS text: waiting for DIV" in data:
            raise SystemExit("Reset links the retired DIV readiness gate")
    else:
        if not re.search(r"\bmov\s+r0,\s*#0\b", symbol_body(elf, "celebi_auto_host_buttons")):
            raise SystemExit("RNG can produce virtual controller input")
        if "begin_full_phase_probe" not in table:
            raise SystemExit("RNG is missing the fixed FAAA resolver")
        for value in (b"SHINY CELEBI FOUND", b"CELEBI AUTO STOPPED", b"Attempts ", b"R: continue"):
            if value in data:
                raise SystemExit(f"RNG links a hidden UI string: {value!r}")

    verify_stack(elf, "celebi_auto_service", 8192)
    verify_stack(elf, resolve_symbol(elf, "route_crystal"), 256)
    if mode == "rng":
        verify_stack(elf, resolve_symbol(elf, "begin_full_phase_probe"), 2048)


def build(mode: str) -> pathlib.Path:
    mode_build = BUILD / mode
    mode_build.mkdir(parents=True, exist_ok=True)
    rust_lib = build_rust(mode, mode_build)
    elf = compile_elf(mode_build, rust_lib)
    verify_mode(elf, mode)

    output = DIST / ("Reset.3gx" if mode == "reset" else "RNG.3gx")
    metadata = ROOT / "3gx" / ("Reset.plgInfo" if mode == "reset" else "RNG.plgInfo")
    DIST.mkdir(exist_ok=True)
    run([THREEGX, "-s", elf, metadata, output])
    data = output.read_bytes()
    if data[:8] != b"3GX$0002":
        raise SystemExit(f"Unexpected 3GX header: {output}")
    if any(value in data for value in (b"PokeReader", b"pokereader", b".jsonl", b"Kou", b"58854")):
        raise SystemExit(f"Final container branding/privacy audit failed: {output}")
    print(f"BUILT {output.name} ({len(data)} bytes)")
    print(f"SHA256 {hashlib.sha256(data).hexdigest().upper()}")
    return output


def main() -> None:
    choices = {arg for arg in sys.argv[1:] if arg in {"--reset", "--rng"}}
    if any(arg not in {"--reset", "--rng"} for arg in sys.argv[1:]):
        raise SystemExit("Usage: python tools/build.py [--reset] [--rng]")
    modes = [
        value
        for flag, value in (("--reset", "reset"), ("--rng", "rng"))
        if not choices or flag in choices
    ]
    outputs = [build(mode) for mode in modes]
    checksums = "".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest().upper()}  {path.name}\n"
        for path in outputs
    )
    (DIST / "SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    print(checksums, end="")


if __name__ == "__main__":
    main()
