"""Portable devkitARM discovery for the C RNG build (no Rust dependency)."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ARCH = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp", "-mfloat-abi=hard", "-marm"]


def roots() -> list[Path]:
    candidates = [Path(value).expanduser() for name in
                  ("DEVKITPRO", "CELEBIHUNTER_TOOLCHAIN_ROOT", "DEVKITARM")
                  if (value := os.environ.get(name))]
    candidates += [ROOT / ".toolchain", ROOT / "toolchain"]
    return list(dict.fromkeys(path.resolve() for path in candidates if path.is_dir()))


def find_tool(name: str, search_roots: list[Path]) -> Path:
    # Prefer explicitly configured SDKs; accept either Windows or Unix names.
    for base in search_roots:
        for relative in (Path("bin"), Path("devkitARM/bin"), Path("tools/bin"), Path(".")):
            for suffix in (".exe", "") if os.name == "nt" else ("", ".exe"):
                path = base / relative / (name + suffix)
                if path.is_file():
                    return path.resolve()
        for pattern in (f"**/bin/{name}.exe", f"**/bin/{name}"):
            matches = sorted(base.glob(pattern))
            if matches:
                return matches[0].resolve()
    if resolved := shutil.which(name):
        return Path(resolved).resolve()
    raise RuntimeError(f"Missing {name}. Put it on PATH or set DEVKITPRO/CELEBIHUNTER_TOOLCHAIN_ROOT.")


class Toolchain:
    def __init__(self) -> None:
        configured = roots()
        self.gcc = find_tool("arm-none-eabi-gcc", configured)
        suffix = self.gcc.suffix if self.gcc.suffix.lower() == ".exe" else ""
        self.nm = self.gcc.with_name("arm-none-eabi-nm" + suffix)
        self.objdump = self.gcc.with_name("arm-none-eabi-objdump" + suffix)
        self.readelf = self.gcc.with_name("arm-none-eabi-readelf" + suffix)
        self.size = self.gcc.with_name("arm-none-eabi-size" + suffix)
        for path in (self.nm, self.objdump, self.readelf, self.size):
            if not path.is_file():
                raise RuntimeError(f"Missing compiler companion: {path}")
        candidates = list(configured)
        if self.gcc.parent.parent.name.lower() == "devkitarm":
            candidates.append(self.gcc.parent.parent.parent)
        self.libctru = self.find_libctru(candidates)
        self.packager = find_tool("3gxtool", candidates)
        multidir = self.query("-print-multi-directory").replace("\\", "/")
        if multidir != "armv6k/fpu":
            raise RuntimeError(f"SDK does not select required ARMv6K hard-float libraries: {multidir}")
        self.libraries = [self.libctru / "lib/libctru.a"]
        # Query the compiler rather than pinning a GCC version or host path.
        for name in ("libc.a", "libsysbase.a", "libgcc.a"):
            value = self.query("-print-file-name=" + name)
            path = Path(value).resolve()
            if value == name or not path.is_file() or "armv6k/fpu" not in path.as_posix():
                raise RuntimeError(f"Missing ARMv6K hard-float {name}: {value}")
            self.libraries.append(path)
        if not self.libraries[0].is_file():
            raise RuntimeError(f"Missing libctru archive: {self.libraries[0]}")

    def query(self, option: str) -> str:
        return subprocess.check_output([str(self.gcc), *ARCH, option], text=True).strip()

    @staticmethod
    def find_libctru(search_roots: list[Path]) -> Path:
        for base in search_roots:
            candidates = [base / "libctru", base]
            candidates += sorted(base.glob("**/libctru"))
            for path in candidates:
                if (path / "include/3ds.h").is_file():
                    return path.resolve()
        raise RuntimeError("Missing libctru. Set DEVKITPRO or CELEBIHUNTER_TOOLCHAIN_ROOT.")
