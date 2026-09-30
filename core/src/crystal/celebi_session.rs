use super::celebi_rng::{shiny, Candidate, Position, Search, Snapshot, Track};

pub const SEARCH_COUNT: u32 = 131_072;
pub const SEARCH_BATCH: u32 = 4096;
const MAX_AUTO_RESEARCHES: u8 = 32;
// Retained only for the legacy diagnostic screen text. Runtime authorization
// uses `supported_cpu_mhz` below.
pub const EXPECTED_CPU_MHZ: u32 = 268;
pub const fn supported_cpu_mhz(cpu_mhz: u32) -> bool {
    cpu_mhz == 268 || cpu_mhz == 804
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Idle,
    Searching,
    Candidate,
    Stepping,
    PreTarget,
    Ready,
    Waiting,
    Shiny,
    Miss,
    Fault,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Command {
    None,
    Search,
    Arm,
    Cancel,
    Trigger,
    Resume,
}

#[derive(Clone, Copy, Debug)]
pub struct Observation {
    pub rng: Option<Snapshot>,
    pub cpu_mhz: u32,
    pub tid: u16,
    pub map: (u8, u8),
    pub battle: u8,
    pub battle_start: u8,
    pub battle_ended: u8,
    pub species: u8,
    pub level: u8,
    pub hp: u16,
    pub max_hp: u16,
    pub dvs: [u8; 2],
}

impl Observation {
    fn environment_error(&self) -> Option<&'static str> {
        if self.cpu_mhz == 0 {
            Some("CPU query unavailable")
        } else if !supported_cpu_mhz(self.cpu_mhz) {
            Some("CPU mismatch; see values")
        } else if self.map != (3, 52) {
            Some("Map mismatch; see values")
        } else {
            None
        }
    }
    fn environment_ok(&self) -> bool {
        self.environment_error().is_none()
    }
    fn encounter_ready(&self) -> bool {
        self.battle == 1
            && self.species == 251
            && self.level == 30
            && self.hp > 0
            && self.hp <= self.max_hp
            && self.battle_ended == 0
    }
}

pub struct Session {
    pub phase: Phase,
    pub message: &'static str,
    pub candidate: Option<Candidate>,
    pub result: Option<[u8; 2]>,
    pub anchor: Option<Snapshot>,
    search: Option<Search>,
    track: Option<Track>,
    wait_frames: u32,
    saw_pre_battle: bool,
    saw_battle_init: bool,
    stable_frames: u8,
    last_dvs: Option<[u8; 2]>,
    last_step_advance: Option<u32>,
    waiting_last_advance: u32,
    stalled: u16,
    auto_researches: u8,
    required_outcome_mask: u64,
    tail64_model1: bool,
}

impl Session {
    pub fn invalidate_battle_sample(&mut self) {
        self.stable_frames = 0;
        self.last_dvs = None;
    }
    pub const fn new() -> Self {
        Self {
            phase: Phase::Idle,
            message: "Pause at GS BALL text",
            candidate: None,
            result: None,
            anchor: None,
            search: None,
            track: None,
            wait_frames: 0,
            saw_pre_battle: false,
            saw_battle_init: false,
            stable_frames: 0,
            last_dvs: None,
            last_step_advance: None,
            waiting_last_advance: 0,
            stalled: 0,
            auto_researches: 0,
            required_outcome_mask: u64::MAX,
            tail64_model1: false,
        }
    }

    pub fn set_required_outcome_mask(&mut self, mask: u64) {
        self.required_outcome_mask = if mask == 0 { u64::MAX } else { mask };
    }

    pub fn enable_tail64_model1(&mut self) {
        self.tail64_model1 = true;
    }

    /// Begin the encounter only from the exact target displayed by the public
    /// Celebi generator. The automatic JOYP path arms one VBlank earlier;
    /// physical-A reference mode deliberately advances to Candidate.snapshot.
    pub fn trigger_public_target(&mut self, obs: Observation) -> bool {
        let expected = self.candidate.map(|candidate| candidate.snapshot);
        if self.phase != Phase::Ready || expected.is_none() || obs.rng != expected {
            self.fail("Physical A was not at public target");
            return false;
        }
        let snap = expected.unwrap();
        self.phase = Phase::Waiting;
        self.waiting_last_advance = snap.advance;
        self.saw_pre_battle = obs.battle == 0;
        self.message = "Wait; no more inputs";
        true
    }

    /// Start the encounter from the frozen GS Ball boundary after the nearest
    /// public candidate has supplied the desired shiny DV pair.  The direct
    /// resolver controls the three later item/DV outputs, so reaching the
    /// candidate's natural Advance is deliberately unnecessary in fast mode.
    pub fn trigger_selected_candidate_now(&mut self, obs: Observation) -> bool {
        let anchor = self.anchor;
        if self.phase != Phase::Candidate
            || self.candidate.is_none()
            || anchor.is_none()
            || obs.rng != anchor
        {
            self.fail("Fast A was not at frozen anchor");
            return false;
        }
        if let Some(message) = obs.environment_error() {
            self.fail(message);
            return false;
        }
        if obs.battle != 0 {
            self.fail("Battle active before fast A");
            return false;
        }
        let snap = anchor.unwrap();
        self.phase = Phase::Waiting;
        self.waiting_last_advance = snap.advance;
        self.saw_pre_battle = true;
        self.message = "Fast A sent; wait for selected DVs";
        true
    }

    /// Start from the verified GS Ball boundary without selecting or changing
    /// any RNG result. The Reset build uses this after its per-attempt timing
    /// delay; every FF04 read and both DV bytes remain game-generated.
    pub fn trigger_unmodified_now(&mut self, obs: Observation) -> bool {
        if self.phase != Phase::Idle {
            self.fail("Natural trigger was not idle");
            return false;
        }
        if let Some(message) = obs.environment_error() {
            self.fail(message);
            return false;
        }
        if obs.battle != 0 {
            self.fail("Battle active before natural A");
            return false;
        }
        // Reset does not consume, target, or modify the tracked RNG.
        // Keep a valid snapshot for optional audit data when one is already
        // available, but never delay the visible-prompt response waiting for
        // the DIV tracker to identify its phase.
        self.anchor = obs.rng.filter(Snapshot::valid);
        self.phase = Phase::Waiting;
        self.waiting_last_advance = self.anchor.map(|snap| snap.advance).unwrap_or(0);
        self.saw_pre_battle = true;
        self.message = "Natural A sent; game chooses DVs";
        true
    }

    fn search_from(&mut self, snap: Snapshot, message: &'static str) {
        self.anchor = Some(snap);
        self.candidate = None;
        self.track = None;
        self.search = Search::new(snap, SEARCH_COUNT);
        if self.search.is_some() {
            self.phase = Phase::Searching;
            self.message = message;
            self.last_step_advance = None;
            self.stalled = 0;
        } else {
            self.fail("Invalid search range");
        }
    }

    pub fn fail(&mut self, message: &'static str) {
        self.phase = Phase::Fault;
        self.message = message;
        self.search = None;
        self.track = None;
        self.candidate = None;
    }

    pub fn remaining(&self) -> u32 {
        self.search.as_ref().map(Search::remaining).unwrap_or(0)
    }

    // Return true only for ONE upstream-style frame step while remaining paused.
    // No input injection, reset, save edit or automatic encounter confirmation.
    pub fn tick(&mut self, obs: Observation, paused: bool, cmd: Command) -> bool {
        if cmd == Command::Cancel {
            *self = Self::new();
            self.message = "Cancelled; no auto input";
            return false;
        }
        if matches!(self.phase, Phase::Shiny | Phase::Miss) {
            // Latch the result until explicit cancellation. Never reset a shiny.
            return false;
        }
        if cmd == Command::Search && paused {
            let required_outcome_mask = self.required_outcome_mask;
            let tail64_model1 = self.tail64_model1;
            *self = Self::new();
            self.required_outcome_mask = required_outcome_mask;
            self.tail64_model1 = tail64_model1;
            if let Some(message) = obs.environment_error() {
                self.fail(message);
                return false;
            }
            if obs.battle != 0 {
                self.fail("Battle active; see values");
                return false;
            }
            if let Some(snap) = obs.rng.filter(Snapshot::valid) {
                self.search_from(snap, "Searching candidates");
            } else {
                self.fail("Wait for DIV tracking");
            }
        }
        if matches!(self.phase, Phase::Idle | Phase::Fault) {
            return false;
        }
        // Searching uses the frozen HRAM RNG snapshot and in-plugin DIV
        // trackers. D000-DFFF may legitimately expose a graphics WRAM bank at
        // the pause boundary, so do not re-read player/map fields per batch.
        if self.phase == Phase::Searching {
            if !paused || obs.rng != self.anchor || matches!(cmd, Command::Resume | Command::Trigger) {
                self.fail("Snapshot changed");
                return false;
            }
            let search = self.search.as_mut().unwrap();
            let candidate = if self.tail64_model1 {
                search.batch_tail64_model1(SEARCH_BATCH)
            } else {
                search.batch_for_mask(SEARCH_BATCH, self.required_outcome_mask)
            };
            if let Some(candidate) = candidate {
                if candidate.snapshot.advance > self.anchor.unwrap().advance.saturating_add(1) {
                    // Arm one update before the public displayed target.  The
                    // first VC action-button read is observed on the following
                    // tracked update and must therefore equal that target.
                    self.track = Track::new(self.anchor.unwrap(), candidate.auto_trigger_snapshot());
                    self.candidate = Some(candidate);
                    self.phase = Phase::Candidate;
                    self.message = if self.tail64_model1 {
                        "Tail64 model1: arm one before"
                    } else {
                        "Community target: arm one before"
                    };
                }
            } else if search.remaining() == 0 {
                self.fail("No candidate in range");
            }
            return false;
        }
        if self.phase == Phase::Waiting {
            if let Some(snap) = obs.rng {
                if snap.advance < self.waiting_last_advance {
                    self.fail("Reset during encounter");
                    return false;
                }
                self.waiting_last_advance = snap.advance;
            }
            // Do not count pause-loop polls as advancing game frames.
            if !paused {
                self.wait_frames += 1;
                // The exact TID/map fields are banked and may be temporarily
                // unavailable during the encounter transition.  The trigger
                // was already authorized in the verified shrine context, so
                // wait for the specific fresh Celebi battle instead of failing
                // on transient cutscene memory.
                self.saw_pre_battle |= obs.battle == 0;
                if obs.encounter_ready() {
                    // DoBattle sets this flag; BattleTurn clears it after setup.
                    // It is useful when observed, but the bank can switch while
                    // the one-frame value is live.  A verified pre-battle state
                    // followed by stable Celebi fields is an independent fresh
                    // encounter witness.
                    self.saw_battle_init |= obs.battle_start == 1;
                    if self.last_dvs == Some(obs.dvs) {
                        self.stable_frames = self.stable_frames.saturating_add(1);
                    } else {
                        self.stable_frames = 0;
                    }
                    self.last_dvs = Some(obs.dvs);
                    if (self.saw_pre_battle || self.saw_battle_init)
                        && obs.battle_start == 0
                        && self.stable_frames >= 3
                    {
                        self.result = Some(obs.dvs);
                        self.phase = if shiny(obs.dvs[0], obs.dvs[1]) {
                            Phase::Shiny
                        } else {
                            Phase::Miss
                        };
                        self.message = "Actual battle DVs";
                    }
                } else {
                    self.stable_frames = 0;
                    self.last_dvs = None;
                }
                if self.wait_frames > 3600 && self.phase == Phase::Waiting {
                    self.fail("Battle timeout; review");
                }
            }
            return false;
        }
        if !obs.environment_ok() {
            self.fail("Environment changed");
            return false;
        }
        if obs.battle != 0 {
            self.fail("Unexpected battle");
            return false;
        }
        let snap = match obs.rng.filter(Snapshot::valid) {
            Some(s) => s,
            None => {
                self.fail("DIV tracking lost");
                return false;
            }
        };
        if cmd == Command::Resume || (!paused && !matches!(self.phase, Phase::Stepping | Phase::Waiting)) {
            self.fail("Unpaused; search again");
            return false;
        }
        let position = self
            .track
            .as_mut()
            .map(|track| track.observe(snap))
            .unwrap_or(Position::Invalid);
        if position == Position::Invalid {
            // A displayed DIV index can cross one of the upstream model's
            // paired adjustment boundaries differently from a previously
            // anchored prediction.  A stale target must never be used, but
            // this is recoverable: freeze and recompute from the exact live
            // state instead of terminating the whole automatic run.
            self.auto_researches = self.auto_researches.saturating_add(1);
            if self.auto_researches > MAX_AUTO_RESEARCHES {
                self.fail("Repeated RNG shifts; stopped");
            } else {
                self.search_from(snap, "RNG shifted: re-searching");
            }
            return false;
        }
        if cmd == Command::Trigger {
            if position == Position::Target {
                self.phase = Phase::Waiting;
                self.waiting_last_advance = snap.advance;
                self.saw_pre_battle = obs.battle == 0;
                self.message = "Wait; no more inputs";
            } else {
                self.fail("A was not at target");
            }
            return false;
        }
        match position {
            Position::Target => {
                self.phase = Phase::Ready;
                self.message = "At target: manual A";
            }
            Position::PreTarget => {
                self.phase = Phase::PreTarget;
                self.message = "Manual L; verify READY";
            }
            Position::Before => {
                if cmd == Command::Arm && self.phase == Phase::Candidate {
                    self.phase = Phase::Stepping;
                }
                if self.phase == Phase::Stepping {
                    if self.last_step_advance == Some(snap.advance) {
                        self.stalled += 1;
                        if self.stalled > 120 {
                            self.fail("No RNG progress");
                            return false;
                        }
                    } else {
                        self.stalled = 0;
                    }
                    self.last_step_advance = Some(snap.advance);
                    self.message = "Auto step; B cancels";
                    // While paused, request one exact frame step.  While the
                    // game is running, the caller observes every presentation
                    // and pauses at PreTarget/Target instead.
                    return paused;
                }
            }
            Position::Invalid => unreachable!(),
        }
        false
    }
}
