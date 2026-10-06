"""Build Reset with its original implementation and RNG with the C v1.2.0 runtime."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reset", action="store_true", help="Build the unchanged Rust Reset implementation")
    parser.add_argument("--rng", action="store_true", help="Build the C RNG v1.2.0 implementation; Rust is not required")
    args = parser.parse_args()
    both = not (args.reset or args.rng)
    if args.reset or both:
        # Preserve the original script byte-for-byte for Reset's existing build.
        # It is never used for the public --rng path.
        import build_legacy
        build_legacy.build("reset")
    if args.rng or both:
        import build_rng
        build_rng.build()
    # Preserve the checksum entry for an existing unchanged Reset artifact.
    checksums = "".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest().upper()}  {path.name}\n"
        for name in ("Reset.3gx", "RNG.3gx") if (path := DIST / name).is_file())
    (DIST / "SHA256SUMS.txt").write_text(checksums, encoding="ascii")
    print(checksums, end="")


if __name__ == "__main__":
    main()
