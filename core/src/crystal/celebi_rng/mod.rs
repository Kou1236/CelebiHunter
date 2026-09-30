//! Candidate search for the English Crystal VC Celebi event at 268 MHz.
//!
//! The event sequence follows the public PokemonRNGGuides generator.  That
//! generator evaluates 595 ordinary RNG updates from the displayed target.
//! The automatic controller arms the VC JOYP hook one tracked RNG update before
//! the displayed community target.  Hardware traces from 0.2.12-0.2.15 show
//! that the first action-button read occurs on the following update, so this
//! makes the consumed A land on the displayed target itself.  The public
//! 595-update model and displayed target stay unchanged.
//! Hardware DVs remain the only reset authority.
mod div;
mod rng;
pub use div::Div;
pub use rng::GameboyRng;

pub const MAX_OUTCOMES: usize = 4;
const CUTSCENE_PREFIX_VBLANKS: usize = 595;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Snapshot {
    pub advance: u32,
    pub state: u16,
    pub div: u16,
    pub adiv_index: usize,
    pub sdiv_index: usize,
}

impl Snapshot {
    pub fn valid(&self) -> bool {
        self.adiv_index < 0x4000 && self.sdiv_index < 0x4000
    }

    pub fn rng(&self) -> GameboyRng {
        GameboyRng::new(
            self.state,
            Div::new(self.adiv_index, (self.div >> 8) as u8),
            Div::new(self.sdiv_index, self.div as u8),
        )
    }

    pub fn matches(&self, rng: &GameboyRng) -> bool {
        self.state == rng.state()
            && self.div == ((rng.adiv() as u16) << 8 | rng.sdiv() as u16)
            && self.adiv_index == rng.add_div.index()
            && self.sdiv_index == rng.sub_div.index()
    }
}

pub fn shiny(atkdef: u8, spespc: u8) -> bool {
    spespc == 0xaa && atkdef & 0x2f == 0x2a
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Candidate {
    pub snapshot: Snapshot,
    pub trigger_snapshot: Snapshot,
    pub outcomes: [[u8; 2]; MAX_OUTCOMES],
    pub outcome_count: u8,
    pub shiny_mask: u64,
}

impl Candidate {
    pub fn outcomes(&self) -> &[[u8; 2]] {
        &self.outcomes[..self.outcome_count as usize]
    }

    /// Snapshot where the automatic controller arms A.  The VC consumes the
    /// first action-button JOYP read on the next tracked update, which is the
    /// displayed community target stored in `snapshot`.
    pub fn auto_trigger_snapshot(&self) -> Snapshot {
        self.trigger_snapshot
    }

    /// Return the shiny DV pair selected at this nearest Advance.
    ///
    /// The public model can expose up to four execution branches at the same
    /// Advance. Prefer a learned branch when one is available; otherwise take
    /// the first shiny branch. `outcomes_after_prefix` orders the two-Random
    /// paths before the three-Random paths, so ties also prefer the shorter
    /// public path.
    pub fn selected_shiny_dvs(&self, preferred_mask: u64) -> Option<[u8; 2]> {
        let mut preferred = if preferred_mask == 0 || preferred_mask == u64::MAX {
            self.shiny_mask
        } else {
            self.shiny_mask & preferred_mask
        };
        // A learned branch may be absent at the nearest Advance.  Falling
        // back to that candidate's first shiny branch preserves the user's
        // primary rule: minimize Advance before preferring a branch.
        if preferred == 0 {
            preferred = self.shiny_mask;
        }
        if preferred == 0 {
            return None;
        }
        let index = preferred.trailing_zeros() as usize;
        if index >= self.outcome_count as usize {
            None
        } else {
            Some(self.outcomes[index])
        }
    }
}

#[derive(Clone, Copy)]
pub struct OutcomeSet {
    values: [[u8; 2]; MAX_OUTCOMES],
    len: usize,
}

impl OutcomeSet {
    const fn new() -> Self {
        Self {
            values: [[0; 2]; MAX_OUTCOMES],
            len: 0,
        }
    }

    fn push(&mut self, value: [u8; 2]) {
        // Keep all four public parameter branches in stable order even when
        // two branches happen to produce the same DVs. Closed-loop learning
        // stores branch bits, so value de-duplication would shift their meaning.
        debug_assert!(self.len < MAX_OUTCOMES);
        if self.len < MAX_OUTCOMES {
            self.values[self.len] = value;
            self.len += 1;
        }
    }

    pub fn values(&self) -> &[[u8; 2]] {
        &self.values[..self.len]
    }
}

fn generate_celebi_rands(prefix: &GameboyRng, extra_consumed_rands: u8, div_off: u8) -> [u8; 2] {
    use rng::Offset;

    let mut rng = prefix.clone();
    let adiv_index = rng.add_div.index();
    let sdiv_index = rng.sub_div.index();
    rng.next_with_div_offset(Offset::Minus(0x0c));

    for _ in 0..extra_consumed_rands {
        rng.next();
    }

    rng.next_with_div_inc(Offset::Plus(div_off));
    rng.add_div.set_index(adiv_index);
    rng.add_div.decrement_index(2);
    rng.sub_div.set_index(sdiv_index);
    rng.sub_div.decrement_index(2);

    for _ in 0..12 {
        rng.next();
    }

    rng.next_with_div_inc(Offset::Plus(0x6f));
    rng.add_div.set_index(0);
    rng.sub_div.set_index(3);
    let first = rng.next_with_div_inc(Offset::Plus(0x0c));
    let second = rng.next_with_div_inc(Offset::Plus(0xe4));
    [first[1], second[1]]
}

/// Enumerate the four branches from the public PokemonRNGGuides generator.
///
/// `prefix` is the state after the public generator's 595 ordinary updates.
pub fn outcomes_after_prefix(prefix: &GameboyRng) -> OutcomeSet {
    let mut out = OutcomeSet::new();
    for extra in [2, 3] {
        for div_off in [0xba, 0xbb] {
            out.push(generate_celebi_rands(prefix, extra, div_off));
        }
    }
    out
}

/// Prospectively observed AUTO 0.2.44 post-wait Tail64 transform.
///
/// This is intentionally separate from the public community model.  The
/// constants replay run 0000000177722EBA exactly and require untouched
/// hardware validation before promotion beyond model 1.
pub fn tail64_model1_outcome(snapshot: Snapshot) -> Option<[u8; 2]> {
    const ADD_MOD: u8 = 0x76;
    const BASE_SUB: u8 = 0xf7;
    const DIRECT_OFFSETS: [u8; 3] = [0xec, 0xf2, 0xf4];

    let start_add = (snapshot.state >> 8) as u8;
    let start_sub = snapshot.state as u8;
    let (first_add, carry) = start_add.overflowing_add(ADD_MOD);
    let mut state = ((first_add as u16) << 8)
        | start_sub.wrapping_sub(BASE_SUB.wrapping_add(carry as u8)) as u16;
    let mut outputs = [0u8; 3];
    for (index, offset) in DIRECT_OFFSETS.iter().copied().enumerate() {
        let [add, sub] = GameboyRng::advance_state(
            (state >> 8) as u8,
            state as u8,
            ((snapshot.div >> 8) as u8).wrapping_add(offset),
            (snapshot.div as u8).wrapping_add(offset),
        );
        state = ((add as u16) << 8) | sub as u16;
        outputs[index] = sub;
    }
    // The first call selects the held-item branch. Model 1 has prospective
    // evidence only for the observed three-call path.
    if outputs[0] >= 0xc0 {
        None
    } else {
        Some([outputs[1], outputs[2]])
    }
}

pub struct Search {
    cursor: GameboyRng,
    prefix: GameboyRng,
    advance: u32,
    remaining: u32,
    previous: Option<Snapshot>,
}

pub fn outcome_mask_satisfies(shiny_mask: u64, required_mask: u64) -> bool {
    if required_mask == u64::MAX {
        shiny_mask != 0
    } else {
        required_mask != 0 && shiny_mask & required_mask == required_mask
    }
}

impl Search {
    pub fn new(snapshot: Snapshot, count: u32) -> Option<Self> {
        if !snapshot.valid() || count == 0 || snapshot.advance.checked_add(count).is_none() {
            return None;
        }
        let cursor = snapshot.rng();
        let mut prefix = cursor.clone();
        for _ in 0..CUTSCENE_PREFIX_VBLANKS {
            prefix.next();
        }
        Some(Self {
            cursor,
            prefix,
            advance: snapshot.advance,
            remaining: count,
            previous: None,
        })
    }

    pub fn remaining(&self) -> u32 {
        self.remaining
    }

    pub fn batch(&mut self, count: u32) -> Option<Candidate> {
        self.batch_for_mask(count, u64::MAX)
    }

    /// Search the exact prospective Tail64 model-1 path. One predicted result
    /// remains in `outcomes`, so actual battle DVs retain final authority.
    pub fn batch_tail64_model1(&mut self, count: u32) -> Option<Candidate> {
        for _ in 0..count.min(self.remaining) {
            let snapshot = Snapshot {
                advance: self.advance,
                state: self.cursor.state(),
                div: (self.cursor.adiv() as u16) << 8 | self.cursor.sdiv() as u16,
                adiv_index: self.cursor.add_div.index(),
                sdiv_index: self.cursor.sub_div.index(),
            };
            let trigger_snapshot = self.previous;
            self.previous = Some(snapshot);
            self.cursor.next();
            self.prefix.next();
            self.advance += 1;
            self.remaining -= 1;
            if let Some(dvs) = tail64_model1_outcome(snapshot) {
                if shiny(dvs[0], dvs[1]) && trigger_snapshot.is_some() {
                    let mut outcomes = [[0u8; 2]; MAX_OUTCOMES];
                    outcomes[0] = dvs;
                    return Some(Candidate {
                        snapshot,
                        trigger_snapshot: trigger_snapshot.unwrap(),
                        outcomes,
                        outcome_count: 1,
                        shiny_mask: 1,
                    });
                }
            }
        }
        None
    }

    /// Search only candidates whose shiny outcome intersects `required_mask`.
    /// An all-ones mask preserves the public generator's original behaviour.
    pub fn batch_for_mask(&mut self, count: u32, required_mask: u64) -> Option<Candidate> {
        for _ in 0..count.min(self.remaining) {
            let outcome_set = outcomes_after_prefix(&self.prefix);
            let mut mask = 0u64;
            for (i, [ad, ss]) in outcome_set.values().iter().copied().enumerate() {
                if shiny(ad, ss) {
                    mask |= 1u64 << i;
                }
            }
            let snapshot = Snapshot {
                advance: self.advance,
                state: self.cursor.state(),
                div: (self.cursor.adiv() as u16) << 8 | self.cursor.sdiv() as u16,
                adiv_index: self.cursor.add_div.index(),
                sdiv_index: self.cursor.sub_div.index(),
            };
            let trigger_snapshot = self.previous;
            self.previous = Some(snapshot);
            self.cursor.next();
            self.prefix.next();
            self.advance += 1;
            self.remaining -= 1;
            if outcome_mask_satisfies(mask, required_mask) && trigger_snapshot.is_some() {
                return Some(Candidate {
                    snapshot,
                    trigger_snapshot: trigger_snapshot.unwrap(),
                    outcomes: outcome_set.values,
                    outcome_count: outcome_set.len as u8,
                    shiny_mask: mask,
                });
            }
        }
        None
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Position {
    Before,
    PreTarget,
    Target,
    Invalid,
}

pub struct Track {
    expected: GameboyRng,
    last: u32,
    target: Snapshot,
}

impl Track {
    pub fn new(start: Snapshot, target: Snapshot) -> Option<Self> {
        if !start.valid() || !target.valid() || target.advance <= start.advance {
            return None;
        }
        Some(Self {
            expected: start.rng(),
            last: start.advance,
            target,
        })
    }

    pub fn observe(&mut self, live: Snapshot) -> Position {
        let delta = match live.advance.checked_sub(self.last) {
            Some(delta) if delta <= 8 => delta,
            _ => return Position::Invalid,
        };
        for _ in 0..delta {
            self.expected.next();
        }
        self.last = live.advance;
        if !live.matches(&self.expected) || live.advance > self.target.advance {
            return Position::Invalid;
        }
        if live.advance == self.target.advance {
            return if live == self.target {
                Position::Target
            } else {
                Position::Invalid
            };
        }
        if live.advance + 1 == self.target.advance {
            Position::PreTarget
        } else {
            Position::Before
        }
    }
}
