# Building

RNG v1.4.0 is built from the C/assembly sources in `rng/`. Reset keeps the v1.0.0 Rust core and C/assembly entry layer.

## Dependencies

- Python 3.10 or newer
- devkitARM and libctru
- 3gxtool

Reset retains its original Windows build setup: Rustup, Cargo, Rust nightly `nightly-2024-03-21-x86_64-pc-windows-gnu`, and the target/build-std components for `armv6k-nintendo-3ds`. Building RNG alone does not need Rust.

The build script checks PATH and devkitPro locations. Set `DEVKITPRO` to your devkitPro directory if needed. To use a toolchain stored elsewhere, set:

```text
CELEBIHUNTER_TOOLCHAIN_ROOT=/path/to/toolchain
```

For an offline Reset build with a Cargo vendor directory, set:

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

Outputs are written to `dist/Reset.3gx`, `dist/RNG.3gx`, and `dist/SHA256SUMS.txt`. RNG's build checks its ELF layout and packaged 3GX contents before accepting the output.

`rng/build_sources.json` lists and hashes the RNG sources and their dependencies. Update the matching hashes when changing those files. `tools/build_legacy.py` preserves the original Reset build; its old RNG path is retained for the v1.0.0 source history.

## Tests

Run the RNG tests with a host C compiler (`gcc` or `clang`):

```text
python tests/run_rng_tests.py
```

Use `--cc` or `CC` to choose the compiler. On Windows, Zig is also supported:

```text
python tests/run_rng_tests.py --zig /path/to/zig.exe
```

The suite covers candidate search, scalar comparison, player input, long waits, and saved waiting-state fixtures. It mocks device interfaces; hardware reports remain useful for other console and CPU configurations.

Run the unchanged Reset and controller tests with Rust:

```text
mkdir build
rustc --test --edition 2021 tests/celebi.rs -O -o build/celebi_tests
build/celebi_tests
```

`tests/celebi_manual.rs` tests the historical v1.0.0 RNG implementation, not RNG v1.4.0. CI runs the new C suite and the Reset tests.

## Package a release

After building both versions, run:

```text
python tools/package_release.py
```

The release archive and checksums are written to `dist/`. For v1.1.0, use the original v1.0.0 `Reset.3gx` alongside the new RNG file. Its SHA256 is:

```text
765cacc20faac806857c2744a201b890b76f53248cbd3dc870f6b9af1f1a3724
```

Do not include a toolchain, ROM, CIA, native code dump, save, or personal capture in a release.
