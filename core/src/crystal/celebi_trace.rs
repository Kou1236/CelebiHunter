//! Bounded in-memory evidence. No allocation or disk IO during sampling.
use super::celebi_auto::{Bot, Screen, Stage};
use super::celebi_rng::Snapshot;
use super::celebi_session::Observation;
#[cfg(not(test))]
use alloc::string::String;
use core::fmt::Write;

pub const RNG_TRACE_CAPACITY: usize = 1024;
pub const PRE_TRIGGER_TIMING_CAPACITY: usize = 512;
pub const HOST_PHASE_WINDOW_SIZE: usize = 64;
pub const HOST_PHASE_SAMPLE_CAPACITY: usize = 12;
pub const HOST_DIV_TRACE_CAPACITY: usize = 2;
pub const FINAL_PHASE_TRACE_CAPACITY: usize = 12;
pub const EXECUTION_GAP_TRACE_CAPACITY: usize = 24;
pub const EXECUTION_GAP_TAIL_CAPACITY: usize = 32;
pub const DIRECT_BOUNDARY_TRACE_CAPACITY: usize = 4;
pub const DIRECT_BOUNDARY_TAIL_PER_CALL: usize = 8;
pub const DIRECT_BOUNDARY_TAIL_CAPACITY: usize =
    DIRECT_BOUNDARY_TRACE_CAPACITY * DIRECT_BOUNDARY_TAIL_PER_CALL;
pub const DIRECT_RNG_RESOLVER_CAPACITY: usize = 3;
pub const EXECUTION_GAP_CHECKPOINT_STRIDE: u32 = 512;
pub const FINAL_PHASE_CHECKPOINT_ADD: u8 = 0;
pub const FINAL_PHASE_CHECKPOINT_SUB: u8 = 1;
pub const FINAL_PHASE_VBLANK_ADD: u8 = 2;
pub const FINAL_PHASE_VBLANK_SUB: u8 = 3;
pub const FINAL_PHASE_DIRECT_ADD: u8 = 4;
pub const FINAL_PHASE_DIRECT_SUB: u8 = 5;
pub const HOST_PHASE_TRIGGER: u8 = 0;
pub const HOST_PHASE_DIRECT_ADD: u8 = 1;
pub const HOST_PHASE_DIRECT_SUB: u8 = 2;
/// Guest PC immediately after the bank switch and before `call _BattleRandom`
/// in English Crystal 1.1. AUTO 0.2.43 captured this as the final cycle
/// callback before the first direct `Random` FF04 read.
pub const PRE_DIRECT_TAIL_PC: u16 = 0x2FA5;

pub const fn is_pre_direct_tail_control_point(pc: u16) -> bool {
    pc == PRE_DIRECT_TAIL_PC
}
pub const RNG_SOURCE_VBLANK: u8 = 0;
pub const RNG_SOURCE_DIRECT: u8 = 1;
const UNKNOWN_STATE: u32 = 0x1_0000;
const DIV_PHASE_MODULUS: u16 = 0x4000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DirectRngResolverSample {
    pub ordinal: u8,
    pub pre_state: u16,
    pub state_before_sub: u16,
    pub natural_add_div: u8,
    pub natural_sub_div: u8,
    pub returned_sub_div: u8,
    pub desired_output: u8,
    pub carry_out: bool,
}

impl DirectRngResolverSample {
    pub const fn empty() -> Self {
        Self {
            ordinal: 0,
            pre_state: 0,
            state_before_sub: 0,
            natural_add_div: 0,
            natural_sub_div: 0,
            returned_sub_div: 0,
            desired_output: 0,
            carry_out: false,
        }
    }
}

/// Controls only the three direct `Random` calls used by a normal wild-mon
/// initialization: held-item selection, first DV byte, second DV byte.  The
/// add-side FF04 read remains natural. At the matching sub-side read we return
/// the divider byte which makes Crystal's own `sbc` produce 00 followed by the
/// exact shiny DV pair selected by the nearest public-model candidate. The
/// first byte forces the one-call no-item branch; the final two are the DVs.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DirectRngResolver {
    pub active: bool,
    pub failed: bool,
    pub failure_code: u8,
    ordinal: u8,
    pending: bool,
    pre_state: u16,
    natural_add_div: u8,
    desired_outputs: [u8; DIRECT_RNG_RESOLVER_CAPACITY],
    samples: [DirectRngResolverSample; DIRECT_RNG_RESOLVER_CAPACITY],
    sample_count: usize,
}

impl DirectRngResolver {
    pub const fn new() -> Self {
        Self {
            active: false,
            failed: false,
            failure_code: 0,
            ordinal: 0,
            pending: false,
            pre_state: 0,
            natural_add_div: 0,
            desired_outputs: [0x00, 0xaa, 0xaa],
            samples: [DirectRngResolverSample::empty(); DIRECT_RNG_RESOLVER_CAPACITY],
            sample_count: 0,
        }
    }

    pub fn start(&mut self) {
        self.start_for_dvs([0xaa, 0xaa]);
    }

    pub fn start_for_dvs(&mut self, dvs: [u8; 2]) {
        *self = Self::new();
        if dvs[1] != 0xaa || dvs[0] & 0x2f != 0x2a {
            self.failed = true;
            self.failure_code = 4;
            return;
        }
        self.desired_outputs = [0x00, dvs[0], dvs[1]];
        self.active = true;
    }

    pub fn stop(&mut self) {
        self.active = false;
        self.pending = false;
    }

    pub fn observe_add(&mut self, pre_state: u16, natural_div: u8) {
        if !self.active || self.failed {
            return;
        }
        if self.pending || self.ordinal as usize >= DIRECT_RNG_RESOLVER_CAPACITY {
            self.fail(1);
            return;
        }
        self.ordinal += 1;
        self.pending = true;
        self.pre_state = pre_state;
        self.natural_add_div = natural_div;
    }

    pub fn resolve_sub(&mut self, state_before_sub: u16, natural_div: u8) -> Option<u8> {
        if !self.active || self.failed {
            return None;
        }
        if !self.pending || self.ordinal == 0 || self.ordinal as usize > DIRECT_RNG_RESOLVER_CAPACITY {
            self.fail(2);
            return None;
        }

        let pre_add = (self.pre_state >> 8) as u8;
        let post_add = (state_before_sub >> 8) as u8;
        let carry_out = match infer_adc_carry_out(pre_add, self.natural_add_div, post_add) {
            Some(value) => value,
            None => {
                self.fail(3);
                return None;
            }
        };
        let desired_output = self.desired_outputs[self.ordinal as usize - 1];
        let old_sub = state_before_sub as u8;
        let returned_sub_div = divider_for_sbc_output(old_sub, carry_out, desired_output);
        self.samples[self.sample_count] = DirectRngResolverSample {
            ordinal: self.ordinal,
            pre_state: self.pre_state,
            state_before_sub,
            natural_add_div: self.natural_add_div,
            natural_sub_div: natural_div,
            returned_sub_div,
            desired_output,
            carry_out,
        };
        self.sample_count += 1;
        self.pending = false;
        if self.ordinal as usize == DIRECT_RNG_RESOLVER_CAPACITY {
            self.active = false;
        }
        Some(returned_sub_div)
    }

    fn fail(&mut self, code: u8) {
        self.active = false;
        self.failed = true;
        self.failure_code = code;
        self.pending = false;
    }

    pub fn len(&self) -> usize {
        self.sample_count
    }

    pub fn get(&self, index: usize) -> Option<DirectRngResolverSample> {
        self.samples.get(index).copied().filter(|_| index < self.sample_count)
    }
}

/// Infer the carry left by `adc b` from the add byte before and after the
/// instruction.  Both possible incoming-carry cases are checked explicitly;
/// any impossible state fails closed in the live resolver.
pub fn infer_adc_carry_out(pre_add: u8, div: u8, post_add: u8) -> Option<bool> {
    for carry_in in [0u16, 1u16] {
        let sum = pre_add as u16 + div as u16 + carry_in;
        if sum as u8 == post_add {
            return Some(sum > 0xff);
        }
    }
    None
}

pub const fn divider_for_sbc_output(old_sub: u8, carry_out: bool, desired: u8) -> u8 {
    old_sub
        .wrapping_sub(desired)
        .wrapping_sub(carry_out as u8)
}

/// A bounded, read-only snapshot of the VC host state. Each direct Random call
/// produces an add and a sub sample. The three windows are centered on the
/// current divider storage, its pointer slot, and the emulated PC storage.
#[derive(Clone, Copy)]
pub struct HostPhaseSample {
    pub kind: u8,
    pub direct_ordinal: u8,
    pub valid_mask: u8,
    pub pc: u16,
    pub advance: u32,
    pub frame: u8,
    pub cycle: u32,
    pub state: u16,
    pub div: u8,
    pub div_storage_ptr: u32,
    pub div_window_base: u32,
    pub div_window: [u8; HOST_PHASE_WINDOW_SIZE],
    pub pointer_window_base: u32,
    pub pointer_window: [u8; HOST_PHASE_WINDOW_SIZE],
    pub pc_window_base: u32,
    pub pc_window: [u8; HOST_PHASE_WINDOW_SIZE],
    pub arm_regs: [u32; 15],
}

impl HostPhaseSample {
    pub const fn empty() -> Self {
        Self {
            kind: HOST_PHASE_TRIGGER,
            direct_ordinal: 0,
            valid_mask: 0,
            pc: 0,
            advance: 0,
            frame: 0,
            cycle: 0,
            state: 0,
            div: 0,
            div_storage_ptr: 0,
            div_window_base: 0,
            div_window: [0; HOST_PHASE_WINDOW_SIZE],
            pointer_window_base: 0,
            pointer_window: [0; HOST_PHASE_WINDOW_SIZE],
            pc_window_base: 0,
            pc_window: [0; HOST_PHASE_WINDOW_SIZE],
            arm_regs: [0; 15],
        }
    }
}

pub struct HostPhaseTrace {
    samples: [Option<HostPhaseSample>; HOST_PHASE_SAMPLE_CAPACITY],
    count: usize,
    next_direct_ordinal: u8,
    pub overflow: bool,
}

impl HostPhaseTrace {
    pub const fn new() -> Self {
        Self {
            samples: [None; HOST_PHASE_SAMPLE_CAPACITY],
            count: 0,
            next_direct_ordinal: 0,
            overflow: false,
        }
    }

    pub fn reset(&mut self) {
        self.samples.fill(None);
        self.count = 0;
        self.next_direct_ordinal = 0;
        self.overflow = false;
    }

    pub fn record_trigger(&mut self, mut sample: HostPhaseSample) {
        sample.kind = HOST_PHASE_TRIGGER;
        sample.direct_ordinal = 0;
        self.push(sample);
    }

    pub fn record_direct_add(&mut self, mut sample: HostPhaseSample) {
        self.next_direct_ordinal = self.next_direct_ordinal.saturating_add(1);
        sample.kind = HOST_PHASE_DIRECT_ADD;
        sample.direct_ordinal = self.next_direct_ordinal;
        self.push(sample);
    }

    pub fn record_direct_sub(&mut self, mut sample: HostPhaseSample) {
        sample.kind = HOST_PHASE_DIRECT_SUB;
        sample.direct_ordinal = self.next_direct_ordinal;
        self.push(sample);
    }

    fn push(&mut self, sample: HostPhaseSample) {
        if self.count == self.samples.len() {
            self.overflow = true;
            return;
        }
        self.samples[self.count] = Some(sample);
        self.count += 1;
    }

    pub fn len(&self) -> usize {
        self.count
    }

    pub fn get(&self, index: usize) -> Option<HostPhaseSample> {
        self.samples.get(index).copied().flatten()
    }
}

/// Minimal host-divider observation made at each VBlank after the final A and
/// stopped before the first direct `Random` call.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct HostDivSample {
    pub kind: u8,
    pub advance: u32,
    pub cycle: u32,
    pub state: u16,
    pub storage: u32,
    pub pc: u16,
    pub frame: u8,
    pub pc_window_base: u32,
    pub pc_window: [u8; HOST_PHASE_WINDOW_SIZE],
    pub arm_regs: [u32; 15],
}

impl HostDivSample {
    pub const fn empty() -> Self {
        Self {
            kind: 0,
            advance: 0,
            cycle: 0,
            state: 0,
            storage: 0,
            pc: 0,
            frame: 0,
            pc_window_base: 0,
            pc_window: [0; HOST_PHASE_WINDOW_SIZE],
            arm_regs: [0; 15],
        }
    }
}

pub struct HostDivTrace {
    samples: [HostDivSample; HOST_DIV_TRACE_CAPACITY],
    count: usize,
    sealed: bool,
    pub overflow: bool,
}

impl HostDivTrace {
    pub const fn new() -> Self {
        Self {
            samples: [HostDivSample::empty(); HOST_DIV_TRACE_CAPACITY],
            count: 0,
            sealed: false,
            overflow: false,
        }
    }

    pub fn reset(&mut self) {
        self.samples.fill(HostDivSample::empty());
        self.count = 0;
        self.sealed = false;
        self.overflow = false;
    }

    pub fn record(&mut self, sample: HostDivSample) {
        if self.sealed {
            return;
        }
        if self.count == self.samples.len() {
            self.overflow = true;
            return;
        }
        self.samples[self.count] = sample;
        self.count += 1;
    }

    pub fn seal(&mut self) {
        self.sealed = true;
    }

    pub fn len(&self) -> usize {
        self.count
    }

    pub fn get(&self, index: usize) -> Option<HostDivSample> {
        self.samples.get(index).copied().filter(|_| index < self.count)
    }

    pub fn sealed(&self) -> bool {
        self.sealed
    }
}

/// Scalar-only evidence around the final VBlank and direct Celebi RNG calls.
/// The FF04 byte is the value returned by the original VC read function and
/// passed back to Crystal. No guessed pointer or host-memory window is stored.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeTimerState {
    pub readable: bool,
    pub last_instruction_cycles: u32,
    pub guest_cycle_total: u32,
    pub div_countdown: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativePhaseDelayState {
    pub requested: u8,
    pub pending: u8,
    pub applied: u8,
    /// 0 = never armed, 1 = legacy JOYP edge, 2 = final VBlank SUB read.
    pub arm_source: u8,
    /// 0 = none, 1 = invalid instruction delta, 2 = timer unreadable,
    /// 3 = invalid countdown, 4 = invalid target, 5 = writeback mismatch,
    /// 6 = encounter completed before the controller completed.
    pub failure_code: u8,
    pub countdown_before: u8,
    pub input_delta: u8,
    pub written_cycles: u8,
    pub readback_cycles: u8,
    pub armed: bool,
    pub complete: bool,
    pub failed: bool,
}

impl NativePhaseDelayState {
    pub const fn new() -> Self {
        Self {
            requested: 0,
            pending: 0,
            applied: 0,
            arm_source: 0,
            failure_code: 0,
            countdown_before: 0,
            input_delta: 0,
            written_cycles: 0,
            readback_cycles: 0,
            armed: false,
            complete: false,
            failed: false,
        }
    }
}

/// Add only enough extra M-cycles to keep one interpreter instruction at or
/// below 64 cycles. Repeated calls consume the complete 0..63-cycle request
/// through the VC's original timer-update path.
pub fn phase_delay_chunk(original: u32, pending: u8) -> Option<(u32, u8, u8)> {
    if original == 0 || original > 64 {
        return None;
    }
    let extra = (64 - original).min(pending as u32) as u8;
    Some((original + extra as u32, pending - extra, extra))
}

pub fn advance_native_countdown(value: u8, cycles: u32) -> Option<u8> {
    if !(1..=64).contains(&value) {
        return None;
    }
    Some((((value as u32 - 1).wrapping_sub(cycles % 64)) % 64 + 1) as u8)
}

pub fn phase_delay_to_target(current: u8, original: u32, target: u8) -> Option<u8> {
    if !(1..=64).contains(&target) {
        return None;
    }
    let natural = advance_native_countdown(current, original)?;
    Some(((natural as i16 - target as i16).rem_euclid(64)) as u8)
}

pub fn matching_outcome_mask(
    candidate: Option<super::celebi_rng::Candidate>,
    result: Option<[u8; 2]>,
) -> Option<u64> {
    match (candidate, result) {
        (Some(candidate), Some(dvs)) => Some(
            candidate
                .outcomes()
                .iter()
                .enumerate()
                .fold(0u64, |mask, (index, predicted)| {
                    mask | if *predicted == dvs { 1u64 << index } else { 0 }
                }),
        ),
        _ => None,
    }
}

impl NativeTimerState {
    pub const fn unreadable() -> Self {
        Self {
            readable: false,
            last_instruction_cycles: 0,
            guest_cycle_total: 0,
            div_countdown: 0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FinalPhaseSample {
    pub kind: u8,
    pub direct_ordinal: u8,
    pub advance: u32,
    pub cycle: u32,
    pub state: u16,
    pub div: u8,
    pub pc: u16,
    pub frame: u8,
    pub path_word: u16,
    pub native_timer: NativeTimerState,
}

/// Sparse checkpoints from the VC's existing cycle-accounting callback.
/// `delta` is the callback's original scalar argument; `pc` is read only from
/// Crystal's fixed, already-validated PC storage slot. No address is derived
/// from ARM registers and no guest-memory window is copied.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExecutionGapSample {
    pub elapsed: u32,
    pub delta: u32,
    pub pc: u16,
}

/// Rolling, chronological tail immediately before the first direct Random.
/// It uses only fixed scalar slots and is bounded independently of cutscene
/// duration, so one diagnostic encounter can identify the last safe control
/// boundary without retaining the full execution path.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExecutionGapTailSample {
    pub elapsed: u32,
    pub delta: u32,
    pub pc: u16,
    pub countdown_before: u32,
}

impl ExecutionGapTailSample {
    pub const fn empty() -> Self {
        Self { elapsed: 0, delta: 0, pc: 0, countdown_before: 0 }
    }
}

/// The most recent verified guest instruction boundary before one direct
/// `BattleRandom` call.  AUTO 0.2.46 records this separately for each direct
/// call so later control can be designed from hardware evidence instead of
/// assuming the first-call boundary repeats.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DirectBoundarySample {
    pub ordinal: u8,
    pub elapsed: u32,
    pub control_cycle: u32,
    pub direct_cycle: u32,
    pub delta: u32,
    pub pc: u16,
    pub countdown_before: u32,
    pub direct_div: u8,
}

impl DirectBoundarySample {
    pub const fn empty() -> Self {
        Self {
            ordinal: 0,
            elapsed: 0,
            control_cycle: 0,
            direct_cycle: 0,
            delta: 0,
            pc: 0,
            countdown_before: 0,
            direct_div: 0,
        }
    }
}

/// A compact per-call tail used to discover an alternative guest PC when a
/// later direct call does not pass through 00:2FA5. `callbacks_before_direct`
/// is one for the final callback immediately preceding the FF04 read.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DirectBoundaryTailSample {
    pub ordinal: u8,
    pub callbacks_before_direct: u8,
    pub elapsed: u32,
    pub cycle: u32,
    pub delta: u32,
    pub pc: u16,
    pub countdown_before: u32,
}

impl DirectBoundaryTailSample {
    pub const fn empty() -> Self {
        Self {
            ordinal: 0,
            callbacks_before_direct: 0,
            elapsed: 0,
            cycle: 0,
            delta: 0,
            pc: 0,
            countdown_before: 0,
        }
    }
}

impl ExecutionGapSample {
    pub const fn empty() -> Self {
        Self { elapsed: 0, delta: 0, pc: 0 }
    }
}

impl FinalPhaseSample {
    pub const fn empty() -> Self {
        Self {
            kind: 0,
            direct_ordinal: 0,
            advance: 0,
            cycle: 0,
            state: 0,
            div: 0,
            pc: 0,
            frame: 0,
            path_word: 0,
            native_timer: NativeTimerState::unreadable(),
        }
    }
}

pub struct FinalPhaseTrace {
    samples: [FinalPhaseSample; FINAL_PHASE_TRACE_CAPACITY],
    count: usize,
    direct_ordinal: u8,
    pub overflow: bool,
    pub path_word: u16,
    pub final_vblank_advance: u32,
    pub supported_path: bool,
    gap_samples: [ExecutionGapSample; EXECUTION_GAP_TRACE_CAPACITY],
    gap_count: usize,
    gap_tail: [ExecutionGapTailSample; EXECUTION_GAP_TAIL_CAPACITY],
    gap_tail_count: usize,
    gap_tail_next: usize,
    gap_active: bool,
    pub gap_sealed: bool,
    pub gap_overflow: bool,
    pub gap_start_cycle: u32,
    pub gap_end_cycle: u32,
    pub gap_update_calls: u32,
    gap_next_checkpoint: u32,
    direct_boundary_samples: [DirectBoundarySample; DIRECT_BOUNDARY_TRACE_CAPACITY],
    direct_boundary_count: usize,
    direct_boundary_pending: ExecutionGapTailSample,
    direct_boundary_pending_cycle: u32,
    direct_boundary_pending_valid: bool,
    direct_boundary_active: bool,
    pub direct_boundary_hits: u32,
    pub direct_boundary_misses: u32,
    direct_boundary_ring: [ExecutionGapTailSample; DIRECT_BOUNDARY_TAIL_PER_CALL],
    direct_boundary_ring_cycles: [u32; DIRECT_BOUNDARY_TAIL_PER_CALL],
    direct_boundary_ring_count: usize,
    direct_boundary_ring_next: usize,
    direct_boundary_tail_samples: [DirectBoundaryTailSample; DIRECT_BOUNDARY_TAIL_CAPACITY],
    direct_boundary_tail_count: usize,
}

impl FinalPhaseTrace {
    pub const fn new() -> Self {
        Self {
            samples: [FinalPhaseSample::empty(); FINAL_PHASE_TRACE_CAPACITY],
            count: 0,
            direct_ordinal: 0,
            overflow: false,
            path_word: 0,
            final_vblank_advance: 0,
            supported_path: false,
            gap_samples: [ExecutionGapSample::empty(); EXECUTION_GAP_TRACE_CAPACITY],
            gap_count: 0,
            gap_tail: [ExecutionGapTailSample::empty(); EXECUTION_GAP_TAIL_CAPACITY],
            gap_tail_count: 0,
            gap_tail_next: 0,
            gap_active: false,
            gap_sealed: false,
            gap_overflow: false,
            gap_start_cycle: 0,
            gap_end_cycle: 0,
            gap_update_calls: 0,
            gap_next_checkpoint: EXECUTION_GAP_CHECKPOINT_STRIDE,
            direct_boundary_samples: [DirectBoundarySample::empty(); DIRECT_BOUNDARY_TRACE_CAPACITY],
            direct_boundary_count: 0,
            direct_boundary_pending: ExecutionGapTailSample::empty(),
            direct_boundary_pending_cycle: 0,
            direct_boundary_pending_valid: false,
            direct_boundary_active: false,
            direct_boundary_hits: 0,
            direct_boundary_misses: 0,
            direct_boundary_ring: [ExecutionGapTailSample::empty(); DIRECT_BOUNDARY_TAIL_PER_CALL],
            direct_boundary_ring_cycles: [0; DIRECT_BOUNDARY_TAIL_PER_CALL],
            direct_boundary_ring_count: 0,
            direct_boundary_ring_next: 0,
            direct_boundary_tail_samples: [DirectBoundaryTailSample::empty(); DIRECT_BOUNDARY_TAIL_CAPACITY],
            direct_boundary_tail_count: 0,
        }
    }

    pub fn reset(&mut self) {
        self.samples.fill(FinalPhaseSample::empty());
        self.count = 0;
        self.direct_ordinal = 0;
        self.overflow = false;
        self.path_word = 0;
        self.final_vblank_advance = 0;
        self.supported_path = false;
        self.gap_samples.fill(ExecutionGapSample::empty());
        self.gap_count = 0;
        self.gap_tail.fill(ExecutionGapTailSample::empty());
        self.gap_tail_count = 0;
        self.gap_tail_next = 0;
        self.gap_active = false;
        self.gap_sealed = false;
        self.gap_overflow = false;
        self.gap_start_cycle = 0;
        self.gap_end_cycle = 0;
        self.gap_update_calls = 0;
        self.gap_next_checkpoint = EXECUTION_GAP_CHECKPOINT_STRIDE;
        self.direct_boundary_samples.fill(DirectBoundarySample::empty());
        self.direct_boundary_count = 0;
        self.direct_boundary_pending = ExecutionGapTailSample::empty();
        self.direct_boundary_pending_cycle = 0;
        self.direct_boundary_pending_valid = false;
        self.direct_boundary_active = false;
        self.direct_boundary_hits = 0;
        self.direct_boundary_misses = 0;
        self.direct_boundary_ring.fill(ExecutionGapTailSample::empty());
        self.direct_boundary_ring_cycles.fill(0);
        self.direct_boundary_ring_count = 0;
        self.direct_boundary_ring_next = 0;
        self.direct_boundary_tail_samples.fill(DirectBoundaryTailSample::empty());
        self.direct_boundary_tail_count = 0;
    }

    pub fn schedule(&mut self, checkpoint_advance: u32, path_word: u16) -> Option<u32> {
        self.path_word = path_word;
        let delta = match path_word {
            0xC0B9 => 3,
            0xC0A7 => 4,
            _ => {
                self.supported_path = false;
                self.final_vblank_advance = 0;
                return None;
            }
        };
        self.supported_path = true;
        self.final_vblank_advance = checkpoint_advance.wrapping_add(delta);
        Some(self.final_vblank_advance)
    }

    pub fn record(&mut self, mut sample: FinalPhaseSample) {
        match sample.kind {
            FINAL_PHASE_DIRECT_ADD => {
                self.direct_ordinal = self.direct_ordinal.saturating_add(1);
                sample.direct_ordinal = self.direct_ordinal;
            }
            FINAL_PHASE_DIRECT_SUB => sample.direct_ordinal = self.direct_ordinal,
            _ => sample.direct_ordinal = 0,
        }
        if self.count == self.samples.len() {
            self.overflow = true;
            return;
        }
        self.samples[self.count] = sample;
        self.count += 1;
    }

    pub fn len(&self) -> usize {
        self.count
    }

    pub fn get(&self, index: usize) -> Option<FinalPhaseSample> {
        self.samples.get(index).copied().filter(|_| index < self.count)
    }

    pub fn begin_execution_gap(&mut self, cycle: u32) {
        self.gap_samples.fill(ExecutionGapSample::empty());
        self.gap_count = 0;
        self.gap_tail.fill(ExecutionGapTailSample::empty());
        self.gap_tail_count = 0;
        self.gap_tail_next = 0;
        self.gap_active = true;
        self.gap_sealed = false;
        self.gap_overflow = false;
        self.gap_start_cycle = cycle;
        self.gap_end_cycle = 0;
        self.gap_update_calls = 0;
        self.gap_next_checkpoint = EXECUTION_GAP_CHECKPOINT_STRIDE;
        self.direct_boundary_samples.fill(DirectBoundarySample::empty());
        self.direct_boundary_count = 0;
        self.direct_boundary_pending = ExecutionGapTailSample::empty();
        self.direct_boundary_pending_cycle = 0;
        self.direct_boundary_pending_valid = false;
        self.direct_boundary_active = true;
        self.direct_boundary_hits = 0;
        self.direct_boundary_misses = 0;
        self.direct_boundary_ring.fill(ExecutionGapTailSample::empty());
        self.direct_boundary_ring_cycles.fill(0);
        self.direct_boundary_ring_count = 0;
        self.direct_boundary_ring_next = 0;
        self.direct_boundary_tail_samples.fill(DirectBoundaryTailSample::empty());
        self.direct_boundary_tail_count = 0;
    }

    /// Account for every existing cycle callback, but request a fixed-PC read
    /// only after another 512-cycle boundary has been crossed.
    pub fn observe_execution_gap_cycle(&mut self, total_cycle: u32) -> bool {
        if !self.gap_active {
            return false;
        }
        self.gap_update_calls = self.gap_update_calls.saturating_add(1);
        total_cycle.wrapping_sub(self.gap_start_cycle) >= self.gap_next_checkpoint
    }

    pub fn record_execution_gap_checkpoint(&mut self, total_cycle: u32, delta: u32, pc: u16) {
        if !self.gap_active {
            return;
        }
        let elapsed = total_cycle.wrapping_sub(self.gap_start_cycle);
        if self.gap_count == self.gap_samples.len() {
            self.gap_overflow = true;
        } else {
            self.gap_samples[self.gap_count] = ExecutionGapSample { elapsed, delta, pc };
            self.gap_count += 1;
        }
        while self.gap_next_checkpoint <= elapsed {
            self.gap_next_checkpoint = self
                .gap_next_checkpoint
                .saturating_add(EXECUTION_GAP_CHECKPOINT_STRIDE);
        }
    }

    pub fn record_execution_gap_tail(
        &mut self,
        total_cycle: u32,
        delta: u32,
        pc: u16,
        countdown_before: u32,
    ) {
        if !self.gap_active {
            return;
        }
        self.gap_tail[self.gap_tail_next] = ExecutionGapTailSample {
            elapsed: total_cycle.wrapping_sub(self.gap_start_cycle),
            delta,
            pc,
            countdown_before,
        };
        self.gap_tail_next = (self.gap_tail_next + 1) % self.gap_tail.len();
        self.gap_tail_count = (self.gap_tail_count + 1).min(self.gap_tail.len());
    }

    pub fn seal_execution_gap(&mut self, cycle: u32) {
        if self.gap_active {
            self.gap_end_cycle = cycle;
            self.gap_active = false;
            self.gap_sealed = true;
        }
    }

    /// Retain only the latest exact 00:2FA5 callback since the preceding
    /// direct RNG call. This remains active after the full execution-gap trace
    /// is sealed, avoiding another large per-frame trace.
    pub fn observe_direct_boundary_point(
        &mut self,
        total_cycle: u32,
        delta: u32,
        pc: u16,
        countdown_before: u32,
    ) {
        if !self.direct_boundary_active {
            return;
        }
        let sample = ExecutionGapTailSample {
            elapsed: total_cycle.wrapping_sub(self.gap_start_cycle),
            delta,
            pc,
            countdown_before,
        };
        self.direct_boundary_ring[self.direct_boundary_ring_next] = sample;
        self.direct_boundary_ring_cycles[self.direct_boundary_ring_next] = total_cycle;
        self.direct_boundary_ring_next =
            (self.direct_boundary_ring_next + 1) % self.direct_boundary_ring.len();
        self.direct_boundary_ring_count =
            (self.direct_boundary_ring_count + 1).min(self.direct_boundary_ring.len());
        if is_pre_direct_tail_control_point(pc) {
            self.direct_boundary_pending = sample;
            self.direct_boundary_pending_cycle = total_cycle;
            self.direct_boundary_pending_valid = true;
            self.direct_boundary_hits = self.direct_boundary_hits.saturating_add(1);
        }
    }

    /// Pair the latest control-point witness with the direct FF04 byte that
    /// followed it. A witness is consumed exactly once, so a missing boundary
    /// cannot be silently reused for a later call.
    pub fn record_direct_boundary(&mut self, direct_cycle: u32, direct_div: u8) {
        if !self.direct_boundary_active {
            return;
        }
        let ordinal = self.direct_ordinal.saturating_add(1);
        if self.direct_boundary_pending_valid {
            if self.direct_boundary_count < self.direct_boundary_samples.len() {
                let pending = self.direct_boundary_pending;
                self.direct_boundary_samples[self.direct_boundary_count] = DirectBoundarySample {
                    ordinal,
                    elapsed: pending.elapsed,
                    control_cycle: self.direct_boundary_pending_cycle,
                    direct_cycle,
                    delta: pending.delta,
                    pc: pending.pc,
                    countdown_before: pending.countdown_before,
                    direct_div,
                };
                self.direct_boundary_count += 1;
            } else {
                self.overflow = true;
            }
        } else {
            self.direct_boundary_misses = self.direct_boundary_misses.saturating_add(1);
        }
        let oldest = if self.direct_boundary_ring_count == self.direct_boundary_ring.len() {
            self.direct_boundary_ring_next
        } else {
            0
        };
        for index in 0..self.direct_boundary_ring_count {
            if self.direct_boundary_tail_count == self.direct_boundary_tail_samples.len() {
                self.overflow = true;
                break;
            }
            let ring_index = (oldest + index) % self.direct_boundary_ring.len();
            let tail = self.direct_boundary_ring[ring_index];
            self.direct_boundary_tail_samples[self.direct_boundary_tail_count] =
                DirectBoundaryTailSample {
                    ordinal,
                    callbacks_before_direct: (self.direct_boundary_ring_count - index) as u8,
                    elapsed: tail.elapsed,
                    cycle: self.direct_boundary_ring_cycles[ring_index],
                    delta: tail.delta,
                    pc: tail.pc,
                    countdown_before: tail.countdown_before,
                };
            self.direct_boundary_tail_count += 1;
        }
        self.direct_boundary_ring_count = 0;
        self.direct_boundary_ring_next = 0;
        self.direct_boundary_pending_valid = false;
        if ordinal as usize >= DIRECT_BOUNDARY_TRACE_CAPACITY {
            self.direct_boundary_active = false;
        }
    }

    pub fn end_direct_boundary_probe(&mut self) {
        self.direct_boundary_active = false;
        self.direct_boundary_pending_valid = false;
        self.direct_boundary_ring_count = 0;
        self.direct_boundary_ring_next = 0;
    }

    pub fn direct_boundary_active(&self) -> bool { self.direct_boundary_active }
    pub fn direct_boundary_len(&self) -> usize { self.direct_boundary_count }
    pub fn direct_boundary_get(&self, index: usize) -> Option<DirectBoundarySample> {
        self.direct_boundary_samples
            .get(index)
            .copied()
            .filter(|_| index < self.direct_boundary_count)
    }
    pub fn direct_boundary_tail_len(&self) -> usize {
        self.direct_boundary_tail_count
    }
    pub fn direct_boundary_tail_get(&self, index: usize) -> Option<DirectBoundaryTailSample> {
        self.direct_boundary_tail_samples
            .get(index)
            .copied()
            .filter(|_| index < self.direct_boundary_tail_count)
    }

    pub fn execution_gap_active(&self) -> bool { self.gap_active }
    pub fn execution_gap_len(&self) -> usize { self.gap_count }
    pub fn execution_gap_get(&self, index: usize) -> Option<ExecutionGapSample> {
        self.gap_samples.get(index).copied().filter(|_| index < self.gap_count)
    }
    pub fn execution_gap_tail_len(&self) -> usize { self.gap_tail_count }
    pub fn execution_gap_tail_get(&self, index: usize) -> Option<ExecutionGapTailSample> {
        if index >= self.gap_tail_count {
            return None;
        }
        let oldest = if self.gap_tail_count == self.gap_tail.len() {
            self.gap_tail_next
        } else {
            0
        };
        Some(self.gap_tail[(oldest + index) % self.gap_tail.len()])
    }
}

/// The low six divider bits omitted by FF04, expressed as a phase offset from
/// the VC cycle counter.  The first observation creates exactly 64 possible
/// offsets; later reads only clear candidates, so hook work stays bounded.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DivPhaseSnapshot {
    pub base: u16,
    pub mask: u64,
    pub conflicts: u16,
}

impl DivPhaseSnapshot {
    pub fn count(&self) -> u32 {
        self.mask.count_ones()
    }
}

/// A bounded correction selected from the still-possible low six DIV bits.
/// The correction never guesses one candidate: it rotates the complete set
/// into one non-wrapping interval centred around 31/32.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhaseBarrierDecision {
    pub delay: u8,
    pub before_min: u8,
    pub before_max: u8,
    pub after_min: u8,
    pub after_max: u8,
    pub count: u8,
}

/// Select at most 63 guest cycles without discarding any phase candidate.
/// Ambiguous sets wider than half a DIV tick and conflicted trackers fail
/// closed, leaving the original VC execution path untouched.
pub fn phase_barrier_decision(
    snapshot: DivPhaseSnapshot,
    cycle: u32,
) -> Option<PhaseBarrierDecision> {
    let count = snapshot.count();
    if snapshot.conflicts != 0 || count == 0 || count > 32 {
        return None;
    }

    let mut before_min = u8::MAX;
    let mut before_max = 0u8;
    let mut remaining = snapshot.mask;
    while remaining != 0 {
        let bit = remaining.trailing_zeros() as u16;
        let low = ((cycle as u16)
            .wrapping_add(snapshot.base)
            .wrapping_add(bit)
            & 63) as u8;
        before_min = core::cmp::min(before_min, low);
        before_max = core::cmp::max(before_max, low);
        remaining &= remaining - 1;
    }

    let mut best: Option<(u8, u8, u8, u8)> = None;
    for delay in 0u8..=63 {
        let mut after_min = u8::MAX;
        let mut after_max = 0u8;
        let mut remaining = snapshot.mask;
        while remaining != 0 {
            let bit = remaining.trailing_zeros() as u16;
            let low = ((cycle as u16)
                .wrapping_add(snapshot.base)
                .wrapping_add(bit)
                .wrapping_add(delay as u16)
                & 63) as u8;
            after_min = core::cmp::min(after_min, low);
            after_max = core::cmp::max(after_max, low);
            remaining &= remaining - 1;
        }
        let span = after_max - after_min;
        if span > 31 {
            continue;
        }
        let centre_error = ((after_min as i16 + after_max as i16) - 63).unsigned_abs() as u8;
        let score = (span, centre_error, delay, after_min);
        if best.map_or(true, |current| score < current) {
            best = Some(score);
        }
    }

    let (_, _, delay, after_min) = best?;
    let mut after_max = 0u8;
    let mut remaining = snapshot.mask;
    while remaining != 0 {
        let bit = remaining.trailing_zeros() as u16;
        let low = ((cycle as u16)
            .wrapping_add(snapshot.base)
            .wrapping_add(bit)
            .wrapping_add(delay as u16)
            & 63) as u8;
        after_max = core::cmp::max(after_max, low);
        remaining &= remaining - 1;
    }
    Some(PhaseBarrierDecision {
        delay,
        before_min,
        before_max,
        after_min,
        after_max,
        count: count as u8,
    })
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PhaseBarrierTrace {
    pub cycle_target: u32,
    pub verified: bool,
    pub applied: u16,
    pub skipped: u16,
    pub total_cycles: u32,
    pub max_delay: u8,
    pub max_candidates: u8,
    pub first_advance: u32,
    pub last_advance: u32,
    pub last_decision: Option<PhaseBarrierDecision>,
}

impl PhaseBarrierTrace {
    pub const fn new() -> Self {
        Self {
            cycle_target: 0,
            verified: false,
            applied: 0,
            skipped: 0,
            total_cycles: 0,
            max_delay: 0,
            max_candidates: 0,
            first_advance: 0,
            last_advance: 0,
            last_decision: None,
        }
    }

    pub fn reset(&mut self, cycle_target: u32, verified: bool) {
        *self = Self { cycle_target, verified, ..Self::new() };
    }

    pub fn record_applied(&mut self, advance: u32, decision: PhaseBarrierDecision) {
        if self.applied == 0 {
            self.first_advance = advance;
        }
        self.applied = self.applied.saturating_add(1);
        self.last_advance = advance;
        self.total_cycles = self.total_cycles.saturating_add(decision.delay as u32);
        self.max_delay = core::cmp::max(self.max_delay, decision.delay);
        self.max_candidates = core::cmp::max(self.max_candidates, decision.count);
        self.last_decision = Some(decision);
    }

    pub fn record_skipped(&mut self, advance: u32, count: u32) {
        self.skipped = self.skipped.saturating_add(1);
        self.last_advance = advance;
        self.max_candidates = core::cmp::max(self.max_candidates, count.min(255) as u8);
    }
}

pub struct DivPhaseTracker {
    base: u16,
    mask: u64,
    initialized: bool,
    conflicts: u16,
}

impl DivPhaseTracker {
    pub const fn new() -> Self {
        Self {
            base: 0,
            mask: 0,
            initialized: false,
            conflicts: 0,
        }
    }

    pub fn reset(&mut self) {
        self.base = 0;
        self.mask = 0;
        self.initialized = false;
        self.conflicts = 0;
    }

    pub fn observe(&mut self, cycle: u32, div: u8) {
        if !self.initialized {
            let cycle_phase = (cycle as u16) & (DIV_PHASE_MODULUS - 1);
            self.base = ((div as u16) << 6).wrapping_sub(cycle_phase) & (DIV_PHASE_MODULUS - 1);
            self.mask = u64::MAX;
            self.initialized = true;
            return;
        }
        let mut keep = self.mask;
        let mut remaining = self.mask;
        while remaining != 0 {
            let bit = remaining.trailing_zeros();
            let flag = 1u64 << bit;
            let phase = self.base.wrapping_add(bit as u16) & (DIV_PHASE_MODULUS - 1);
            let predicted = (((cycle as u16).wrapping_add(phase) >> 6) & 0xff) as u8;
            if predicted != div {
                keep &= !flag;
            }
            remaining &= remaining - 1;
        }
        if keep == 0 {
            self.conflicts = self.conflicts.saturating_add(1);
            let cycle_phase = (cycle as u16) & (DIV_PHASE_MODULUS - 1);
            self.base = ((div as u16) << 6).wrapping_sub(cycle_phase) & (DIV_PHASE_MODULUS - 1);
            self.mask = u64::MAX;
        } else {
            self.mask = keep;
        }
    }

    pub fn snapshot(&self) -> Option<DivPhaseSnapshot> {
        if self.initialized && self.mask != 0 {
            Some(DivPhaseSnapshot {
                base: self.base,
                mask: self.mask,
                conflicts: self.conflicts,
            })
        } else {
            None
        }
    }
}

/// A complete VBlank Random call captured before the final A input.  Five
/// hundred and twelve calls cover two full 8-bit frame-counter cycles while
/// keeping hook work fixed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TimingSample {
    pub advance: u32,
    pub frame: u8,
    pub add_cycle: u32,
    pub sub_cycle: u32,
    pub adiv: u8,
    pub sdiv: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct PendingTimingSample {
    advance: u32,
    frame: u8,
    add_cycle: u32,
    adiv: u8,
}

#[derive(Clone, Copy)]
pub struct TimingHistory {
    samples: [Option<TimingSample>; PRE_TRIGGER_TIMING_CAPACITY],
    next: usize,
    count: usize,
    pending: Option<PendingTimingSample>,
}

impl TimingHistory {
    pub const fn new() -> Self {
        Self {
            samples: [None; PRE_TRIGGER_TIMING_CAPACITY],
            next: 0,
            count: 0,
            pending: None,
        }
    }

    pub fn reset(&mut self) {
        self.samples.fill(None);
        self.next = 0;
        self.count = 0;
        self.pending = None;
    }

    pub fn observe_add(&mut self, advance: u32, frame: u8, cycle: u32, div: u8) {
        self.pending = Some(PendingTimingSample {
            advance,
            frame,
            add_cycle: cycle,
            adiv: div,
        });
    }

    pub fn observe_sub(&mut self, cycle: u32, div: u8) {
        let Some(pending) = self.pending.take() else {
            return;
        };
        self.samples[self.next] = Some(TimingSample {
            advance: pending.advance,
            frame: pending.frame,
            add_cycle: pending.add_cycle,
            sub_cycle: cycle,
            adiv: pending.adiv,
            sdiv: div,
        });
        self.next = (self.next + 1) % PRE_TRIGGER_TIMING_CAPACITY;
        self.count = core::cmp::min(self.count + 1, PRE_TRIGGER_TIMING_CAPACITY);
    }

    pub fn len(&self) -> usize {
        self.count
    }

    /// Returns samples oldest-first even after the fixed ring wraps.
    pub fn get(&self, index: usize) -> Option<TimingSample> {
        if index >= self.count {
            return None;
        }
        let start = if self.count == PRE_TRIGGER_TIMING_CAPACITY {
            self.next
        } else {
            0
        };
        self.samples[(start + index) % PRE_TRIGGER_TIMING_CAPACITY]
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RngRead {
    Add(u8),
    Sub(u8),
}

// English Crystal 1.1 VC PCs while read_gb_mem handles FF04. The VBlank
// routine drives the internal Advance counter; Random is used by BattleRandom
// for held-item and DV generation and must be recorded without incrementing it.
pub fn classify_rng_read(pc: u16) -> Option<RngRead> {
    match pc {
        0x2b6 => Some(RngRead::Add(RNG_SOURCE_VBLANK)),
        0x2be => Some(RngRead::Sub(RNG_SOURCE_VBLANK)),
        0x2f8e => Some(RngRead::Add(RNG_SOURCE_DIRECT)),
        0x2f96 => Some(RngRead::Sub(RNG_SOURCE_DIRECT)),
        _ => None,
    }
}

#[derive(Clone, Copy)]
pub struct RngStep {
    pub source: u8,
    pub frame: u8,
    pub advance: u32,
    // Accumulated GB CPU cycles reported by the VC execution hook.  Unlike the
    // empirical DIV index, this preserves the fine timer phase needed to
    // explain 0x12/0x13 boundary swaps.
    pub add_cycle: u32,
    // The second rDIV read happens several GB CPU cycles later. Recording it
    // separately is required near a 64-cycle DIV boundary.
    pub sub_cycle: u32,
    // RNG state immediately before the add-DIV read.
    pub state: u16,
    // State observed at the next recorded RNG call. This captures the real
    // result even though the memory hook runs before Random stores its output.
    pub post_state: u32,
    pub adiv: u8,
    pub sdiv: u8,
    pub adiv_index: u16,
    pub sdiv_index: u16,
}

#[derive(Clone, Copy)]
struct PendingRngStep {
    source: u8,
    frame: u8,
    advance: u32,
    add_cycle: u32,
    state: u16,
    adiv: u8,
    adiv_index: u16,
}

// Fixed storage keeps the RNG hook allocation-free. One entry represents one
// complete add/sub RNG call, assembled from the two FF04 reads.
pub struct RngTrace {
    active: bool,
    steps: [Option<RngStep>; RNG_TRACE_CAPACITY],
    count: usize,
    pub overflow: bool,
    pending: Option<PendingRngStep>,
}

impl RngTrace {
    pub const fn new() -> Self {
        Self {
            active: false,
            steps: [None; RNG_TRACE_CAPACITY],
            count: 0,
            overflow: false,
            pending: None,
        }
    }
    #[inline(never)]
    pub fn reset(&mut self) {
        self.active = false;
        self.steps.fill(None);
        self.count = 0;
        self.overflow = false;
        self.pending = None;
    }
    pub fn start(&mut self) {
        self.reset();
        self.active = true;
    }
    pub fn stop(&mut self) {
        self.active = false;
        self.pending = None;
    }
    pub fn observe_add(
        &mut self,
        source: u8,
        frame: u8,
        advance: u32,
        state: u16,
        adiv: u8,
        adiv_index: Option<usize>,
        cycle: u32,
    ) {
        if !self.active {
            return;
        }
        if self.count != 0 {
            if let Some(previous) = self.steps[self.count - 1].as_mut() {
                previous.post_state = state as u32;
            }
        }
        self.pending = Some(PendingRngStep {
            source,
            frame,
            advance,
            add_cycle: cycle,
            state,
            adiv,
            adiv_index: index_or_unknown(adiv_index),
        });
    }
    pub fn observe_sub(&mut self, source: u8, sdiv: u8, sdiv_index: Option<usize>, sub_cycle: u32) {
        if !self.active {
            return;
        }
        let Some(pending) = self.pending.take() else {
            return;
        };
        if pending.source != source {
            return;
        }
        if self.count == self.steps.len() {
            self.overflow = true;
            return;
        }
        self.steps[self.count] = Some(RngStep {
            source: pending.source,
            frame: pending.frame,
            advance: pending.advance,
            add_cycle: pending.add_cycle,
            sub_cycle,
            state: pending.state,
            post_state: UNKNOWN_STATE,
            adiv: pending.adiv,
            sdiv,
            adiv_index: pending.adiv_index,
            sdiv_index: index_or_unknown(sdiv_index),
        });
        self.count += 1;
    }
    pub fn len(&self) -> usize {
        self.count
    }
    pub fn get(&self, index: usize) -> Option<RngStep> {
        self.steps.get(index).copied().flatten()
    }
}

fn index_or_unknown(index: Option<usize>) -> u16 {
    index
        .and_then(|value| u16::try_from(value).ok())
        .unwrap_or(u16::MAX)
}

#[derive(Clone, Copy)]
pub struct ReadEvent {
    pub frame: u8,
    pub pc: u16,
    pub advance: u32,
    pub cycle: u32,
    pub raw: u8,
    pub returned: u8,
}

/// Scalar host state copied at the exact JOYP read where Crystal first
/// consumes the automatic A press. Register values come from the hook's
/// already-valid saved-register frame; no register value is dereferenced.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ActionEdgeContext {
    pub cycle: u32,
    pub advance: u32,
    pub guest_pc: u16,
    pub path_word: u16,
    pub native_timer: NativeTimerState,
    pub arm_regs: [u32; 15],
}

impl ActionEdgeContext {
    pub const fn empty() -> Self {
        Self {
            cycle: 0,
            advance: 0,
            guest_pc: 0,
            path_word: 0,
            native_timer: NativeTimerState::unreadable(),
            arm_regs: [0; 15],
        }
    }
}
#[derive(Clone, Copy)]
pub struct Reads {
    pub active: bool,
    pub first_press: Option<ReadEvent>,
    pub first_release: Option<ReadEvent>,
}
impl Reads {
    pub const fn new() -> Self {
        Self {
            active: false,
            first_press: None,
            first_release: None,
        }
    }
    pub fn observe(&mut self, e: ReadEvent) {
        if !self.active || e.returned & 0x20 != 0 {
            return;
        }
        if e.returned & 1 == 0 && self.first_press.is_none() {
            self.first_press = Some(e);
        } else if e.returned & 1 != 0 && self.first_press.is_some() {
            self.first_release = Some(e);
            self.active = false;
        }
    }
}
#[derive(Clone, Copy)]
struct Event {
    label: &'static str,
    stage: Stage,
    poll: u32,
    obs: Observation,
    screen: Screen,
    buttons: u8,
    paused_before: bool,
}
pub struct Trace {
    events: [Option<Event>; 32],
    count: usize,
    pub overflow: bool,
    polls: u32,
    started: bool,
    ack_seen: bool,
    gs_left: bool,
    release_sent: bool,
    release_seen: bool,
    invalid_bank_seen: bool,
    trigger_cycle: Option<u32>,
    trigger_phase: Option<DivPhaseSnapshot>,
    trigger_timing: Option<TimingHistory>,
    bank_recovered: bool,
}
impl Trace {
    pub const fn new() -> Self {
        Self {
            events: [None; 32],
            count: 0,
            overflow: false,
            polls: 0,
            started: false,
            ack_seen: false,
            gs_left: false,
            release_sent: false,
            release_seen: false,
            invalid_bank_seen: false,
            trigger_cycle: None,
            trigger_phase: None,
            trigger_timing: None,
            bank_recovered: false,
        }
    }

    /// Clear the static trace in place. Assigning `Trace::new()` inside the
    /// service callback materializes the whole trace (including the optional
    /// 512-sample timing history) on the game thread stack.
    #[inline(never)]
    pub fn reset(&mut self) {
        self.events.fill(None);
        self.count = 0;
        self.overflow = false;
        self.polls = 0;
        self.started = false;
        self.ack_seen = false;
        self.gs_left = false;
        self.release_sent = false;
        self.release_seen = false;
        self.invalid_bank_seen = false;
        self.trigger_cycle = None;
        self.trigger_phase = None;
        self.trigger_timing = None;
        self.bank_recovered = false;
    }

    pub fn capture_trigger_phase(
        &mut self,
        cycle: u32,
        phase: Option<DivPhaseSnapshot>,
        timing: TimingHistory,
    ) {
        if self.trigger_cycle.is_none() {
            self.trigger_cycle = Some(cycle);
            self.trigger_phase = phase;
            self.trigger_timing = Some(timing);
        }
    }
    fn record(
        &mut self,
        label: &'static str,
        stage: Stage,
        obs: Observation,
        screen: Screen,
        buttons: u8,
        paused_before: bool,
    ) {
        if self.count == self.events.len() {
            self.overflow = true;
            return;
        }
        self.events[self.count] = Some(Event {
            label,
            stage,
            poll: self.polls,
            obs,
            screen,
            buttons,
            paused_before,
        });
        self.count += 1;
    }
    #[inline(never)]
    pub fn update(
        &mut self,
        previous: Stage,
        current: Stage,
        obs: Observation,
        s: Screen,
        buttons: u8,
        paused_before: bool,
    ) {
        self.polls = self.polls.saturating_add(1);
        if self.count == 0 {
            self.record("first_observation", current, obs, s, buttons, paused_before);
        }
        if previous != current {
            self.record("stage", current, obs, s, buttons, paused_before);
        }
        if s.wram_bank & 7 > 1 && !self.invalid_bank_seen {
            self.invalid_bank_seen = true;
            self.record("first_non_player_WRAM", current, obs, s, buttons, paused_before);
        } else if s.wram_bank & 7 <= 1 && self.invalid_bank_seen && !self.bank_recovered {
            self.bank_recovered = true;
            self.record("player_WRAM_restored", current, obs, s, buttons, paused_before);
        }
        if !self.started
            && matches!(previous, Stage::DirectA | Stage::ManualA)
            && current == Stage::Battle
        {
            self.started = true;
            self.record(
                if previous == Stage::DirectA {
                    "automatic_JOYP_A_at_frozen_anchor"
                } else {
                    "automatic_JOYP_A_armed_at_public_target"
                },
                current,
                obs,
                s,
                buttons,
                paused_before,
            );
        } else if self.started {
            if !self.ack_seen && s.joy & 1 != 0 {
                self.ack_seen = true;
                self.record(
                    "first_observed_hJoypadDown_A",
                    current,
                    obs,
                    s,
                    buttons,
                    paused_before,
                );
            }
            if !self.gs_left && !s.gs_position && s.wram_bank & 7 <= 1 {
                self.gs_left = true;
                self.record(
                    "first_observed_GS_guard_lost",
                    current,
                    obs,
                    s,
                    buttons,
                    paused_before,
                );
            }
            if !self.release_sent && buttons & 1 == 0 {
                self.release_sent = true;
                self.record("release_command", current, obs, s, buttons, paused_before);
            } else if self.release_sent && !self.release_seen && s.joy & 1 == 0 {
                self.release_seen = true;
                self.record(
                    "first_observed_hJoypadDown_clear",
                    current,
                    obs,
                    s,
                    buttons,
                    paused_before,
                );
            }
        }
    }
    // Called only after terminal pause; formatting allocates here, never in hook.
    #[inline(never)]
    pub fn report(
        &self,
        run_id: u64,
        bot: &Bot,
        reads: Reads,
        action_edge: Option<ActionEdgeContext>,
        read_fn: u32,
        rng_trace: &RngTrace,
        final_phase_trace: &FinalPhaseTrace,
        direct_rng_resolver: DirectRngResolver,
        phase_barrier: &PhaseBarrierTrace,
    ) -> String {
        let mut out = String::new();
        let _ = write!(out, "{{\"schema\":1,\"version\":\"2.1.2\",\"kind\":\"terminal\",\"run_id\":\"{:016X}\",\"build_mode\":\"{}\",\"resolver_permitted\":{},\"stage\":\"{:?}\",\"reason\":", run_id, super::celebi_auto::build_mode_name(), super::celebi_auto::resolver_mode(), bot.stage);
        json_string(&mut out, bot.message);
        let _ = write!(
            out,
            ",\"attempts\":{},\"overflow\":{},\"read_fn\":{},\"anchor\":",
            bot.attempts, self.overflow, read_fn
        );
        snapshot(&mut out, bot.session.anchor);
        if super::celebi_auto::natural_mode() {
            let _ = write!(
                out,
                ",\"trigger_mode\":\"human_profile_unmodified_game_timing\",\"natural_delay_frames\":{},\"natural_timing_class\":{},\"natural_entropy_tag\":{}",
                bot.natural_delay_frames,
                bot.natural_timing_class,
                bot.natural_entropy_tag
            );
        } else if super::celebi_auto::resolver_mode() {
            let dvs = bot.selected_resolver_dvs();
            let _ = write!(
                out,
                ",\"trigger_mode\":\"immediate_selected_resolver\",\"selected_resolver_dvs\":[{},{}]",
                dvs[0],
                dvs[1]
            );
        } else {
            out.push_str(",\"trigger_mode\":\"frozen_anchor_immediate_after_search\"");
        }
        out.push_str(",\"candidate\":");
        if let Some(c) = bot.session.candidate {
            out.push_str("{\"snapshot\":");
            snapshot(&mut out, Some(c.snapshot));
            out.push_str(",\"auto_trigger_snapshot\":");
            snapshot(&mut out, Some(c.auto_trigger_snapshot()));
            out.push_str(",\"outcomes\":[");
            for (i, outcome) in c.outcomes().iter().enumerate() {
                if i != 0 {
                    out.push(',');
                }
                let _ = write!(out, "{:?}", outcome);
            }
            let selected = c.selected_shiny_dvs(bot.learned_outcome_mask);
            let _ = write!(
                out,
                "],\"outcome_count\":{},\"shiny_mask\":{},\"selected_shiny_dvs\":",
                c.outcome_count, c.shiny_mask
            );
            if let Some(dvs) = selected {
                let _ = write!(out, "{:?}", dvs);
            } else {
                out.push_str("null");
            }
            out.push('}');
        } else {
            out.push_str("null");
        }
        out.push_str(",\"actual_dvs\":");
        if let Some(d) = bot.session.result {
            let _ = write!(out, "{:?}", d);
        } else {
            out.push_str("null");
        }
        let matching = matching_outcome_mask(bot.session.candidate, bot.session.result);
        out.push_str(",\"matching_mask\":");
        if let Some(mask) = matching {
            let _ = write!(out, "{}", mask);
        } else {
            out.push_str("null");
        }
        out.push_str(",\"first_action_read_pressed\":");
        read_event(&mut out, reads.first_press);
        out.push_str(",\"first_action_read_released\":");
        read_event(&mut out, reads.first_release);
        out.push_str(",\"action_edge_context\":");
        action_edge_context(&mut out, action_edge);
        out.push_str(",\"phase_at_trigger\":");
        if let (Some(cycle), Some(phase)) = (self.trigger_cycle, self.trigger_phase) {
            let _ = write!(
                out,
                "{{\"cycle\":{},\"base\":{},\"mask\":\"{:016X}\",\"count\":{},\"conflicts\":{}}}",
                cycle,
                phase.base,
                phase.mask,
                phase.count(),
                phase.conflicts
            );
        } else {
            out.push_str("null");
        }
        out.push_str(",\"timing_before_trigger\":");
        if let Some(timing) = self.trigger_timing {
            out.push('[');
            for i in 0..timing.len() {
                if i != 0 {
                    out.push(',');
                }
                let sample = timing.get(i).unwrap();
                let _ = write!(
                    out,
                    "[{},{},{},{},{},{}]",
                    sample.advance,
                    sample.frame,
                    sample.add_cycle,
                    sample.sub_cycle,
                    sample.adiv,
                    sample.sdiv
                );
            }
            out.push(']');
        } else {
            out.push_str("null");
        }
        let _ = write!(
            out,
            ",\"rng_path\":{{\"count\":{},\"overflow\":{},\"steps\":[",
            rng_trace.len(),
            rng_trace.overflow
        );
        for i in 0..rng_trace.len() {
            if i != 0 {
                out.push(',');
            }
            let step = rng_trace.get(i).unwrap();
            let _ = write!(
                out,
                "[{},{},{},{},{},{},{},{},{},{},{}]",
                step.source,
                step.advance,
                step.frame,
                step.add_cycle,
                step.sub_cycle,
                step.state,
                step.post_state,
                step.adiv,
                step.sdiv,
                step.adiv_index,
                step.sdiv_index
            );
        }
        out.push_str("]}");
        let _ = write!(
            out,
            ",\"direct_rng_resolver\":{{\"mode\":\"sub_read_entropy_control_00_selected_candidate_dvs\",\"active\":{},\"failed\":{},\"failure_code\":{},\"sample_count\":{},\"samples\":[",
            direct_rng_resolver.active,
            direct_rng_resolver.failed,
            direct_rng_resolver.failure_code,
            direct_rng_resolver.len()
        );
        for i in 0..direct_rng_resolver.len() {
            if i != 0 { out.push(','); }
            let sample = direct_rng_resolver.get(i).unwrap();
            let _ = write!(
                out,
                "{{\"ordinal\":{},\"pre_state\":{},\"state_before_sub\":{},\"natural_add_div\":{},\"natural_sub_div\":{},\"returned_sub_div\":{},\"desired_output\":{},\"carry_out\":{}}}",
                sample.ordinal,
                sample.pre_state,
                sample.state_before_sub,
                sample.natural_add_div,
                sample.natural_sub_div,
                sample.returned_sub_div,
                sample.desired_output,
                sample.carry_out
            );
        }
        out.push_str("]}");
        let _ = write!(
            out,
            ",\"direct_boundary_observer\":{{\"target_pc\":{},\"hits\":{},\"misses\":{},\"sample_count\":{},\"samples\":[",
            PRE_DIRECT_TAIL_PC,
            final_phase_trace.direct_boundary_hits,
            final_phase_trace.direct_boundary_misses,
            final_phase_trace.direct_boundary_len()
        );
        for i in 0..final_phase_trace.direct_boundary_len() {
            if i != 0 { out.push(','); }
            let sample = final_phase_trace.direct_boundary_get(i).unwrap();
            let _ = write!(
                out,
                "{{\"ordinal\":{},\"elapsed\":{},\"control_cycle\":{},\"direct_cycle\":{},\"delta\":{},\"pc\":{},\"countdown_before\":{},\"direct_div\":{}}}",
                sample.ordinal,
                sample.elapsed,
                sample.control_cycle,
                sample.direct_cycle,
                sample.delta,
                sample.pc,
                sample.countdown_before,
                sample.direct_div
            );
        }
        out.push_str("],\"tail_per_call\":8,\"tail_samples\":[");
        for i in 0..final_phase_trace.direct_boundary_tail_len() {
            if i != 0 {
                out.push(',');
            }
            let sample = final_phase_trace.direct_boundary_tail_get(i).unwrap();
            let _ = write!(
                out,
                "{{\"ordinal\":{},\"callbacks_before_direct\":{},\"elapsed\":{},\"cycle\":{},\"delta\":{},\"pc\":{},\"countdown_before\":{}}}",
                sample.ordinal,
                sample.callbacks_before_direct,
                sample.elapsed,
                sample.cycle,
                sample.delta,
                sample.pc,
                sample.countdown_before
            );
        }
        out.push_str("]}");
        let _ = write!(
            out,
            ",\"final_phase_observer\":{{\"read_mode\":\"original_ff04_single_call_returned_to_game\",\"checkpoint_delta\":607,\"path_word\":{},\"supported_path\":{},\"final_vblank_advance\":{},\"sample_count\":{},\"overflow\":{},\"samples\":[",
            final_phase_trace.path_word,
            final_phase_trace.supported_path,
            final_phase_trace.final_vblank_advance,
            final_phase_trace.len(),
            final_phase_trace.overflow
        );
        for i in 0..final_phase_trace.len() {
            if i != 0 {
                out.push(',');
            }
            let sample = final_phase_trace.get(i).unwrap();
            let _ = write!(
                out,
                "{{\"kind\":{},\"direct_ordinal\":{},\"advance\":{},\"cycle\":{},\"state\":{},\"div\":{},\"pc\":{},\"frame\":{},\"path_word\":{},\"native_timer\":{{\"readable\":{},\"last_instruction_cycles\":{},\"guest_cycle_total\":{},\"div_countdown\":{}}}}}",
                sample.kind,
                sample.direct_ordinal,
                sample.advance,
                sample.cycle,
                sample.state,
                sample.div,
                sample.pc,
                sample.frame,
                sample.path_word,
                sample.native_timer.readable,
                sample.native_timer.last_instruction_cycles,
                sample.native_timer.guest_cycle_total,
                sample.native_timer.div_countdown
            );
        }
        out.push_str("]}");
        let gap_cycles = if final_phase_trace.gap_sealed {
            final_phase_trace.gap_end_cycle.wrapping_sub(final_phase_trace.gap_start_cycle)
        } else { 0 };
        let _ = write!(
            out,
            ",\"execution_gap_observer\":{{\"clock_source\":\"existing_vc_cycle_callback\",\"pc_source\":\"fixed_22F5FC_volatile_u16\",\"checkpoint_stride\":{},\"sealed\":{},\"start_cycle\":{},\"end_cycle\":{},\"total_cycles\":{},\"update_calls\":{},\"sample_count\":{},\"overflow\":{},\"samples\":[",
            EXECUTION_GAP_CHECKPOINT_STRIDE,
            final_phase_trace.gap_sealed,
            final_phase_trace.gap_start_cycle,
            final_phase_trace.gap_end_cycle,
            gap_cycles,
            final_phase_trace.gap_update_calls,
            final_phase_trace.execution_gap_len(),
            final_phase_trace.gap_overflow
        );
        for i in 0..final_phase_trace.execution_gap_len() {
            if i != 0 { out.push(','); }
            let sample = final_phase_trace.execution_gap_get(i).unwrap();
            let _ = write!(out, "{{\"elapsed\":{},\"delta\":{},\"pc\":{}}}", sample.elapsed, sample.delta, sample.pc);
        }
        out.push_str("],\"tail_mode\":\"last_32_cycle_callbacks_before_first_direct\",\"tail_samples\":[");
        for i in 0..final_phase_trace.execution_gap_tail_len() {
            if i != 0 { out.push(','); }
            let sample = final_phase_trace.execution_gap_tail_get(i).unwrap();
            let _ = write!(out, "{{\"elapsed\":{},\"delta\":{},\"pc\":{},\"countdown_before\":{}}}", sample.elapsed, sample.delta, sample.pc, sample.countdown_before);
        }
        out.push_str("]}");
        let _ = write!(
            out,
            ",\"phase_barrier\":{{\"mode\":\"post_a_vblank_sub_centered_low6_window\",\"cycle_target\":{},\"verified\":{},\"applied\":{},\"skipped\":{},\"total_cycles\":{},\"max_delay\":{},\"max_candidates\":{},\"first_advance\":{},\"last_advance\":{},\"last_decision\":",
            phase_barrier.cycle_target,
            phase_barrier.verified,
            phase_barrier.applied,
            phase_barrier.skipped,
            phase_barrier.total_cycles,
            phase_barrier.max_delay,
            phase_barrier.max_candidates,
            phase_barrier.first_advance,
            phase_barrier.last_advance
        );
        if let Some(decision) = phase_barrier.last_decision {
            let _ = write!(
                out,
                "{{\"delay\":{},\"before\":[{},{}],\"after\":[{},{}],\"count\":{}}}",
                decision.delay,
                decision.before_min,
                decision.before_max,
                decision.after_min,
                decision.after_max,
                decision.count
            );
        } else {
            out.push_str("null");
        }
        out.push('}');
        out.push_str(",\"events\":[");
        for (i, event) in self.events[..self.count].iter().enumerate() {
            let e = event.unwrap();
            if i != 0 {
                out.push(',');
            }
            let _ = write!(out, "{{\"event\":\"{}\",\"stage\":\"{:?}\",\"poll\":{},\"paused_before\":{},\"physical_a\":{},\"gb_counter\":{},\"joy\":{},\"joy_pressed\":{},\"buttons_command\":{},\"cpu_mhz\":{},\"tid\":{},\"map\":[{},{}],\"battle\":{},\"battle_start\":{},\"species\":{},\"level\":{},\"dvs_observed\":{:?},\"script\":[{},{}],\"gs_guard\":{},\"rng\":", e.label, e.stage, e.poll, e.paused_before, e.screen.physical_a, e.screen.frame, e.screen.joy, e.screen.joy_pressed, e.buttons, e.obs.cpu_mhz, e.obs.tid, e.obs.map.0, e.obs.map.1, e.obs.battle, e.obs.battle_start, e.obs.species, e.obs.level, e.obs.dvs, e.screen.script_bank, e.screen.script_pos, e.screen.gs_position);
            snapshot(&mut out, e.obs.rng);
            let _ = write!(
                out,
                ",\"wram_bank\":{},\"player_data_valid\":{}",
                e.screen.wram_bank,
                e.screen.wram_bank & 7 <= 1
            );
            let _ = write!(out, ",\"game_joy\":{},\"rom_bank\":{},\"menu\":{},\"save_info\":{},\"cursor\":{},\"world\":{},\"xy\":[{},{}]", e.screen.game_joy, e.screen.bank, e.screen.menu, e.screen.save_info, e.screen.cursor, e.screen.world, e.screen.x, e.screen.y);
            out.push('}');
        }
        out.push_str("]}\n");
        out
    }
}
fn snapshot(out: &mut String, value: Option<Snapshot>) {
    if let Some(s) = value {
        let _ = write!(
            out,
            "{{\"advance\":{},\"state\":{},\"div\":{},\"adiv_index\":{},\"sdiv_index\":{}}}",
            s.advance, s.state, s.div, s.adiv_index, s.sdiv_index
        );
    } else {
        out.push_str("null");
    }
}
fn read_event(out: &mut String, value: Option<ReadEvent>) {
    if let Some(e) = value {
        let _ = write!(
            out,
            "{{\"gb_counter\":{},\"pc\":{},\"advance\":{},\"cycle\":{},\"raw\":{},\"returned\":{}}}",
            e.frame, e.pc, e.advance, e.cycle, e.raw, e.returned
        );
    } else {
        out.push_str("null");
    }
}
fn action_edge_context(out: &mut String, value: Option<ActionEdgeContext>) {
    if let Some(e) = value {
        let _ = write!(
            out,
            "{{\"source\":\"saved_scalar_registers_and_fixed_native_timer_fields_at_first_pressed_joyp_read\",\"cycle\":{},\"advance\":{},\"guest_pc\":{},\"path_word\":{},\"native_timer\":{{\"readable\":{},\"last_instruction_cycles\":{},\"guest_cycle_total\":{},\"div_countdown\":{}}},\"arm_regs\":[",
            e.cycle,
            e.advance,
            e.guest_pc,
            e.path_word,
            e.native_timer.readable,
            e.native_timer.last_instruction_cycles,
            e.native_timer.guest_cycle_total,
            e.native_timer.div_countdown
        );
        for (index, value) in e.arm_regs.iter().enumerate() {
            if index != 0 {
                out.push(',');
            }
            let _ = write!(out, "{}", value);
        }
        out.push_str("]}");
    } else {
        out.push_str("null");
    }
}
fn json_string(out: &mut String, value: &str) {
    out.push('"');
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            ch if ch < ' ' => {
                let _ = write!(out, "\\u{:04x}", ch as u32);
            }
            ch => out.push(ch),
        }
    }
    out.push('"');
}
