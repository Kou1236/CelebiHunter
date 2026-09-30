use super::reader::Gen2Reader;
use crate::utils;

#[cfg(not(feature = "celebi_natural"))]
const DIV_INCREMENTS: [u8; 16] = [
    0x12, 0x12, 0x12, 0x13, 0x12, 0x12, 0x13, 0x12, 0x12, 0x13, 0x12, 0x12, 0x13, 0x12, 0x12, 0x13,
];

// Don't worry, I don't feel great about this either
// This is hacky while explorations are happening
static mut RNG_ADVANCE: u32 = 0;
static mut ADIV: u8 = 0;
static mut SDIV: u8 = 0;
static mut CYCLE_COUNTER: u32 = 0;
static mut DIV_PHASE_TRACKER: super::celebi_trace::DivPhaseTracker =
    super::celebi_trace::DivPhaseTracker::new();
static mut PRE_TRIGGER_TIMING: super::celebi_trace::TimingHistory = super::celebi_trace::TimingHistory::new();
// From final A until stable encounter DVs, retain only the VBlank advance
// counter required to timestamp the real JOYP read. All phase, 512-sample
// timing, state and full cutscene tracing is disabled on this critical path.
static mut LEAN_BATTLE: bool = false;
// The physical-A reference must not replace or observe JOYP reads after the
// target.  In this mode the shared hook immediately chains to the original VC
// memory-read function without touching guest memory, DIV trackers or traces.
static mut PASSIVE_BATTLE: bool = false;
// Read-only diagnostic mode for 0.2.35. It records only the FF04 byte returned
// by the original VC read function at two selected VBlanks and the following
// direct Random calls. The same byte is returned to Crystal, so observation
// never performs a second FF04 read.
static mut FULL_PHASE_PROBE: bool = false;
static mut HOST_DIV_CHECKPOINT_ADVANCE: u32 = 0;
static mut FINAL_PHASE_TRACE: super::celebi_trace::FinalPhaseTrace =
    super::celebi_trace::FinalPhaseTrace::new();
static mut DIRECT_RNG_RESOLVER: super::celebi_trace::DirectRngResolver =
    super::celebi_trace::DirectRngResolver::new();
static mut PHASE_BARRIER_TRACE: super::celebi_trace::PhaseBarrierTrace =
    super::celebi_trace::PhaseBarrierTrace::new();
static mut ACTION_EDGE_CONTEXT: Option<super::celebi_trace::ActionEdgeContext> = None;
static mut FINAL_VBLANK_ADVANCE: u32 = 0;
static mut NATIVE_PHASE_DELAY: super::celebi_trace::NativePhaseDelayState =
    super::celebi_trace::NativePhaseDelayState::new();
const CRYSTAL_PATH_WORD_ADDR: u32 = 0x22F5FE;
const CRYSTAL_PC_STORAGE_ADDR: u32 = 0x22F5FC;
// Verified from the pristine English Crystal VC 0.2.39 executable dump.
// The interpreter writes the just-executed instruction cost to 0x22F604,
// accumulates it at 0x22F924, then subtracts it from 0x22FA50 before updating
// the emulated DIV byte. These are fixed scalar fields, never pointers.
const VC_LAST_INSTRUCTION_CYCLES_ADDR: u32 = 0x22F604;
const VC_GUEST_CYCLE_TOTAL_ADDR: u32 = 0x22F924;
const VC_DIV_COUNTDOWN_ADDR: u32 = 0x22FA50;

fn native_timer_state() -> super::celebi_trace::NativeTimerState {
    let readable = crate::pnp::is_readable_range(VC_LAST_INSTRUCTION_CYCLES_ADDR, 4)
        && crate::pnp::is_readable_range(VC_GUEST_CYCLE_TOTAL_ADDR, 4)
        && crate::pnp::is_readable_range(VC_DIV_COUNTDOWN_ADDR, 4);
    if !readable {
        return super::celebi_trace::NativeTimerState::unreadable();
    }
    super::celebi_trace::NativeTimerState {
        readable: true,
        last_instruction_cycles: u32::from_le_bytes(crate::pnp::read_array(
            VC_LAST_INSTRUCTION_CYCLES_ADDR,
        )),
        guest_cycle_total: u32::from_le_bytes(crate::pnp::read_array(
            VC_GUEST_CYCLE_TOTAL_ADDR,
        )),
        div_countdown: u32::from_le_bytes(crate::pnp::read_array(VC_DIV_COUNTDOWN_ADDR)),
    }
}

pub fn measured_div() -> u16 {
    unsafe { (ADIV as u16) << 8 | SDIV as u16 }
}

pub fn rng_advance() -> u32 {
    unsafe { RNG_ADVANCE }
}

pub fn cycle_counter() -> u32 {
    unsafe { CYCLE_COUNTER }
}

pub fn div_phase() -> Option<super::celebi_trace::DivPhaseSnapshot> {
    unsafe { DIV_PHASE_TRACKER.snapshot() }
}

pub fn pre_trigger_timing() -> super::celebi_trace::TimingHistory {
    unsafe { PRE_TRIGGER_TIMING }
}

pub fn reset_rng_advance() {
    unsafe { RNG_ADVANCE = 0 };
}

#[inline(never)]
fn update_cycle_counter(regs: &mut [u32], _stack_pointer: *mut u32) {
    let mut delta = regs[0];
    unsafe {
        let gap_active = FULL_PHASE_PROBE && FINAL_PHASE_TRACE.execution_gap_active();
        let boundary_active = FULL_PHASE_PROBE && FINAL_PHASE_TRACE.direct_boundary_active();
        let tail_timer = if gap_active || boundary_active {
            native_timer_state()
        } else {
            super::celebi_trace::NativeTimerState::unreadable()
        };
        let tail_countdown_before = tail_timer.div_countdown;
        let tail_pc = if gap_active || boundary_active {
            u16::from_le(core::ptr::read_volatile(
                CRYSTAL_PC_STORAGE_ADDR as *const u16,
            ))
        } else {
            0
        };
        // 0.2.42 applied inside the 9,093-cycle wait and the wait simply ran
        // fewer natural guest cycles. 0.2.43 identified 00:2FA5 as the final
        // completed-instruction boundary after that wait and before the first
        // direct BattleRandom call. Arm on this exact PC witness so the
        // original VC timer update for this callback performs the change.
        if gap_active
            && super::celebi_trace::is_pre_direct_tail_control_point(tail_pc)
            && NATIVE_PHASE_DELAY.requested != 0
            && !NATIVE_PHASE_DELAY.complete
            && !NATIVE_PHASE_DELAY.failed
            && !NATIVE_PHASE_DELAY.armed
        {
            NATIVE_PHASE_DELAY.arm_source = 4;
            NATIVE_PHASE_DELAY.armed = true;
        }
        if NATIVE_PHASE_DELAY.armed {
            if delta == 0 || delta > 64 {
                NATIVE_PHASE_DELAY.failed = true;
                NATIVE_PHASE_DELAY.armed = false;
                NATIVE_PHASE_DELAY.pending = 0;
                NATIVE_PHASE_DELAY.failure_code = 1;
            } else {
                if NATIVE_PHASE_DELAY.pending == 0
                    && NATIVE_PHASE_DELAY.applied == 0
                    && !NATIVE_PHASE_DELAY.complete
                {
                    let timer = native_timer_state();
                    NATIVE_PHASE_DELAY.input_delta = delta as u8;
                    NATIVE_PHASE_DELAY.countdown_before = timer.div_countdown as u8;
                    let planned = if !timer.readable {
                        NATIVE_PHASE_DELAY.failure_code = 2;
                        None
                    } else if !(1..=64).contains(&timer.div_countdown) {
                        NATIVE_PHASE_DELAY.failure_code = 3;
                        None
                    } else if !(1..=64).contains(&NATIVE_PHASE_DELAY.requested) {
                        NATIVE_PHASE_DELAY.failure_code = 4;
                        None
                    } else {
                        super::celebi_trace::phase_delay_to_target(
                            timer.div_countdown as u8,
                            delta,
                            NATIVE_PHASE_DELAY.requested,
                        )
                    };
                    if let Some(extra) = planned {
                        // The post-wait tail has only this callback before the
                        // first direct read. Never leak a partial correction
                        // into a later callback after BattleRandom has begun.
                        if NATIVE_PHASE_DELAY.arm_source == 4
                            && extra as u32 > 64 - delta
                        {
                            NATIVE_PHASE_DELAY.failure_code = 7;
                            NATIVE_PHASE_DELAY.failed = true;
                            NATIVE_PHASE_DELAY.armed = false;
                            NATIVE_PHASE_DELAY.pending = 0;
                        } else {
                            NATIVE_PHASE_DELAY.pending = extra;
                        }
                        if !NATIVE_PHASE_DELAY.failed && extra == 0 {
                            NATIVE_PHASE_DELAY.armed = false;
                            NATIVE_PHASE_DELAY.complete = true;
                        }
                    } else {
                        NATIVE_PHASE_DELAY.failed = true;
                        NATIVE_PHASE_DELAY.armed = false;
                    }
                }
                let chunk = super::celebi_trace::phase_delay_chunk(
                    delta,
                    NATIVE_PHASE_DELAY.pending,
                );
                if NATIVE_PHASE_DELAY.armed && chunk.is_none() {
                    NATIVE_PHASE_DELAY.failed = true;
                    NATIVE_PHASE_DELAY.armed = false;
                    NATIVE_PHASE_DELAY.pending = 0;
                } else if NATIVE_PHASE_DELAY.armed {
                    let (adjusted, pending, applied) = chunk.unwrap();
                    if applied != 0 {
                        core::ptr::write_volatile(
                            VC_LAST_INSTRUCTION_CYCLES_ADDR as *mut u32,
                            adjusted,
                        );
                        let readback = core::ptr::read_volatile(
                            VC_LAST_INSTRUCTION_CYCLES_ADDR as *const u32,
                        );
                        NATIVE_PHASE_DELAY.written_cycles = adjusted as u8;
                        NATIVE_PHASE_DELAY.readback_cycles = readback as u8;
                        if readback != adjusted {
                            core::ptr::write_volatile(
                                VC_LAST_INSTRUCTION_CYCLES_ADDR as *mut u32,
                                delta,
                            );
                            NATIVE_PHASE_DELAY.failed = true;
                            NATIVE_PHASE_DELAY.failure_code = 5;
                            NATIVE_PHASE_DELAY.armed = false;
                            NATIVE_PHASE_DELAY.pending = 0;
                        } else {
                            regs[0] = adjusted;
                            delta = adjusted;
                            NATIVE_PHASE_DELAY.pending = pending;
                            NATIVE_PHASE_DELAY.applied =
                                NATIVE_PHASE_DELAY.applied.saturating_add(applied);
                        }
                    }
                    if !NATIVE_PHASE_DELAY.failed && NATIVE_PHASE_DELAY.pending == 0 {
                        NATIVE_PHASE_DELAY.armed = false;
                        NATIVE_PHASE_DELAY.complete = true;
                    }
                }
            }
        }
        CYCLE_COUNTER = CYCLE_COUNTER.wrapping_add(delta);
        if boundary_active {
            FINAL_PHASE_TRACE.observe_direct_boundary_point(
                CYCLE_COUNTER,
                delta,
                tail_pc,
                tail_countdown_before,
            );
        }
        if gap_active {
            FINAL_PHASE_TRACE.record_execution_gap_tail(
                CYCLE_COUNTER,
                delta,
                tail_pc,
                tail_countdown_before,
            );
            if FINAL_PHASE_TRACE.observe_execution_gap_cycle(CYCLE_COUNTER) {
                FINAL_PHASE_TRACE.record_execution_gap_checkpoint(CYCLE_COUNTER, delta, tail_pc);
            }
        }
    };
}

#[cfg(not(feature = "celebi_natural"))]
#[repr(C)]
pub struct DivTracker {
    last_div: u8,
    index: usize,
    correct_index: bool,
}

#[cfg(not(feature = "celebi_natural"))]
impl DivTracker {
    const fn new() -> Self {
        Self {
            last_div: 0,
            index: 0,
            correct_index: false,
        }
    }

    fn update(&mut self, div: u8) {
        let small_index = self.index % DIV_INCREMENTS.len();
        let diff = div.wrapping_sub(self.last_div);
        self.last_div = div;

        if diff != 0x12 && diff != 0x13 {
            self.correct_index = false;
        }

        if diff != DIV_INCREMENTS[small_index]
            && [2, 3, 5, 6, 8, 9].contains(&(small_index))
            && (self.index >= DIV_INCREMENTS.len() || self.correct_index)
        {
            self.index = match small_index {
                2 => 1 + 0x562,
                3 => 1 + 0x563,
                5 => 1 + 0x22b5,
                6 => 1 + 0x22b6,
                8 => 1 + 8,
                9 => 1 + 9,
                _ => 0,
            };
            self.correct_index = true;
        } else if diff != DIV_INCREMENTS[small_index] {
            self.index = 0;
            self.correct_index = false;
        } else {
            self.index = (self.index + 1) % 0x4000;
        }
    }

    pub fn index(&self) -> Option<usize> {
        // Hides until ready
        match self.correct_index {
            true => Some(self.index),
            false => None,
        }
    }
}

#[cfg(not(feature = "celebi_natural"))]
static mut ADD_DIV_TRACKER: DivTracker = DivTracker::new();
#[cfg(not(feature = "celebi_natural"))]
static mut SUB_DIV_TRACKER: DivTracker = DivTracker::new();

#[cfg(not(feature = "celebi_natural"))]
pub fn add_div_tracker() -> &'static DivTracker {
    unsafe { &ADD_DIV_TRACKER }
}

#[cfg(not(feature = "celebi_natural"))]
pub fn sub_div_tracker() -> &'static DivTracker {
    unsafe { &SUB_DIV_TRACKER }
}

fn gb_read_mem(regs: &[u32], _stack_pointer: *mut u32) -> Option<u8> {
    if unsafe { PASSIVE_BATTLE } {
        return None;
    }
    if regs[0] != 0xff04 {
        return None;
    }

    let reader = Gen2Reader::crystal();
    let Some(read) = super::celebi_trace::classify_rng_read(reader.pc_reg()) else {
        return None;
    };
    if unsafe { FULL_PHASE_PROBE } {
        // Call the original VC function exactly once, then return this same
        // byte to Crystal. This records the value actually consumed by Random
        // without a second FF04 read or a guessed host-memory pointer.
        let natural_div = super::game_lib::read_gb_mem(0xff04);
        let mut div = natural_div;
        unsafe { DIV_PHASE_TRACKER.observe(cycle_counter(), natural_div) };
        match read {
            super::celebi_trace::RngRead::Add(super::celebi_trace::RNG_SOURCE_VBLANK) => {
                unsafe { RNG_ADVANCE = RNG_ADVANCE.wrapping_add(1) };
                let current = rng_advance();
                if current == unsafe { HOST_DIV_CHECKPOINT_ADVANCE } {
                    let path_word = read_path_word().unwrap_or(0);
                    unsafe {
                        FINAL_VBLANK_ADVANCE = FINAL_PHASE_TRACE
                            .schedule(current, path_word)
                            .unwrap_or(0);
                        FINAL_PHASE_TRACE.record(capture_final_phase(
                            super::celebi_trace::FINAL_PHASE_CHECKPOINT_ADD,
                            div,
                        ));
                    }
                }
                if unsafe { FINAL_VBLANK_ADVANCE != 0 && current == FINAL_VBLANK_ADVANCE } {
                    unsafe {
                        FINAL_PHASE_TRACE.record(capture_final_phase(
                            super::celebi_trace::FINAL_PHASE_VBLANK_ADD,
                            div,
                        ));
                        FINAL_PHASE_TRACE.begin_execution_gap(cycle_counter());
                    }
                }
            }
            super::celebi_trace::RngRead::Sub(super::celebi_trace::RNG_SOURCE_VBLANK) => {
                let current = rng_advance();
                if current == unsafe { HOST_DIV_CHECKPOINT_ADVANCE } {
                    unsafe {
                        FINAL_PHASE_TRACE.record(capture_final_phase(
                            super::celebi_trace::FINAL_PHASE_CHECKPOINT_SUB,
                            div,
                        ));
                        // Leave phase control unarmed throughout the 9k-cycle
                        // wait. update_cycle_counter arms it only when the
                        // verified post-wait 00:2FA5 boundary is observed.
                    }
                }
                if unsafe { FINAL_VBLANK_ADVANCE != 0 && current == FINAL_VBLANK_ADVANCE } {
                    unsafe {
                        FINAL_PHASE_TRACE.record(capture_final_phase(
                            super::celebi_trace::FINAL_PHASE_VBLANK_SUB,
                            div,
                        ));
                    }
                }
                // AUTO 0.2.37 hardware evidence showed that directly calling
                // the helper did not keep actual rDIV synchronized with the
                // plugin counter. Leave execution untouched while the
                // executable audit determines the real scheduler boundary.
                // captures the fixed helper code for offline disassembly.
            }
            super::celebi_trace::RngRead::Add(super::celebi_trace::RNG_SOURCE_DIRECT) => {
                unsafe {
                    DIRECT_RNG_RESOLVER.observe_add(reader.rng_state(), natural_div);
                    FINAL_PHASE_TRACE.record_direct_boundary(cycle_counter(), div);
                    FINAL_PHASE_TRACE.seal_execution_gap(cycle_counter());
                    FINAL_PHASE_TRACE.record(capture_final_phase(
                        super::celebi_trace::FINAL_PHASE_DIRECT_ADD,
                        div,
                    ));
                }
            }
            super::celebi_trace::RngRead::Sub(super::celebi_trace::RNG_SOURCE_DIRECT) => {
                unsafe {
                    if let Some(resolved) =
                        DIRECT_RNG_RESOLVER.resolve_sub(reader.rng_state(), natural_div)
                    {
                        div = resolved;
                    }
                    FINAL_PHASE_TRACE.record(capture_final_phase(
                        super::celebi_trace::FINAL_PHASE_DIRECT_SUB,
                        div,
                    ));
                }
            }
            _ => {}
        }
        return Some(div);
    }
    if unsafe { LEAN_BATTLE } {
        if matches!(
            read,
            super::celebi_trace::RngRead::Add(super::celebi_trace::RNG_SOURCE_VBLANK)
        ) {
            unsafe { RNG_ADVANCE = RNG_ADVANCE.wrapping_add(1) };
        }
        return None;
    }
    match read {
        super::celebi_trace::RngRead::Add(source) => {
            let state = reader.rng_state();
            let div = reader.div();
            unsafe { DIV_PHASE_TRACKER.observe(cycle_counter(), div) };
            let index = if source == super::celebi_trace::RNG_SOURCE_VBLANK {
                unsafe { ADIV = div };
                unsafe { RNG_ADVANCE = RNG_ADVANCE.wrapping_add(1) };
                unsafe {
                    PRE_TRIGGER_TIMING.observe_add(
                        RNG_ADVANCE,
                        super::game_lib::gb_mem::read_u8(0xff9b),
                        cycle_counter(),
                        div,
                    )
                };
                #[cfg(feature = "celebi_natural")]
                {
                    None
                }
                #[cfg(not(feature = "celebi_natural"))]
                {
                    unsafe { ADD_DIV_TRACKER.update(div) };
                    unsafe { ADD_DIV_TRACKER.index() }
                }
            } else {
                None
            };
            unsafe {
                BATTLE_RNG_TRACE.observe_add(
                    source,
                    super::game_lib::gb_mem::read_u8(0xff9b),
                    RNG_ADVANCE,
                    state,
                    div,
                    index,
                    cycle_counter(),
                )
            };
        }
        super::celebi_trace::RngRead::Sub(source) => {
            let div = reader.div();
            unsafe { DIV_PHASE_TRACKER.observe(cycle_counter(), div) };
            let index = if source == super::celebi_trace::RNG_SOURCE_VBLANK {
                unsafe { SDIV = div };
                unsafe { PRE_TRIGGER_TIMING.observe_sub(cycle_counter(), div) };
                #[cfg(feature = "celebi_natural")]
                {
                    None
                }
                #[cfg(not(feature = "celebi_natural"))]
                {
                    unsafe { SUB_DIV_TRACKER.update(div) };
                    unsafe { SUB_DIV_TRACKER.index() }
                }
            } else {
                None
            };
            unsafe { BATTLE_RNG_TRACE.observe_sub(source, div, index, cycle_counter()) };
        }
    }
    None
}

static mut ORIGINAL_CYCLES: u32 = 0;
static mut ORIGINAL_READ: u32 = 0;
static mut INPUT_READS: super::celebi_trace::Reads = super::celebi_trace::Reads::new();
static mut BATTLE_RNG_TRACE: super::celebi_trace::RngTrace = super::celebi_trace::RngTrace::new();

fn read_path_word() -> Option<u16> {
    if !crate::pnp::is_readable_range(CRYSTAL_PATH_WORD_ADDR, 2) {
        return None;
    }
    Some(u16::from_le_bytes(crate::pnp::read_array(CRYSTAL_PATH_WORD_ADDR)))
}

fn capture_final_phase(kind: u8, div: u8) -> super::celebi_trace::FinalPhaseSample {
    let reader = Gen2Reader::crystal();
    super::celebi_trace::FinalPhaseSample {
        kind,
        direct_ordinal: 0,
        advance: rng_advance(),
        cycle: cycle_counter(),
        state: reader.rng_state(),
        div,
        pc: reader.pc_reg(),
        frame: super::game_lib::gb_mem::read_u8(0xff9b),
        path_word: unsafe { FINAL_PHASE_TRACE.path_word },
        native_timer: native_timer_state(),
    }
}

pub fn cycle_hook_verified() -> bool {
    unsafe { ORIGINAL_CYCLES == 0x1aad80 }
}

pub fn cycle_hook_target() -> u32 {
    unsafe { ORIGINAL_CYCLES }
}

pub fn start_input_trace() {
    unsafe {
        INPUT_READS = super::celebi_trace::Reads::new();
        INPUT_READS.active = true;
    }
}
pub fn finish_input_trace() -> super::celebi_trace::Reads {
    unsafe {
        INPUT_READS.active = false;
        INPUT_READS
    }
}
pub fn input_trace() -> super::celebi_trace::Reads {
    unsafe { INPUT_READS }
}
pub fn action_edge_context() -> Option<super::celebi_trace::ActionEdgeContext> {
    unsafe { ACTION_EDGE_CONTEXT }
}
pub fn start_battle_rng_trace() {
    unsafe { BATTLE_RNG_TRACE.start() }
}
pub fn finish_battle_rng_trace() {
    unsafe { BATTLE_RNG_TRACE.stop() }
}
pub fn battle_rng_trace() -> &'static super::celebi_trace::RngTrace {
    unsafe { &BATTLE_RNG_TRACE }
}
#[inline(never)]
pub fn begin_full_phase_probe(phase_target: u8, desired_dvs: [u8; 2]) {
    unsafe {
        FULL_PHASE_PROBE = false;
        FINAL_PHASE_TRACE.reset();
        DIRECT_RNG_RESOLVER.start_for_dvs(desired_dvs);
        PHASE_BARRIER_TRACE.reset(ORIGINAL_CYCLES, cycle_hook_verified());
        FINAL_VBLANK_ADVANCE = 0;
        HOST_DIV_CHECKPOINT_ADVANCE = RNG_ADVANCE.wrapping_add(607);
        INPUT_READS = super::celebi_trace::Reads::new();
        INPUT_READS.active = true;
        ACTION_EDGE_CONTEXT = None;
        NATIVE_PHASE_DELAY = super::celebi_trace::NativePhaseDelayState::new();
        if phase_target == 0 {
            // Native-cycle injection is disabled. Mark that controller
            // complete; the stable resolver controls only the three direct sub-side FF04
            // return bytes through DIRECT_RNG_RESOLVER.
            NATIVE_PHASE_DELAY.requested = 0;
            NATIVE_PHASE_DELAY.complete = true;
        } else {
            NATIVE_PHASE_DELAY.requested = phase_target.clamp(1, 64);
        }
        BATTLE_RNG_TRACE.reset();
        LEAN_BATTLE = false;
        PASSIVE_BATTLE = false;
        FULL_PHASE_PROBE = true;
    }
}
pub fn native_phase_delay() -> super::celebi_trace::NativePhaseDelayState {
    unsafe { NATIVE_PHASE_DELAY }
}
pub fn native_phase_delay_failed() -> bool {
    unsafe { NATIVE_PHASE_DELAY.failed }
}
pub fn mark_native_phase_incomplete() {
    unsafe {
        if !NATIVE_PHASE_DELAY.complete && !NATIVE_PHASE_DELAY.failed {
            NATIVE_PHASE_DELAY.failed = true;
            NATIVE_PHASE_DELAY.failure_code = 6;
            NATIVE_PHASE_DELAY.armed = false;
            NATIVE_PHASE_DELAY.pending = 0;
        }
    }
}
pub fn end_full_phase_probe() {
    unsafe {
        FINAL_PHASE_TRACE.end_direct_boundary_probe();
        DIRECT_RNG_RESOLVER.stop();
        FULL_PHASE_PROBE = false;
    }
}
pub fn final_phase_trace() -> &'static super::celebi_trace::FinalPhaseTrace {
    unsafe { &FINAL_PHASE_TRACE }
}
pub fn direct_rng_resolver() -> super::celebi_trace::DirectRngResolver {
    unsafe { DIRECT_RNG_RESOLVER }
}
pub fn phase_barrier_trace() -> &'static super::celebi_trace::PhaseBarrierTrace {
    unsafe { &PHASE_BARRIER_TRACE }
}
pub fn begin_lean_battle() {
    unsafe {
        LEAN_BATTLE = true;
        BATTLE_RNG_TRACE.reset();
    }
}
pub fn begin_passive_battle() {
    unsafe {
        PASSIVE_BATTLE = true;
        LEAN_BATTLE = false;
        INPUT_READS = super::celebi_trace::Reads::new();
        ACTION_EDGE_CONTEXT = None;
        NATIVE_PHASE_DELAY = super::celebi_trace::NativePhaseDelayState::new();
        BATTLE_RNG_TRACE.reset();
    }
}
pub fn end_passive_battle() {
    unsafe { PASSIVE_BATTLE = false }
}
pub fn end_lean_battle() {
    unsafe { LEAN_BATTLE = false }
}
pub fn lean_battle() -> bool {
    unsafe { LEAN_BATTLE }
}
#[inline(never)]
pub fn reset_runtime_traces() {
    unsafe {
        INPUT_READS = super::celebi_trace::Reads::new();
        ACTION_EDGE_CONTEXT = None;
        NATIVE_PHASE_DELAY = super::celebi_trace::NativePhaseDelayState::new();
        BATTLE_RNG_TRACE.reset();
        LEAN_BATTLE = false;
        PASSIVE_BATTLE = false;
        FULL_PHASE_PROBE = false;
        HOST_DIV_CHECKPOINT_ADVANCE = 0;
        FINAL_PHASE_TRACE.reset();
        DIRECT_RNG_RESOLVER = super::celebi_trace::DirectRngResolver::new();
        PHASE_BARRIER_TRACE.reset(ORIGINAL_CYCLES, cycle_hook_verified());
        FINAL_VBLANK_ADVANCE = 0;
    }
}

pub fn input_hook_verified() -> bool {
    unsafe { ORIGINAL_READ == 0x1690b0 }
}
pub fn input_hook_target() -> u32 {
    unsafe { ORIGINAL_READ }
}
#[inline(never)]
pub fn reset_trackers() {
    unsafe {
        #[cfg(not(feature = "celebi_natural"))]
        {
        ADD_DIV_TRACKER = DivTracker::new();
        SUB_DIV_TRACKER = DivTracker::new();
        }
        RNG_ADVANCE = 0;
        ADIV = 0;
        SDIV = 0;
        CYCLE_COUNTER = 0;
        DIV_PHASE_TRACKER.reset();
        PRE_TRIGGER_TIMING.reset();
        ACTION_EDGE_CONTEXT = None;
        NATIVE_PHASE_DELAY = super::celebi_trace::NativePhaseDelayState::new();
        LEAN_BATTLE = false;
        PASSIVE_BATTLE = false;
        FULL_PHASE_PROBE = false;
        HOST_DIV_CHECKPOINT_ADVANCE = 0;
        FINAL_PHASE_TRACE.reset();
        DIRECT_RNG_RESOLVER = super::celebi_trace::DirectRngResolver::new();
        PHASE_BARRIER_TRACE.reset(ORIGINAL_CYCLES, cycle_hook_verified());
        FINAL_VBLANK_ADVANCE = 0;
    }
}

// Same saved-register ABI as upstream hook_game_branch!. The JOYP override
// consumes the real emulator read once and skips only that original BL call.
unsafe extern "C" fn route_crystal(stack: *mut u32) {
    let regs = core::slice::from_raw_parts_mut(stack, 15);
    let return_pc = regs[13];
    if return_pc == 0x1a8364 {
        update_cycle_counter(regs, stack.add(15));
        regs[14] = return_pc;
        regs[13] = ORIGINAL_CYCLES;
    } else if return_pc == 0x1af180 {
        // Battle uses the same FF00 override that hit the requested Advance in
        // all 241 AUTO 0.2.20 attempts. INPUT_READS records that exact read.
        let buttons = super::celebi_auto_runtime::buttons();
        if regs[0] == 0xff00 && (buttons != 0 || INPUT_READS.active) && input_hook_verified() {
            let raw = super::game_lib::gb_mem::read_u8(0xff00);
            let value = super::celebi_auto::joyp_with_buttons(raw, buttons);
            if INPUT_READS.active && value & 0x20 == 0 {
                let first_pressed_read = value & 1 == 0 && INPUT_READS.first_press.is_none();
                INPUT_READS.observe(super::celebi_trace::ReadEvent {
                    frame: super::game_lib::gb_mem::read_u8(0xff9b),
                    pc: Gen2Reader::crystal().pc_reg(),
                    advance: rng_advance(),
                    cycle: cycle_counter(),
                    raw,
                    returned: value,
                });
                if first_pressed_read {
                    let mut arm_regs = [0u32; 15];
                    arm_regs.copy_from_slice(regs);
                    ACTION_EDGE_CONTEXT = Some(super::celebi_trace::ActionEdgeContext {
                        cycle: cycle_counter(),
                        advance: rng_advance(),
                        guest_pc: Gen2Reader::crystal().pc_reg(),
                        path_word: read_path_word().unwrap_or(0),
                        native_timer: native_timer_state(),
                        arm_regs,
                    });
                }
            }
            super::celebi_auto::prepare_read_return(regs, ORIGINAL_READ, Some(value));
        } else {
            let exact_value = gb_read_mem(regs, stack.add(15));
            super::celebi_auto::prepare_read_return(regs, ORIGINAL_READ, exact_value);
        }
    }
    regs.rotate_right(1);
}

pub fn init_crystal() {
    if !super::celebi_auto_runtime::celebi_auto_enabled() {
        utils::hook_game_branch!(
            game_name = crystal,
            update_cycle_counter = 0x1a8360,
            gb_read_mem = 0x1af17c,
        );
        return;
    }
    let trampoline = crate::pnp::get_trampoline_addr();
    unsafe {
        ORIGINAL_CYCLES = utils::hook_addr(0x1a8360, trampoline);
        ORIGINAL_READ = utils::hook_addr(0x1af17c, trampoline);
    }
    crate::pnp::write(
        crate::pnp::pa_from_va_ptr(crate::pnp::get_route_hook_addr()),
        &(route_crystal as u32),
    );
}
