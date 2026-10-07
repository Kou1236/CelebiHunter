# Changelog

## v1.4.0

### RNG

- Fix errors after restarting the game from the VC menu.
- Fix screen flashes when searching or refreshing a Target.
- Fix delayed L steps and the HUD disappearing after the first step.
- Keep the Target during temporary state checks.

### Reset

- No changes. Includes the original v1.0.0 `Reset.3gx`.

## v1.3.0

### RNG

- Stop pinning source admission to one captured CPU/timer image; retain structural scheduler-budget checks and read the live DIV through the current IO pointer.
- Keep each save's StartTime unchanged and derive the temporary RTC input from its value.
- Accept relocated engine and WRAM objects when their live pointers pass coherent layout checks.
- Bind later observations to the current session's captured CPU context.
- Revalidate a released source after a transient running scheduler mismatch instead of permanently losing the forecast.
- Show a specific HUD message when no future target is available.
- Add stable reason codes for scene, scheduler, timing, search, and runtime checks; retain live values, failed reads, and retry history in diagnostic record v5.
- Recover the source when an active input plan encounters a temporary scheduler change.
- Allow an ordinary encounter without a forecast and report off-target A separately from source failures.
- Retry read-only acquisition failures with a bounded delay and distinguish transient rechecks from persistent faults.
- Validate save-time conversion against the original clock arithmetic; report unrepresentable offsets explicitly.
- Accept normal previous-battle data at the shrine; verify the original post-generation call chain before reading this encounter's actual DVs.

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
