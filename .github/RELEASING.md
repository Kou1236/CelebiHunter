# Releasing

## Hardware check

Test the release build, or verify that its loaded code matches the tested build with only the HUD version changed.

- Keep the original v1.0.0 `Reset.3gx` for releases without Reset changes. If rebuilding Reset, test one non-shiny cycle and confirm `B` stops it.
- RNG: check manual pause, L steps, resume, Target refresh, and the final encounter's DVs against the forecast.
- Use Restart in the VC touch-screen menu and confirm RNG recovers without R04/R08.
- Run the host tests and wait for GitHub Actions to pass before publishing.

## Package

```text
python tools/build.py --rng
python tools/package_release.py
```

## GitHub release

1. Create the matching version tag and release title, currently `v1.4.0`.
2. Use that version's section of `CHANGELOG.md` as the release notes.
3. Upload:
   - `Reset.3gx`
   - `RNG.3gx`
   - `SHA256SUMS.txt`
   - `CelebiHunter-v1.4.0.zip`
4. Verify each uploaded asset's SHA-256 against the local package. Leave an unverified build as a draft.
