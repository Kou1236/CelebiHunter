# Changelog

## v1.1.0

### RNG

- Predict the next shiny Celebi target from the current RNG and timing state, based on PokeReader's method.
- Read the real DIV instead of replacing its return value to force `FAAA`.
- Control RTC, GameTime, TIMA, and execution timing for the encounter model.
- Show current Advance, Target, predicted DVs, and actual DVs in a transparent HUD.
- Search automatically at the final GS Ball text and recalculate after a missed or changed target.
- Keep pause, step, resume, and the final A press under player control.
- Build the current C/assembly implementation from the repository, with matching host tests.

### Reset

- No changes. The release includes the original v1.0.0 `Reset.3gx`.

## v1.0.0

Initial release.

### Reset

- Automatic Celebi hunting from the title screen.
- Soft reset after a non-shiny encounter.
- Stop and preserve the encounter when a shiny is found.

### RNG

- Manual shrine sequence with no automatic input.
- Generate Celebi with shiny DVs `FAAA`.
