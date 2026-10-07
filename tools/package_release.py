"""Create a CelebiHunter release archive from audited artifacts."""
from __future__ import annotations

import hashlib
import pathlib
import zipfile

import package


ROOT = pathlib.Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
BINARIES = ("Reset.3gx", "RNG.3gx")
DOCS = ("README.md", "README.zh-CN.md", "LICENSE", "CHANGELOG.md", "CONTRIBUTING.md")
REQUIRED_GUIDES = ("docs/BUILDING.md", "docs/SAFETY.md", "docs/ROADMAP.md")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> None:
    missing = [name for name in BINARIES if not (DIST / name).is_file()]
    missing += [name for name in (*DOCS, *REQUIRED_GUIDES) if not (ROOT / name).is_file()]
    if missing:
        raise SystemExit("Missing release input: " + ", ".join(missing))

    # Validate the current C RNG package against its ELF. Keep the existing
    # Reset artifact; creating an archive does not rebuild either plugin.
    package.verify(ROOT / "build/rng/RNG.elf", ROOT / "rng/RNG.plgInfo", DIST / "RNG.3gx")

    checksums = "".join(f"{sha256(DIST / name)}  {name}\n" for name in BINARIES)
    checksum_path = DIST / "SHA256SUMS.txt"
    checksum_path.write_text(checksums, encoding="ascii")

    archive = DIST / "CelebiHunter-v1.4.0.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name in BINARIES:
            output.write(DIST / name, name)
        output.write(checksum_path, checksum_path.name)
        for name in DOCS:
            output.write(ROOT / name, name)
        # Retain paths so the README's guide links and images work in the ZIP.
        for path in sorted((ROOT / "docs").rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(ROOT).as_posix())
    print(archive)
    print(checksums, end="")


if __name__ == "__main__":
    main()
