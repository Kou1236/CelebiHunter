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

Choose RNG if you want to time the encounter for shiny Celebi. It is based on [PokeReader](https://github.com/zaksabeast/PokeReader)'s Celebi RNG method, with improvements. The plugin finds a target and shows the Advance and predicted DVs. You still press every button yourself.

If you just want shiny Celebi and do not need to follow the RNG timing, [RNG v1.0.0](https://github.com/Kou1236/CelebiHunter/releases) is still available. It changes the DIV values returned to the RNG routine to produce fixed `FAAA`.

**Install one build at a time.**

## Compatibility

- English Pokémon Crystal Version for 3DS Virtual Console
- Title ID `0004000000172800`
- Ilex Forest Shrine GS Ball Celebi event
- Luma3DS with Plugin Loader enabled
- Reset v1.0.0: Old 3DS, Old 2DS, New 3DS, New 2DS, and 268/804 MHz modes
- RNG v1.1.0: tested on New 3DS at 268 MHz with Luma3DS 13.4

Primary test environment: New 3DS, 268 MHz, Luma3DS 13.4. Other console models, CPU modes, and Luma3DS versions have not been tested with the RNG build. Reports are welcome through GitHub Issues.

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

At the final GS Ball text, the plugin searches for the next shiny target. The transparent HUD shows the current Advance, Target, predicted DVs, and actual DVs.

<p align="center">
  <img src="docs/images/rng-trigger.png" alt="GS Ball event text on Pokémon Crystal VC" width="400">
</p>

1. Reach the shrine and advance the dialogue manually.
2. When `[PLAYER] put in the GS BALL.` is fully visible, release A. The plugin starts searching for a shiny target.
3. Press `Start + Up` to show or hide the transparent HUD.
4. Press `L + R` to pause near the target. Press `L` to advance one step at a time, or `R` to resume.
5. When **Advance** equals **Target**, press A to continue the event. Release A and wait for Celebi to appear. The HUD will show its actual DVs.

If you miss the target, the plugin searches for another. The target may also change if the game state changes while you wait. Follow the Target currently shown in the HUD. After pressing A, leave the other buttons alone until Celebi appears.

For details, see [How RNG works](#how-rng-works).

### After transferring Celebi

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

CelebiHunter is licensed under the [GNU GPL v3.0 or later](LICENSE).

## How RNG works

Crystal's [`Random` routine](https://github.com/pret/pokecrystal/blob/master/home/random.asm) reads the Game Boy divider (`DIV`) to update its RNG state. Celebi's DVs depend on the timing of those reads during the GS Ball event.

The plugin reads the current RNG and timing state, including the real DIV and its countdown. It controls RTC, GameTime, TIMA, and the emulator's execution clock budget and phase to keep the timing predictable. These settings change the timing environment, but Crystal still generates Celebi through its original encounter routine.

Building on [PokeReader](https://github.com/zaksabeast/PokeReader)'s method, this version improves timing reads and simplifies the calculations. It reads the parameters and searches for targets in the plugin, so you do not need to wait for an RNG index or use a separate RNG website. Once the battle starts, the HUD shows Celebi's actual DVs.

## Credits

Thanks to these projects and their authors:

- [PokeReader](https://github.com/zaksabeast/PokeReader) and [Pokémon RNG Guides](https://github.com/zaksabeast/PokemonRNGGuides) by zaksabeast: game reading and Celebi RNG prediction.
- [pret/pokecrystal](https://github.com/pret/pokecrystal): Crystal disassembly and symbols.
- [Pan Docs](https://gbdev.io/pandocs/) by gbdev: Game Boy timer and interrupt documentation.
- [Luma3DS](https://github.com/LumaTeam/Luma3DS) by LumaTeam: the 3GX plugin loader and Rosalina debugger.
- [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework) by The Pixellizer Group and [Blank Template](https://github.com/PabloMK7/CTRPluginFramework-BlankTemplate) by PabloMK7: plugin interfaces and 3GX build references.
