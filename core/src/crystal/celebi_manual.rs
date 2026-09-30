//! Pure state machine for the invisible, fixed-DV manual resolver release.
//! It never requests controller input, pauses, resets, or draws a UI.

/// The public releases support the Old 3DS clock and both New 3DS clock modes.
/// Old 3DS hardware is reported as 268 MHz by the native shell when Luma's
/// New-3DS-only system-info query is unavailable.
pub const fn supported_cpu_mhz(cpu_mhz: u32) -> bool {
    cpu_mhz == 268 || cpu_mhz == 804
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ManualState {
    SeekPrompt,
    Ready,
    Running,
    Done,
    Failed,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ManualEvent {
    None,
    ArmResolver,
    BattleResult([u8; 2]),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BattleSample {
    pub battle: u8,
    pub battle_start: u8,
    pub battle_ended: u8,
    pub species: u8,
    pub level: u8,
    pub hp: u16,
    pub max_hp: u16,
    pub dvs: [u8; 2],
}

impl BattleSample {
    fn is_celebi(&self) -> bool {
        self.battle == 1
            && self.battle_start == 0
            && self.battle_ended == 0
            && self.species == 251
            && self.level == 30
            && self.hp > 0
            && self.hp <= self.max_hp
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ManualController {
    pub state: ManualState,
    saw_pre_battle: bool,
    stable_frames: u8,
    last_dvs: Option<[u8; 2]>,
}

impl ManualController {
    pub const fn new() -> Self {
        Self {
            state: ManualState::SeekPrompt,
            saw_pre_battle: false,
            stable_frames: 0,
            last_dvs: None,
        }
    }

    pub fn observe_prompt(&mut self, exact_prompt: bool, physical_a: bool) -> ManualEvent {
        if matches!(self.state, ManualState::Done | ManualState::Failed | ManualState::Running) {
            return ManualEvent::None;
        }
        if !exact_prompt {
            self.state = ManualState::SeekPrompt;
            return ManualEvent::None;
        }
        if self.state == ManualState::SeekPrompt {
            self.state = ManualState::Ready;
        }
        // Arm only after the A that displayed the prompt has been released.
        // This guarantees that the resolver is live before the player's final
        // physical A can be consumed by Crystal; it still never creates input.
        if !physical_a {
            self.state = ManualState::Running;
            self.saw_pre_battle = true;
            self.stable_frames = 0;
            self.last_dvs = None;
            ManualEvent::ArmResolver
        } else {
            ManualEvent::None
        }
    }

    pub fn observe_battle(&mut self, sample: BattleSample) -> ManualEvent {
        if self.state != ManualState::Running {
            return ManualEvent::None;
        }
        self.saw_pre_battle |= sample.battle == 0;
        if !sample.is_celebi() {
            self.stable_frames = 0;
            self.last_dvs = None;
            return ManualEvent::None;
        }
        if self.last_dvs == Some(sample.dvs) {
            self.stable_frames = self.stable_frames.saturating_add(1);
        } else {
            self.stable_frames = 0;
            self.last_dvs = Some(sample.dvs);
        }
        if self.saw_pre_battle && self.stable_frames >= 3 {
            ManualEvent::BattleResult(sample.dvs)
        } else {
            ManualEvent::None
        }
    }

    pub fn finish(&mut self, success: bool) {
        self.state = if success {
            ManualState::Done
        } else {
            ManualState::Failed
        };
    }

    pub const fn terminal(&self) -> bool {
        matches!(self.state, ManualState::Done | ManualState::Failed)
    }
}
