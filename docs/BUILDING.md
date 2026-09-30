# Building

CelebiHunter uses Rust for the core and C/assembly for the 3GX entry layer.

## Dependencies

- Python 3.10 or newer
- Rustup and Cargo
- Rust nightly `nightly-2024-03-21`
- the Rust target/build-std components needed for `armv6k-nintendo-3ds`
- devkitARM and libctru
- 3gxtool

The build script checks standard PATH and devkitPro locations. To use a self-contained toolchain, set:

```text
CELEBIHUNTER_TOOLCHAIN_ROOT=/path/to/toolchain
```

For an offline Cargo vendor directory, set:

```text
CELEBIHUNTER_VENDOR_DIR=/path/to/vendor
```

## Commands

Build both versions:

```text
python tools/build.py
```

Build one mode:

```text
python tools/build.py --reset
python tools/build.py --rng
```

Outputs are written to `dist/Reset.3gx`, `dist/RNG.3gx`, and `dist/SHA256SUMS.txt`.

## Tests

The focused host tests are standalone Rust programs:

```text
rustc --test --edition 2021 tests/celebi_manual.rs -O -o build/celebi_manual_tests
build/celebi_manual_tests

rustc --test --edition 2021 tests/celebi.rs -O -o build/celebi_tests
build/celebi_tests
```

## Package a release

After building both versions, run:

```text
python tools/package_release.py
```

The release archive and checksums are written to `dist/`.
