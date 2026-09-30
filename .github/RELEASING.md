# Releasing

## Hardware check

Test the exact `Reset.3gx` and `RNG.3gx` files that will be uploaded.

- Reset: complete one full non-shiny reset cycle, then confirm `B` stops the automation.
- RNG: complete the shrine sequence manually and confirm a shiny `FAAA` Celebi.
- Restart the game once with each version installed.

## Package

```text
python tools/build.py
python tools/package_release.py
```

## GitHub release

1. Create the tag and release title `v1.0.0`.
2. Use the `v1.0.0` section of `CHANGELOG.md` as the release notes.
3. Upload:
   - `Reset.3gx`
   - `RNG.3gx`
   - `SHA256SUMS.txt`
   - `CelebiHunter-v1.0.0.zip`
4. Publish as a pre-release until the planned hardware tests are complete.
