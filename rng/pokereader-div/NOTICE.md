Current CelebiHunter RNG algorithm attribution
=============================================

The candidate algorithms shipped in this release are implemented in:

- `candidate-clock/released_div_prediction.h`: the retained native scheduler's
  released DIV boundary and Normal VBlank DIV-read arithmetic.
- `candidate-query/query.c` and `candidate-query/terminal_eval.c`: candidate
  search and Crystal Celebi terminal evaluation, using the generated arithmetic
  inputs in `data/profile_data.h`.

The released native clock model and generated arithmetic inputs are
CelebiHunter's retained-source derivations. The upstream Game Boy RNG and
Crystal Celebi generator were used as an algorithm reference during development;
their attribution and licensing are retained below. The current candidate
modules compute from copied source state and fixed arithmetic inputs. Their
calculations change local arithmetic state and perform no device, socket, SD,
filesystem or input access.

Upstream reference: the Rust DIV, Game Boy RNG and Crystal Celebi generator in
zaksabeast/PokemonRNGGuides, commit
`b6d7a2467093d1a8349bbefa233a32dc9618829e`.

Upstream source: https://github.com/zaksabeast/PokemonRNGGuides

Upstream source modules:

- `rng_tools/src/rng/gameboy/div.rs`
- `rng_tools/src/rng/gameboy/rng.rs`
- `rng_tools/src/generators/gen2/celebi.rs`

Upstream authors retain copyright in their work. These three source files carry
no separate copyright notice. The upstream GPL version 3 license is reproduced
verbatim in LICENSE, including its Free Software Foundation copyright notice.
The derivative C code is distributed under GPL version 3.

The empirical upstream DIV period and offsets produce **possible** candidates.
Agreement with that reference model does not establish agreement with retained
native clocks or guarantee an encounter. The current released native clock
model requires its captured-source and scheduler contracts to be satisfied.
