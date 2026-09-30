//! Automatic controller. Only observed screens authorize forward transitions.
//! Buttons are transient controller inputs, never writes to saves, RNG, or Pokemon data.
use super::celebi_session::{supported_cpu_mhz, Command, Observation, Phase, Session};

#[cfg(all(feature = "celebi_natural", feature = "celebi_resolver"))]
compile_error!("celebi_natural and celebi_resolver are mutually exclusive");

pub const SHINY_DV_PAIRS: [[u8; 2]; 8] = [
    [0x2a, 0xaa],
    [0x3a, 0xaa],
    [0x6a, 0xaa],
    [0x7a, 0xaa],
    [0xaa, 0xaa],
    [0xba, 0xaa],
    [0xea, 0xaa],
    [0xfa, 0xaa],
];

pub const fn natural_mode() -> bool {
    cfg!(feature = "celebi_natural")
}

pub const fn resolver_mode() -> bool {
    cfg!(feature = "celebi_resolver")
}

pub const fn build_mode_name() -> &'static str {
    if natural_mode() {
        "reset"
    } else if resolver_mode() {
        "rng"
    } else {
        "legacy_test"
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage {
    Title,
    Continue,
    Load,
    Shrine,
    Track,
    SelectDvs,
    NaturalDelay,
    NaturalA,
    Search,
    DirectA,
    Advance,
    ManualA,
    Battle,
    SaveResult,
    Reset,
    Shiny,
    Stopped,
}
#[derive(Clone, Copy, Debug, Default)]
pub struct Screen {
    pub wram_bank: u8,
    pub script_bank: u8,
    pub script_pos: u16,
    pub x: u8,
    pub y: u8,
    pub bank: u8,
    pub frame: u8,
    pub joy: u8,
    pub joy_pressed: u8,
    pub game_joy: u8,
    pub joy_last: u8,
    pub text_sampled: bool,
    pub no_save_menu: bool,
    pub menu: bool,
    pub cursor: u8,
    pub save_info: bool,
    pub world: bool,
    pub shrine: bool,
    pub dialog: bool,
    // Persistent script-position guard for the already-verified GS Ball text.
    // Unlike `gs`, this does not depend on rescanning the tilemap after a
    // stage transition resets the text cache.
    pub gs_position: bool,
    pub gs: bool,
    pub physical_a: bool,
    pub physical_x: bool,
    pub physical_y: bool,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Motion {
    Run,
    Pause,
    Step,
}

pub fn hide_hud(stage: Stage) -> bool {
    // During a fully automatic round the overlay has no actionable controls.
    // Show it only after a terminal result/fault; this leaves the frame budget
    // to Crystal throughout title, tracking, search, encounter and reset.
    !matches!(stage, Stage::SelectDvs | Stage::Shiny | Stage::Stopped)
}

pub fn text_scan_due(stage: Stage, _tick: u8, gs_position: bool) -> bool {
    match stage {
        // 0.2.1 sampled these transition screens on every callback and that
        // exact path passed the user's hardware test. Search/battle remain free
        // of tile scans, so restoring it does not reintroduce search lag.
        Stage::Title | Stage::Continue | Stage::Load => true,
        Stage::Shrine => gs_position,
        Stage::Track => _tick == 0 && gs_position,
        _ => false,
    }
}
#[derive(Clone, Copy, Debug)]
pub struct Output {
    pub motion: Motion,
    pub buttons: u8,
}

pub struct Bot {
    pub calibration: bool,
    pub stage: Stage,
    pub message: &'static str,
    pub session: Session,
    pub attempts: u32,
    pub last_miss: Option<[u8; 2]>,
    pub phase_target: u8,
    pub phase_samples: u8,
    pub learned_phase_target: u8,
    pub learned_outcome_mask: u64,
    pub selected_dv_index: u8,
    pub natural_delay_frames: u16,
    pub natural_delay_remaining: u16,
    pub natural_entropy_tag: u32,
    /// 0 = ordinary response, 1 = short hesitation, 2 = rare long pause.
    pub natural_timing_class: u8,
    polls: u32,
    pulses: u16,
    held: u8,
    pulse_start: u8,
    released: u8,
    ack: bool,
    pulse_polls: u16,
    cooldown: u8,
    last_frame: u8,
    reanchors: u8,
    physical_a_ready: bool,
    select_key_latch: u8,
    run_entropy: u64,
}
impl Bot {
    pub const fn new() -> Self {
        Self {
            calibration: false,
            stage: Stage::Title,
            message: "AUTO: waiting for title",
            session: Session::new(),
            attempts: 0,
            last_miss: None,
            phase_target: 1,
            phase_samples: 0,
            learned_phase_target: 0,
            learned_outcome_mask: 0,
            selected_dv_index: 7,
            natural_delay_frames: 0,
            natural_delay_remaining: 0,
            natural_entropy_tag: 0,
            natural_timing_class: 0,
            polls: 0,
            pulses: 0,
            held: 0,
            pulse_start: 0,
            released: 0,
            ack: false,
            pulse_polls: 0,
            cooldown: 0,
            last_frame: 0,
            reanchors: 0,
            physical_a_ready: false,
            select_key_latch: 0,
            run_entropy: 0,
        }
    }
    pub const fn calibration() -> Self {
        let mut bot = Self::new();
        bot.calibration = true;
        bot
    }
    pub fn active_phase_target(&self) -> u8 {
        if self.learned_phase_target != 0 {
            self.learned_phase_target
        } else {
            self.phase_target
        }
    }
    pub fn selected_candidate_dvs(&self) -> Option<[u8; 2]> {
        self.session
            .candidate
            .and_then(|candidate| candidate.selected_shiny_dvs(self.learned_outcome_mask))
    }
    pub fn selected_resolver_dvs(&self) -> [u8; 2] {
        SHINY_DV_PAIRS[self.selected_dv_index.min(7) as usize]
    }
    pub fn active_target_dvs(&self) -> Option<[u8; 2]> {
        if resolver_mode() {
            Some(self.selected_resolver_dvs())
        } else {
            self.selected_candidate_dvs()
        }
    }
    pub fn set_run_entropy(&mut self, entropy: u64) {
        self.run_entropy = entropy;
    }
    fn prepare_natural_delay(&mut self) {
        // SplitMix64 avalanche. Runtime refreshes run_entropy from an ARM11
        // system-tick sample taken at the visible GS Ball prompt. The attempt
        // number keeps soft-reset rounds distinct. Hidden VC RNG/DIV values do
        // not choose the response time; they remain read-only audit data.
        let mut z = self.run_entropy
            ^ (self.attempts as u64).wrapping_mul(0xD134_2543_DE82_EF95);
        z = z.wrapping_add(0x9E37_79B9_7F4A_7C15);
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^= z >> 31;
        // Human response times are concentrated around a typical response and
        // have a longer slow tail. A flat 1..=256-frame draw overrepresented
        // both near-instant presses and multi-second waits. Four independent
        // bytes form a compact bell-shaped centre (12..=30 GB frames); about
        // 9.8% of attempts add a short hesitation and about 0.4% use a rare
        // long pause. This is an explicitly disclosed human-like timing
        // profile, not a claim that software input is manual play.
        let centre_sum = (z as u8) as u16
            + ((z >> 8) as u8) as u16
            + ((z >> 16) as u8) as u16
            + ((z >> 24) as u8) as u16;
        let mut frames = 12 + centre_sum * 18 / 1020;
        let tail = (z >> 32) as u8;
        self.natural_timing_class = if tail == 0 {
            frames += 60 + ((z >> 40) as u8 % 91) as u16;
            2
        } else if tail < 26 {
            frames += 1 + ((z >> 40) as u8 % 30) as u16;
            1
        } else {
            0
        };
        self.natural_delay_frames = frames;
        self.natural_delay_remaining = self.natural_delay_frames;
        self.natural_entropy_tag = (z >> 32) as u32;
    }
    pub fn observe_phase_result(
        &mut self,
        delay: super::celebi_trace::NativePhaseDelayState,
    ) {
        if self.calibration || delay.failed || !delay.complete {
            return;
        }
        let matching = super::celebi_trace::matching_outcome_mask(
            self.session.candidate,
            self.session.result,
        )
        .unwrap_or(0);
        if self.learned_phase_target == 0 {
            if matching != 0 {
                self.learned_phase_target = self.phase_target;
                self.learned_outcome_mask = matching;
            } else {
                self.phase_samples = self.phase_samples.saturating_add(1);
                if self.phase_samples >= 64 {
                    self.stop("64 phases tried; no public branch");
                    return;
                }
                self.phase_target = self.phase_target % 64 + 1;
            }
        } else if matching & self.learned_outcome_mask == 0 {
            self.learned_phase_target = 0;
            self.learned_outcome_mask = 0;
            self.phase_samples = 0;
            self.phase_target = self.phase_target % 64 + 1;
        }
    }
    pub fn stop(&mut self, reason: &'static str) -> Output {
        self.stage = Stage::Stopped;
        self.message = reason;
        self.held = 0;
        Output {
            motion: Motion::Pause,
            buttons: 0,
        }
    }
    // Reset is allowed only after the runtime has independently accepted the
    // stable Celebi result (and, in a diagnostics build, any required audit
    // write).  Production builds keep this gate entirely in memory.
    pub fn authorize_verified_reset(&mut self, success: bool) {
        if self.stage != Stage::SaveResult {
            return;
        }
        if !success {
            self.stop("Result not committed; no reset");
        } else {
            self.enter(Stage::Reset, "MISS verified: resetting");
            self.held = 15;
            self.released = 0;
            self.ack = false;
            self.pulse_polls = 0;
        }
    }
    fn enter(&mut self, stage: Stage, message: &'static str) {
        self.stage = stage;
        self.message = message;
        self.polls = 0;
        self.pulses = 0;
        if stage == Stage::ManualA {
            self.physical_a_ready = false;
        }
    }
    fn pulse(&mut self, screen: Screen) {
        if self.held == 0 && self.released == 0 && self.cooldown == 0 {
            self.held = 1;
            self.pulse_start = screen.frame;
            self.ack = false;
            self.pulse_polls = 0;
            self.pulses += 1;
        }
    }
    #[inline(never)]
    pub fn tick(
        &mut self,
        obs: Observation,
        s: Screen,
        paused: bool,
        input_ok: bool,
        cancel: bool,
    ) -> Output {
        if cancel {
            return self.stop("Cancelled: inputs released");
        }
        if matches!(self.stage, Stage::Stopped | Stage::Shiny) {
            return Output {
                motion: Motion::Pause,
                buttons: 0,
            };
        }
        if !input_ok {
            return self.stop("Input hook not verified");
        }
        // The title may be reached before the user selects the required clock.
        // Do not send input until the latest requested clock is confirmed.
        if !supported_cpu_mhz(obs.cpu_mhz) {
            if self.stage == Stage::Title && self.attempts == 0 && self.pulses == 0 {
                self.message = "Waiting for supported CPU mode";
                return Output {
                    motion: Motion::Run,
                    buttons: 0,
                };
            }
            return self.stop("CPU changed; stopped");
        }
        self.polls += 1;
        let poll_limit = match self.stage {
            Stage::Advance => 262144,
            Stage::ManualA | Stage::SelectDvs => 360000,
            // A freshly reset DIV tracker may need to observe almost a full
            // 0x4000-entry cycle before one of the identifying irregular
            // increments appears.  The previous 7200-poll guard could stop a
            // healthy run before that witness arrived.
            Stage::Track => 20000,
            _ => 7200,
        };
        if self.polls > poll_limit {
            return self.stop("Stage timeout; no reset");
        }
        let delta = s.frame.wrapping_sub(self.last_frame);
        self.last_frame = s.frame;
        let stepped_battle_frame = self.stage == Stage::Battle && paused && delta != 0;
        if !paused || stepped_battle_frame {
            self.cooldown = self.cooldown.saturating_sub(delta.min(8));
            if self.held == 1 {
                self.pulse_polls += 1;
                self.ack |= s.joy & 1 != 0;
                // Keep A until Crystal's input mirror consumes it, then retain
                // it for two GB frames. The final encounter is delivered by
                // the hardware-verified JOYP hook, which also records the exact
                // advance of the first action-button read.
                let consumed = self.stage == Stage::Battle || s.game_joy & 1 != 0;
                if self.ack && consumed && s.frame.wrapping_sub(self.pulse_start) >= 2 {
                    self.held = 0;
                    self.released = 1;
                    self.cooldown = 12;
                } else if self.pulse_polls
                    > if self.ack && self.stage != Stage::Battle {
                        600
                    } else {
                        120
                    }
                {
                    return self.stop(if self.ack {
                        "Menu did not consume A"
                    } else {
                        "A not acknowledged"
                    });
                }
            }
            if self.stage == Stage::Reset && self.held == 15 {
                self.pulse_polls = self.pulse_polls.saturating_add(1);
                if s.joy & 15 == 15 {
                    // UpdateJoypad stores hJoypadDown before jumping to Reset.
                    // Release immediately so the new boot cannot reset again.
                    self.ack = true;
                    self.held = 0;
                    self.released = 15;
                } else if self.pulse_polls > 120 {
                    return self.stop("Reset input not acknowledged");
                }
            }
            if self.released != 0 && (s.joy | s.game_joy) & self.released == 0 {
                self.released = 0;
            }
        }
        let pulse_limit = if self.stage == Stage::Title { 256 } else { 32 };
        if self.pulses > pulse_limit {
            return self.stop(match self.stage {
                Stage::Title => "Title not advancing",
                Stage::Continue => "Continue not advancing",
                Stage::Load => "Save load not advancing",
                Stage::Shrine => "Shrine not advancing",
                _ => "Too many A pulses",
            });
        }
        if self.stage == Stage::DirectA {
            if !s.gs_position {
                return self.stop("GS text lost before fast A");
            }
            if s.wram_bank & 7 > 1 {
                return self.stop("WRAM unavailable for fast A");
            }
            if resolver_mode() {
                if !self.session.trigger_unmodified_now(obs) {
                    return self.stop(self.session.message);
                }
            } else {
                let anchor = self.session.anchor;
                if anchor.is_none() || obs.rng != anchor {
                    return self.stop("Fast anchor changed; no A");
                }
                if self.selected_candidate_dvs().is_none() {
                    return self.stop("Candidate has no selected shiny DVs");
                }
                if !self.session.trigger_selected_candidate_now(obs) {
                    return self.stop(self.session.message);
                }
            }
            self.enter(Stage::Battle, "JOYP A: resolving selected DVs");
            self.held = 1;
            self.pulse_start = s.frame;
            self.released = 0;
            self.ack = false;
            self.pulse_polls = 0;
            self.pulses = 1;
            self.attempts += 1;
            return Output {
                motion: Motion::Run,
                buttons: 1,
            };
        }
        if self.stage == Stage::NaturalA {
            if !s.gs_position {
                return self.stop("GS text lost before natural A");
            }
            if s.wram_bank & 7 > 1 {
                return self.stop("WRAM unavailable for natural A");
            }
            if !self.session.trigger_unmodified_now(obs) {
                return self.stop(self.session.message);
            }
            self.enter(Stage::Battle, "Reset JOYP A: game chooses DVs");
            self.held = 1;
            self.pulse_start = s.frame;
            self.released = 0;
            self.ack = false;
            self.pulse_polls = 0;
            self.pulses = 1;
            self.attempts += 1;
            return Output { motion: Motion::Run, buttons: 1 };
        }
        if self.stage == Stage::ManualA {
            let target = self.session.candidate.map(|candidate| candidate.snapshot);
            if target.is_none() || obs.rng != target {
                return self.stop("Auto target changed; no A");
            }
            if !self.session.trigger_public_target(obs) {
                return self.stop(self.session.message);
            }
            self.enter(Stage::Battle, "Auto JOYP A: phase tracing");
            self.held = 1;
            self.pulse_start = s.frame;
            self.released = 0;
            self.ack = false;
            self.pulse_polls = 0;
            self.pulses = 1;
            self.attempts += 1;
            return Output {
                motion: Motion::Run,
                buttons: 1,
            };
        }
        // D000-DFFF refers to the currently selected WRAM bank, not always
        // bank 1. Graphics routines legitimately select bank 6 across frames.
        // HRAM input acknowledgements above remain valid in all banks. Do not
        // start another pulse, inspect DVs, or authorize a reset from this data.
        if s.wram_bank & 7 > 1 {
            self.session.invalidate_battle_sample();
            if self.stage == Stage::Advance {
                // The pause can land inside a graphics routine. Preserve the
                // candidate and step until bank 1 returns; Track then validates
                // every intervening RNG advance before a trigger is possible.
                self.reanchors += 1;
                if self.reanchors > 8 {
                    return self.stop("WRAM did not restore; no trigger");
                }
                self.held = 0;
                return Output {
                    // Stop a continuous approach before stepping through the
                    // bank switch.  This keeps the target fail-closed without
                    // returning to the slow pause/redraw loop every frame.
                    motion: if paused { Motion::Step } else { Motion::Pause },
                    buttons: 0,
                };
            } else if self.stage == Stage::Search {
                // Frozen search only needs HRAM RNG + plugin trackers.
                // The Session deliberately skips banked environment reads.
            } else if self.stage == Stage::SaveResult {
                return self.stop("WRAM unavailable at target");
            } else if matches!(self.stage, Stage::Title | Stage::Continue) {
                // Intro/menu decisions use bank-0 tilemap and HRAM input. If a
                // save summary is visible, wait for bank 1 before proceeding.
                if s.save_info {
                    return Output {
                        motion: Motion::Run,
                        buttons: 0,
                    };
                }
            } else if self.stage == Stage::Battle {
                // Match the community physical-A procedure after the press:
                // no pause, step, overlay work, or virtual button injection.
                return Output {
                    motion: Motion::Run,
                    buttons: 0,
                };
            } else {
                return Output {
                    motion: Motion::Run,
                    buttons: self.held,
                };
            }
        }
        let mut motion = Motion::Run;
        match self.stage {
            Stage::Title => {
                if s.no_save_menu {
                    return self.stop("No Continue; no inputs");
                } else if s.save_info {
                    self.held = 0;
                    self.released = 1;
                    self.enter(Stage::Load, "Save info: confirming A");
                } else if s.menu {
                    self.held = 0;
                    self.released = 1;
                    self.enter(Stage::Continue, "Continue menu observed");
                } else if s.world {
                    return self.stop("Expected title, got world");
                } else if s.bank == 1 || s.bank == 0x39 {
                    // This is the 0.2.1 title path that passed on hardware:
                    // CrystalIntro is bank 39; the title loop is bank 01.
                    self.pulse(s);
                }
            }
            Stage::Continue => {
                if s.save_info {
                    self.held = 0;
                    self.released = 1;
                    self.enter(Stage::Load, "Confirming saved game");
                } else if s.menu {
                    // Text can be drawn before SetUpMenu initializes cursor.
                    if s.cursor == 0 {
                        return Output {
                            motion: Motion::Run,
                            buttons: 0,
                        };
                    }
                    if s.cursor != 1 {
                        return self.stop("Continue cursor mismatch");
                    }
                    self.pulse(s);
                }
            }
            Stage::Load => {
                if s.world {
                    if !s.shrine || obs.map != (3, 52) {
                        return self.stop("Save not at shrine start");
                    }
                    self.enter(Stage::Shrine, "Advancing shrine dialog");
                } else if s.save_info {
                    self.pulse(s);
                }
            }
            Stage::Shrine => {
                if obs.map != (3, 52) || obs.battle != 0 {
                    return self.stop("Shrine context changed");
                }
                if s.gs {
                    self.held = 0;
                    self.released = 1;
                    self.enter(
                        Stage::Track,
                        if natural_mode() {
                            "GS text: timing response"
                        } else {
                            "GS text: waiting for DIV"
                        },
                    );
                } else if s.dialog || (s.world && s.shrine) {
                    self.pulse(s);
                }
            }
            Stage::Track => {
                if !s.gs {
                    return self.stop("GS text lost; no reset");
                }
                self.held = 0;
                let input_released = s.joy & 15 == 0 && self.released == 0;
                if natural_mode() && input_released {
                    motion = Motion::Pause;
                    self.prepare_natural_delay();
                    self.enter(Stage::NaturalDelay, "Reset timing: original RNG");
                } else if resolver_mode() && input_released {
                    // The retired selection build only needed the verified GS Ball boundary.
                    // Its three direct Random outputs are supplied after the
                    // player confirms a DV pair, so waiting for a complete DIV
                    // tracker snapshot here only adds a long visible delay.
                    motion = Motion::Pause;
                    self.select_key_latch = 0;
                    self.enter(Stage::SelectDvs, "X/Y choose DVs; A confirms");
                } else if obs.rng.is_some() && input_released {
                    motion = Motion::Pause;
                    self.enter(Stage::Search, "Paused: auto search");
                }
            }
            Stage::SelectDvs => {
                self.held = 0;
                motion = Motion::Pause;
                if !s.gs_position {
                    return self.stop("GS text lost before DV selection");
                }
                let keys = (s.physical_a as u8)
                    | ((s.physical_x as u8) << 1)
                    | ((s.physical_y as u8) << 2);
                let pressed = keys & !self.select_key_latch;
                self.select_key_latch = keys;
                if pressed & 2 != 0 {
                    self.selected_dv_index = (self.selected_dv_index + 1) & 7;
                }
                if pressed & 4 != 0 {
                    self.selected_dv_index = self.selected_dv_index.wrapping_sub(1) & 7;
                }
                if pressed & 1 != 0 {
                    self.enter(Stage::DirectA, "Selected DVs: preparing resolver A");
                }
            }
            Stage::NaturalDelay => {
                self.held = 0;
                if !s.gs_position {
                    return self.stop("GS text lost during natural delay");
                }
                if paused {
                    motion = Motion::Run;
                } else {
                    let elapsed = delta.max(1) as u16;
                    self.natural_delay_remaining =
                        self.natural_delay_remaining.saturating_sub(elapsed);
                    if self.natural_delay_remaining == 0 {
                        self.enter(Stage::NaturalA, "Reset delay complete: preparing A");
                        motion = Motion::Pause;
                    } else {
                        motion = Motion::Run;
                    }
                }
            }
            Stage::Search => {
                self.held = 0;
                motion = Motion::Pause;
                // Track already verified the exact text before pausing. The
                // stage change clears the tile cache, so only the still-live
                // script position is a valid guard while searching.
                if s.wram_bank & 7 <= 1 && !s.gs_position {
                    return self.stop("GS text lost; no reset");
                }
                if paused {
                    // Absolute nearest Advance wins across every valid Gen II
                    // shiny Attack DV. A learned execution branch may break a
                    // tie inside that Advance, but must never skip it.
                    self.session.set_required_outcome_mask(u64::MAX);
                    let cmd = if self.session.phase == Phase::Idle {
                        Command::Search
                    } else {
                        Command::None
                    };
                    self.session.tick(obs, true, cmd);
                    if self.session.phase == Phase::Candidate {
                        // The public search chooses the nearest shiny Advance
                        // only to select its exact DV pair.  The validated
                        // direct resolver no longer needs to wait for that
                        // natural state: consume A from this frozen, verified
                        // GS Ball boundary and produce the selected pair.
                        self.enter(Stage::DirectA, "Nearest DVs: immediate A");
                    }
                }
            }
            Stage::Advance => {
                if !s.gs_position {
                    return self.stop("GS text lost; no reset");
                }
                self.reanchors = 0;
                let cmd = if paused && self.session.phase == Phase::Candidate {
                    Command::Arm
                } else {
                    Command::None
                };
                let _step = self.session.tick(obs, paused, cmd);
                match self.session.phase {
                    Phase::Searching => {
                        // A live/model difference invalidates the old target.
                        // Keep the game frozen and let Search recompute from
                        // the current verified snapshot automatically.
                        self.enter(Stage::Search, "RNG shifted: re-searching");
                        motion = Motion::Pause;
                    }
                    Phase::Ready if paused => {
                        // Ready is the automatic arm point one VBlank before
                        // the public target. The next paused callback validates
                        // Candidate.snapshot and arms the automatic JOYP A.
                        self.enter(Stage::ManualA, "Stepping to public target");
                        motion = Motion::Step;
                    }
                    Phase::Ready => motion = Motion::Pause,
                    Phase::PreTarget => {
                        motion = if paused { Motion::Step } else { Motion::Pause };
                    }
                    Phase::Stepping => {
                        // Run at normal game speed while validating every live
                        // RNG observation.  The next callback pauses exactly at
                        // PreTarget/Target; overshoot or desync cannot trigger A.
                        motion = Motion::Run;
                    }
                    _ => motion = Motion::Pause,
                }
            }
            Stage::Battle => {
                self.session.tick(obs, false, Command::None);
                if self.session.phase == Phase::Shiny {
                    let expected = if resolver_mode() {
                        Some(self.selected_resolver_dvs())
                    } else if natural_mode() {
                        self.session.result
                    } else {
                        self.selected_candidate_dvs()
                    };
                    if self.session.result != expected {
                        return self.stop("Shiny differs from selected candidate");
                    }
                    self.stage = Stage::Shiny;
                    self.message = "SHINY: stopped, keep game";
                    self.held = 0;
                    motion = Motion::Pause;
                } else if self.session.phase == Phase::Miss {
                    // Recheck fresh current data before discarding this encounter.
                    if self.session.result != Some(obs.dvs) || obs.battle != 1 || obs.species != 251 {
                        return self.stop("Result changed; no reset");
                    }
                    if super::celebi_rng::shiny(obs.dvs[0], obs.dvs[1]) {
                        return self.stop("Shiny guard; no reset");
                    }
                    let predicted = self
                        .session
                        .candidate
                        .map(|c| c.outcomes().contains(&obs.dvs))
                        .unwrap_or(false);
                    self.last_miss = self.session.result;
                    if self.calibration {
                        return self.stop("CAL sample ready; no reset");
                    }
                    let attempt_limit = if natural_mode() { 32_768 } else { 1_024 };
                    if self.attempts >= attempt_limit {
                        return self.stop("Attempt limit reached");
                    }
                    self.enter(
                        Stage::SaveResult,
                        if predicted {
                            "MISS verified: reset pending"
                        } else {
                            "MISS verified: reset pending"
                        },
                    );
                    self.held = 0;
                    motion = Motion::Pause;
                } else if self.session.phase == Phase::Waiting {
                    motion = Motion::Run;
                }
            }
            Stage::Reset => {
                // A+B+Select+Start reaches the game's own UpdateJoypad reset.
                // No reset is attempted for faults, timeouts, or missing DVs.
                if self.ack && self.held == 0 && !s.world && (s.bank == 1 || s.bank == 0x39) {
                    self.held = 0;
                    self.cooldown = 12;
                    self.session = Session::new();
                    self.reanchors = 0;
                    self.enter(Stage::Title, "Reset observed: title");
                } else if self.polls > 180 {
                    return self.stop("Reset unconfirmed");
                }
            }
            Stage::SaveResult => {
                self.held = 0;
                motion = Motion::Pause;
            }
            Stage::DirectA | Stage::ManualA | Stage::NaturalA => motion = Motion::Pause,
            Stage::Shiny | Stage::Stopped => {}
        }
        if self.session.phase == Phase::Fault && !matches!(self.stage, Stage::Reset | Stage::Title) {
            return self.stop(self.session.message);
        }
        Output {
            motion,
            buttons: self.held,
        }
    }
}

pub fn joyp_with_buttons(raw: u8, buttons: u8) -> u8 {
    // GB JOYP bit 5 low selects A/B/Select/Start, which are active-low.
    if raw & 0x20 == 0 {
        raw & !(buttons & 15)
    } else {
        raw
    }
}

pub fn joyp_buttons_for_stage(stage: Stage, buttons: u8) -> u8 {
    // The final encounter A uses the JOYP path validated by all 241 completed
    // 0.2.20 attempts. Native HID is not consumed here by Crystal VC.
    match stage {
        Stage::Title
        | Stage::Continue
        | Stage::Load
        | Stage::Shrine
        | Stage::Battle
        | Stage::Reset => buttons,
        _ => 0,
    }
}

pub fn host_buttons_for_stage(stage: Stage, buttons: u8) -> u8 {
    // Native HID remains useful for menus and reset. 0.2.16 and 0.2.22 both
    // proved that this PAD-ring write is not consumed by Crystal for final A.
    // Battle A stays JOYP-only; the two earlier HID experiments were not
    // consumed by Crystal VC.
    match stage {
        Stage::Title | Stage::Continue | Stage::Load | Stage::Shrine | Stage::Reset => buttons,
        _ => 0,
    }
}

pub fn prepare_read_return(regs: &mut [u32], original: u32, override_value: Option<u8>) {
    let return_pc = regs[13];
    regs[14] = return_pc;
    if let Some(value) = override_value {
        regs[0] = value as u32;
        regs[13] = return_pc;
    } else {
        regs[13] = original;
    }
}

pub fn tile_contains(tiles: &[u8], ascii: &[u8]) -> bool {
    if ascii.is_empty() || ascii.len() > 20 {
        return false;
    }
    tiles.chunks_exact(20).any(|row| {
        row.windows(ascii.len()).any(|s| {
            s.iter().zip(ascii).all(|(&tile, &ch)| {
                tile == match ch {
                    b'A'..=b'Z' => ch - b'A' + 0x80,
                    b'a'..=b'z' => ch - b'a' + 0xa0,
                    b' ' => 0x7f,
                    b'.' => 0xe8,
                    _ => 0xff,
                }
            })
        })
    })
}
