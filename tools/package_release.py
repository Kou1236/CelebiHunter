"""Create a CelebiHunter release archive from audited artifacts."""
from __future__ import annotations

import hashlib
import pathlib
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
BINARIES = ("Reset.3gx", "RNG.3gx")
DOCS = ("README.md", "README.zh-CN.md", "docs/SAFETY.md", "CREDITS.md", "LICENSE")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main() -> None:
    missing = [name for name in (*BINARIES, *DOCS) if not (DIST / name).is_file() and not (ROOT / name).is_file()]
    if missing:
        raise SystemExit("Missing release input: " + ", ".join(missing))

    checksums = "".join(f"{sha256(DIST / name)}  {name}\n" for name in BINARIES)
    checksum_path = DIST / "SHA256SUMS.txt"
    checksum_path.write_text(checksums, encoding="ascii")

    archive = DIST / "CelebiHunter-v1.0.0.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name in BINARIES:
            output.write(DIST / name, name)
        output.write(checksum_path, checksum_path.name)
        for name in DOCS:
            output.write(ROOT / name, pathlib.Path(name).name)
    print(archive)
    print(checksums, end="")


if __name__ == "__main__":
    main()
