# CelebiHunter

<p align="center">
  <a href="https://github.com/Kou1236/CelebiHunter/releases/latest"><img src="https://img.shields.io/github/v/release/Kou1236/CelebiHunter?style=flat-square" alt="Latest release"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/actions/workflows/tests.yml"><img src="https://github.com/Kou1236/CelebiHunter/actions/workflows/tests.yml/badge.svg?branch=main" alt="Tests"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/blob/main/LICENSE"><img src="https://img.shields.io/github/license/Kou1236/CelebiHunter?style=flat-square" alt="License"></a>
  <a href="https://github.com/Kou1236/CelebiHunter/releases"><img src="https://img.shields.io/github/downloads/Kou1236/CelebiHunter/total?style=flat-square" alt="Downloads"></a>
</p>

For players hunting shiny Celebi in the English 3DS Virtual Console release of **Pokémon Crystal Version** on a console running Luma3DS. CelebiHunter provides an automatic reset hunt and a player-controlled RNG build.

[简体中文](README.zh-CN.md)

<p align="center">
  <img src="docs/images/celebi-hardware.jpg" alt="Shiny Celebi encountered on New 3DS hardware" width="720">
</p>

> [!IMPORTANT]
> Pokémon Bank will close on [**February 26, 2027**](https://en-americas-support.nintendo.com/app/answers/detail/a_id/61543/~/pok%C3%A9mon-bank-service-update), and transfers from Bank to Pokémon HOME will end with it.
>
> Among the main-series games that can still transfer Pokémon to HOME, the [GS Ball encounter in the 3DS Virtual Console release of Pokémon Crystal](https://www.pokemon.com/us/strategy/wrangle-rare-pokemon-in-pokemon-crystal) is the only way to catch Shiny Celebi yourself with your own OT, Generation II Trainer ID, and the Game Boy origin mark. CelebiHunter is for players completing this hunt before that transfer route closes.

## Builds

**Reset**

Choose Reset if you want the original `1/8192` hunt without pressing the same buttons over and over. The plugin runs the full loop for you, but there is no guaranteed number of attempts.

**RNG**

Choose RNG if you simply want shiny Celebi as soon as possible. You still press every button yourself. The plugin does not search for or advance to a naturally occurring target frame; it controls the random values used when Celebi is generated, producing the shiny DV combination `FAAA`.

**Install one build at a time.**

## Compatibility

- English Pokémon Crystal Version for 3DS Virtual Console
- Title ID `0004000000172800`
- Ilex Forest Shrine GS Ball Celebi event
- Luma3DS with Plugin Loader enabled
- Old 3DS, Old 2DS, New 3DS and New 2DS
- 268 MHz and 804 MHz modes

Primary test environment: New 3DS, 268 MHz, Luma3DS 13.4. Reports from other console models, CPU modes, and Luma3DS versions are welcome through GitHub Issues.

Japanese Crystal and other encounters are **not currently supported** and may be added in the future.

## Installation

1. Face the Ilex Forest Shrine and save before inserting the GS Ball.
2. Close the game, make a Checkpoint backup, and keep an untouched copy on a computer.
3. Download `Reset.3gx` or `RNG.3gx` from the latest GitHub release.
4. Copy the selected file to:

   ```text
   sd:/luma/plugins/0004000000172800/
   ```

5. Make sure the directory contains only one `.3gx` file.
6. Open Rosalina with `L + D-Pad Down + Select` and enable **Plugin Loader**.
7. Launch Pokémon Crystal.

## Reset

Launch the game and leave the controls alone. Reset selects Continue, activates the shrine, and checks Celebi.

<p align="center">
  <img src="docs/images/reset-result.png" alt="Reset result after finding shiny Celebi" width="400">
</p>

- **Non-shiny:** starts the next cycle automatically.
- **Shiny:** stops and preserves the encounter.
- `B`: stops automation.
- `R`: after a shiny is found or automation is stopped with `B`, resumes the game and hides the result card.

## RNG

RNG has no menu. Install it and play normally.

<p align="center">
  <img src="docs/images/rng-trigger.png" alt="GS Ball event text on Pokémon Crystal VC" width="400">
</p>

1. Reach the shrine and advance the dialogue manually.
2. When `[PLAYER] put in the GS BALL.` is fully visible, release A.
3. Press A once to continue the event.
4. Celebi will be generated with shiny DVs `FAAA`.

### After transferring Celebi

- `FAAA` means 15 Attack, 10 Defense, 10 Speed, and 10 Special. The HP DV is 8, making this a shiny spread in Generation II.
- These DVs are not copied into the modern stats.
- Poké Transporter gives Celebi **five IVs of 31** and rolls the last one at random.
- Generation II has no Natures. An untouched level 30 Celebi becomes **Timid**; gaining experience before transfer can change it.
- Your **OT and Generation II Trainer ID** remain. The Secret ID becomes `00000`.
- Celebi arrives in a regular Poké Ball with Natural Cure and the **Game Boy origin mark**.
- Moving Celebi from Bank to HOME does not change these values.

Full conversion rules: [Poké Transporter](https://bulbapedia.bulbagarden.net/wiki/Pok%C3%A9_Transporter#From_Generation_I_and_II)

## Documentation

- [Building](docs/BUILDING.md)
- [Backup and recovery](docs/SAFETY.md)
- [Roadmap](docs/ROADMAP.md)
- [Contributing](CONTRIBUTING.md)

## License

CelebiHunter is licensed under the [GNU GPL v3.0 or later](LICENSE). See [CREDITS.md](CREDITS.md) for acknowledgements.
