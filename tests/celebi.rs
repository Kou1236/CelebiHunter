#![allow(dead_code)]
extern crate alloc;

#[test]
fn real_bank6_shrine_sample_is_not_player_data_and_recovers() {
    use celebi_auto::*;
    let mut b = Bot::new();
    b.stage = Stage::Shrine;
    let mut o = obs(start());
    // Observed CAL 0.2.1 failure, run 000000018732B75A (not synthetic DVs).
    o.tid = 0;
    o.map = (0, 0);
    o.battle = 217;
    o.battle_start = 162;
    o.species = 155;
    o.level = 64;
    o.dvs = [155, 137];
    let mut s = Screen {
        wram_bank: 254,
        bank: 65,
        frame: 169,
        game_joy: 1,
        ..Screen::default()
    };
    let out = b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Shrine);
    assert_eq!(out.motion, Motion::Run);
    assert_eq!(out.buttons, 0);
    s.wram_bank = 249;
    s.frame = 170;
    s.gs = true;
    b.tick(obs(start()), s, false, true, false);
    assert_eq!(b.stage, Stage::Track);
}

#[test]
fn invalid_bank_cannot_fake_result_or_trigger_a_stale_target() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    s.wram_bank = 254;
    o.dvs = [0x2a, 0xaa];
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    for _ in 0..8 {
        s.frame += 1;
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Battle);
    assert!(b.session.result.is_none());
    let (session, mut live) = prepared();
    let candidate = session.candidate;
    let anchor = session.anchor;
    let mut b = Bot::new();
    b.session = session;
    b.stage = Stage::Advance;
    s.gs_position = true;
    let out = b.tick(live, s, true, true, false);
    assert_eq!(out.motion, Motion::Step);
    assert_eq!(out.buttons, 0);
    assert_eq!(b.stage, Stage::Advance);
    assert_eq!(b.session.candidate, candidate);
    assert_eq!(b.session.anchor, anchor);
    // The stepped frame is validated once bank 1 returns.
    let mut rng = start().rng();
    rng.next();
    live.rng = Some(snapshot(&rng, start().advance + 1));
    s.wram_bank = 1;
    s.joy = 0;
    assert_eq!(b.tick(live, s, true, true, false).motion, Motion::Run);
    assert_eq!(b.stage, Stage::Advance);
    // A persistently switching bank must not cause unbounded blind stepping.
    s.wram_bank = 6;
    for _ in 0..8 {
        assert_eq!(b.tick(live, s, true, true, false).motion, Motion::Step);
    }
    b.tick(live, s, true, true, false);
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "WRAM did not restore; no trigger");
}

#[test]
fn search_uses_verified_snapshot_when_text_cache_resets() {
    use celebi_auto::*;
    for bank in [1, 6] {
        let mut b = Bot::new();
        b.stage = Stage::Search;
        let s = Screen {
            wram_bank: bank,
            gs_position: true,
            gs: false,
            ..Screen::default()
        };
        let out = b.tick(obs(start()), s, true, true, false);
        assert_eq!(out.motion, Motion::Pause);
        assert_ne!(b.stage, Stage::Stopped);
        assert_ne!(b.message, "GS text lost; no reset");
        assert!(b.session.anchor.is_some());
    }
}

#[test]
fn pending_result_never_resets_without_runtime_authorization() {
    use celebi_auto::*;
    let mut b = Bot::new();
    b.stage = Stage::SaveResult;
    let o = obs(start());
    let s = Screen::default();
    for _ in 0..10 {
        let out = b.tick(o, s, true, true, false);
        assert_eq!(out.motion, Motion::Pause);
        assert_eq!(out.buttons, 0);
    }
    b.authorize_verified_reset(false);
    assert_eq!(b.stage, Stage::Stopped);
    b.authorize_verified_reset(true);
    assert_eq!(b.tick(o, s, true, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
}

#[test]
fn invalid_bank_still_allows_cancel_and_enforces_cpu_and_timeout() {
    use celebi_auto::*;
    let mut o = obs(start());
    let s = Screen {
        wram_bank: 254,
        ..Screen::default()
    };
    let mut b = Bot::new();
    b.stage = Stage::Shrine;
    assert_eq!(b.tick(o, s, false, true, true).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
    let mut b = Bot::new();
    b.stage = Stage::Shrine;
    o.cpu_mhz = 536;
    b.tick(o, s, false, true, false);
    assert_eq!(b.message, "CPU changed; stopped");
    let mut b = Bot::new();
    b.stage = Stage::Shrine;
    for _ in 0..7201 {
        b.tick(obs(start()), s, false, true, false);
    }
    assert_eq!(b.message, "Stage timeout; no reset");
}
#[path = "../core/src/crystal/celebi_trace.rs"]
mod celebi_trace;

#[test]
fn calibration_stops_on_known_miss_preserving_candidate_and_result() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    b.calibration = true;
    let candidate = b.session.candidate;
    o.dvs = candidate
        .unwrap()
        .outcomes()
        .iter()
        .copied()
        .find(|d| !shiny(d[0], d[1]))
        .unwrap();
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    for _ in 0..10 {
        s.frame += 1;
        assert_ne!(b.tick(o, s, false, true, false).buttons, 15);
    }
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "CAL sample ready; no reset");
    assert_eq!(b.session.candidate, candidate);
    assert_eq!(b.session.result, Some(o.dvs));
}

#[test]
fn intro_bank_and_already_open_save_info_are_supported() {
    use celebi_auto::*;
    let o = obs(start());
    let mut b = Bot::calibration();
    let mut s = Screen {
        bank: 0x39,
        text_sampled: true,
        ..Screen::default()
    };
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.frame = 4;
    s.joy = 1;
    // Restore the proven 0.2.1 handoff: release only after the game mirror
    // confirms the press was consumed.
    s.game_joy = 1;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    let mut b = Bot::calibration();
    s.save_info = true;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Load);
    s.frame = 25;
    s.joy = 0;
    s.game_joy = 0;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
}

#[test]
fn splash_generates_bounded_press_release_edges() {
    use celebi_auto::*;
    let mut b = Bot::calibration();
    let o = obs(start());
    let mut s = Screen {
        bank: 0x39,
        text_sampled: true,
        ..Screen::default()
    };
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.joy = 1;
    for _ in 0..2 {
        s.frame = s.frame.wrapping_add(1);
        if s.frame == 2 {
            s.game_joy = 1;
        }
        let out = b.tick(o, s, false, true, false);
        if s.frame == 1 {
            assert_eq!(out.buttons, 1);
        } else {
            assert_eq!(out.buttons, 0);
        }
    }
    s.joy = 0;
    s.game_joy = 0;
    for _ in 0..12 {
        s.frame = s.frame.wrapping_add(1);
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    assert_eq!(b.stage, Stage::Title);
}

#[test]
fn menu_initialization_waits_and_missing_continue_never_selects_new_game() {
    use celebi_auto::*;
    let o = obs(start());
    let mut b = Bot::calibration();
    let mut s = Screen {
        menu: true,
        text_sampled: true,
        ..Screen::default()
    };
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Continue);
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Continue);
    s.cursor = 1;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    let mut b = Bot::calibration();
    s.menu = false;
    s.no_save_menu = true;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.message, "No Continue; no inputs");
}

#[test]
fn continue_restores_021_consumption_acknowledgement() {
    use celebi_auto::*;
    let o = obs(start());
    let mut b = Bot::new();
    b.stage = Stage::Continue;
    let mut s = Screen {
        menu: true,
        cursor: 1,
        text_sampled: true,
        ..Screen::default()
    };
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    // A low-level acknowledgement alone must not release the button. This is
    // the exact early-release regression introduced after the working 0.2.1.
    s.joy = 1;
    s.frame = 1;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.frame = 2;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    assert_eq!(b.stage, Stage::Continue);
    s.game_joy = 1;
    s.frame = 3;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    s.menu = false;
    s.save_info = true;
    s.text_sampled = true;
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Load);
}

#[test]
fn encounter_trigger_uses_verified_joyp_and_excludes_failed_battle_hid() {
    use celebi_auto::*;
    for stage in [Stage::Title, Stage::Continue, Stage::Load, Stage::Shrine] {
        assert_eq!(host_buttons_for_stage(stage, 1), 1);
    }
    assert_eq!(host_buttons_for_stage(Stage::Reset, 15), 15);
    for stage in [
        Stage::Track,
        Stage::Search,
        Stage::DirectA,
        Stage::Advance,
        Stage::Battle,
        Stage::SaveResult,
        Stage::Shiny,
        Stage::Stopped,
    ] {
        assert_eq!(host_buttons_for_stage(stage, 1), 0);
    }
    assert_eq!(joyp_buttons_for_stage(Stage::Battle, 1), 1);
    assert_eq!(joyp_buttons_for_stage(Stage::Battle, 15), 15);
    assert_eq!(joyp_buttons_for_stage(Stage::Continue, 1), 1);
    assert_eq!(joyp_buttons_for_stage(Stage::Reset, 15), 15);
}

#[test]
fn trace_ignores_direction_reads_and_retains_first_release() {
    use celebi_trace::*;
    let mut reads = Reads::new();
    reads.active = true;
    let mut e = ReadEvent {
        frame: 255,
        pc: 0x1234,
        advance: 1,
        cycle: 0x12345678,
        raw: 0xef,
        returned: 0xee,
    };
    reads.observe(e);
    assert!(reads.first_press.is_none());
    e.raw = 0xdf;
    e.returned = 0xde;
    reads.observe(e);
    assert_eq!(reads.first_press.unwrap().frame, 255);
    e.frame = 0;
    e.returned = 0xdf;
    reads.observe(e);
    assert_eq!(reads.first_release.unwrap().frame, 0);
    assert!(!reads.active);
    e.frame = 1;
    reads.observe(e);
    assert_eq!(reads.first_release.unwrap().frame, 0);
}

#[test]
fn trace_exports_validatable_fixture_with_ambiguous_matches() {
    use celebi_auto::*;
    use celebi_trace::*;
    let mut b = Bot::calibration();
    let mut outcomes = [[0; 2]; MAX_OUTCOMES];
    outcomes[..4].fill([0x2a, 0xaa]);
    b.session.candidate = Some(Candidate {
        snapshot: start(),
        trigger_snapshot: start(),
        outcomes,
        outcome_count: 4,
        shiny_mask: 15,
    });
    b.session.result = Some([0x2a, 0xaa]);
    b.stage = Stage::Shiny;
    b.message = "synthetic \"fixture\"\nnot hardware";
    let o = obs(start());
    let s = Screen {
        frame: 255,
        gs: true,
        ..Screen::default()
    };
    let mut trace = Trace::new();
    let mut timing = TimingHistory::new();
    timing.observe_add(7, 11, 1000, 20);
    timing.observe_sub(1010, 21);
    trace.capture_trigger_phase(
        1020,
        Some(DivPhaseSnapshot { base: 5, mask: 3, conflicts: 0 }),
        timing,
    );
    trace.update(Stage::DirectA, Stage::Battle, o, s, 1, true);
    trace.update(
        Stage::Battle,
        Stage::Shiny,
        o,
        Screen {
            frame: 0,
            joy: 1,
            ..s
        },
        0,
        false,
    );
    let mut final_phase = FinalPhaseTrace::new();
    assert_eq!(final_phase.schedule(607, 0xC0B9), Some(610));
    final_phase.record(FinalPhaseSample {
        kind: FINAL_PHASE_CHECKPOINT_ADD,
        advance: 607,
        div: 0x44,
        path_word: 0xC0B9,
        ..FinalPhaseSample::empty()
    });
    final_phase.record(FinalPhaseSample {
        kind: FINAL_PHASE_DIRECT_ADD,
        advance: 610,
        div: 0xC5,
        path_word: 0xC0B9,
        ..FinalPhaseSample::empty()
    });
    final_phase.record(FinalPhaseSample {
        kind: FINAL_PHASE_DIRECT_SUB,
        advance: 610,
        div: 0xC6,
        path_word: 0xC0B9,
        ..FinalPhaseSample::empty()
    });
    let report = trace.report(
        123,
        &b,
        Reads::new(),
        Some(ActionEdgeContext {
            cycle: 321,
            advance: 7,
            guest_pc: 0x953,
            path_word: 0xC0B9,
            native_timer: NativeTimerState {
                readable: true,
                last_instruction_cycles: 4,
                guest_cycle_total: 12345,
                div_countdown: 37,
            },
            arm_regs: [0x11; 15],
        }),
        0x1690b0,
        &RngTrace::new(),
        &final_phase,
        DirectRngResolver::new(),
        &PhaseBarrierTrace::new(),
    );
    assert!(report.contains("\"matching_mask\":15"));
    assert!(report.contains("\"timing_before_trigger\":[[7,11,1000,1010,20,21]]"));
    assert!(report.contains("\"version\":\"2.1.2\""));
    assert!(report.contains("\"selected_shiny_dvs\":[42, 170]"));
    assert!(report.contains("\"phase_barrier\":{"));
    assert!(report.contains("\"action_edge_context\":{"));
    assert!(report.contains("\"source\":\"saved_scalar_registers_and_fixed_native_timer_fields_at_first_pressed_joyp_read\""));
    assert!(report.contains("\"last_instruction_cycles\":4"));
    assert!(report.contains("\"guest_cycle_total\":12345"));
    assert!(report.contains("\"div_countdown\":37"));
    assert!(report.contains("\"path_word\":49337"));
    assert!(report.contains("\"final_phase_observer\":{"));
    assert!(report.contains("\"read_mode\":\"original_ff04_single_call_returned_to_game\""));
    assert!(report.contains("\"path_word\":49337"));
    assert!(report.contains("\"final_vblank_advance\":610"));
    assert!(!report.contains("\"full_phase_probe\":{"));
    assert!(!report.contains("\"sparse_pre_direct_probe\":{"));
    assert!(!report.contains("\"final_vblank_context\":{"));
    assert!(report.contains("\"sample_count\":3"));
    assert!(report.contains("\"direct_ordinal\":1"));
    assert!(report.contains("\"execution_gap_observer\":{"));
    assert!(report.contains("\"direct_boundary_observer\":{"));
    assert!(report.contains("\"checkpoint_stride\":512"));
    assert!(report.contains("\\\"fixture\\\"\\u000a"));
    assert!(report.ends_with("}\n"));
    if let Ok(path) = std::env::var("CELEBI_TEST_FIXTURE") {
        std::fs::write(path, report).unwrap();
    }
}

#[test]
fn final_phase_trace_schedules_known_paths_pairs_direct_calls_and_is_bounded() {
    use celebi_trace::*;
    let mut trace = FinalPhaseTrace::new();
    assert_eq!(trace.schedule(100, 0xC0B9), Some(103));
    assert!(trace.supported_path);
    assert_eq!(trace.schedule(200, 0xC0A7), Some(204));
    assert_eq!(trace.schedule(300, 0x1234), None);
    assert!(!trace.supported_path);

    trace.reset();
    for expected in 1..=5 {
        trace.record(FinalPhaseSample {
            kind: FINAL_PHASE_DIRECT_ADD,
            ..FinalPhaseSample::empty()
        });
        trace.record(FinalPhaseSample {
            kind: FINAL_PHASE_DIRECT_SUB,
            ..FinalPhaseSample::empty()
        });
        assert_eq!(trace.get(trace.len() - 2).unwrap().direct_ordinal, expected);
        assert_eq!(trace.get(trace.len() - 1).unwrap().direct_ordinal, expected);
    }
    trace.record(FinalPhaseSample::empty());
    trace.record(FinalPhaseSample::empty());
    assert_eq!(trace.len(), FINAL_PHASE_TRACE_CAPACITY);
    trace.record(FinalPhaseSample::empty());
    assert!(trace.overflow);
    assert_eq!(trace.len(), FINAL_PHASE_TRACE_CAPACITY);
}

#[test]
fn phase_barrier_centres_the_complete_low_six_bit_window() {
    use celebi_trace::*;
    let snapshot = DivPhaseSnapshot {
        base: 0x1234,
        mask: 0x0000_0000_003f_ffff,
        conflicts: 0,
    };
    let decision = phase_barrier_decision(snapshot, 0x7654_3210).unwrap();
    assert_eq!(decision.count, 22);
    assert!(decision.delay <= 63);
    assert!(decision.after_max - decision.after_min <= 21);
    assert!((decision.after_min as i16 + decision.after_max as i16 - 63).abs() <= 1);
}

#[test]
fn phase_barrier_fails_closed_for_conflicts_or_wide_candidate_sets() {
    use celebi_trace::*;
    assert!(phase_barrier_decision(
        DivPhaseSnapshot { base: 0, mask: 1, conflicts: 1 },
        0
    )
    .is_none());
    assert!(phase_barrier_decision(
        DivPhaseSnapshot { base: 0, mask: u64::MAX, conflicts: 0 },
        0
    )
    .is_none());

    let mut trace = PhaseBarrierTrace::new();
    trace.reset(0x1aad80, true);
    trace.record_skipped(10, 64);
    let decision = PhaseBarrierDecision {
        delay: 7,
        before_min: 50,
        before_max: 63,
        after_min: 20,
        after_max: 33,
        count: 14,
    };
    trace.record_applied(11, decision);
    assert_eq!(trace.applied, 1);
    assert_eq!(trace.skipped, 1);
    assert_eq!(trace.total_cycles, 7);
    assert_eq!(trace.max_candidates, 64);
    assert_eq!(trace.last_decision, Some(decision));
}

#[test]
fn execution_gap_observer_is_sparse_bounded_and_seals_exact_cycles() {
    use celebi_trace::*;
    let mut trace = FinalPhaseTrace::new();
    trace.begin_execution_gap(10_000);
    assert!(trace.execution_gap_active());
    assert!(!trace.observe_execution_gap_cycle(10_511));
    assert!(trace.observe_execution_gap_cycle(10_512));
    trace.record_execution_gap_checkpoint(10_512, 4, 0x1234);
    for i in 0..40u32 {
        trace.record_execution_gap_tail(10_513 + i, i + 1, 0x2000 + i as u16, 64 - i % 64);
    }
    assert!(!trace.observe_execution_gap_cycle(11_023));
    assert!(trace.observe_execution_gap_cycle(11_024));
    trace.record_execution_gap_checkpoint(11_024, 8, 0x5678);
    trace.seal_execution_gap(19_088);
    assert!(!trace.execution_gap_active());
    assert!(trace.gap_sealed);
    assert_eq!(trace.gap_end_cycle.wrapping_sub(trace.gap_start_cycle), 9_088);
    assert_eq!(trace.gap_update_calls, 4);
    assert_eq!(trace.execution_gap_len(), 2);
    assert_eq!(trace.execution_gap_get(0).unwrap(), ExecutionGapSample { elapsed: 512, delta: 4, pc: 0x1234 });
    assert_eq!(trace.execution_gap_get(1).unwrap().pc, 0x5678);
    assert_eq!(trace.execution_gap_tail_len(), EXECUTION_GAP_TAIL_CAPACITY);
    assert_eq!(trace.execution_gap_tail_get(0).unwrap().delta, 9);
    assert_eq!(trace.execution_gap_tail_get(31).unwrap().delta, 40);
    assert_eq!(trace.execution_gap_tail_get(31).unwrap().pc, 0x2027);
    assert_eq!(trace.execution_gap_tail_get(32), None);
    trace.reset();
    assert!(!trace.gap_sealed);
    assert_eq!(trace.execution_gap_len(), 0);
    assert_eq!(trace.execution_gap_tail_len(), 0);
}

#[test]
fn direct_boundary_observer_pairs_each_witness_once_and_fails_closed() {
    use celebi_trace::*;
    let mut trace = FinalPhaseTrace::new();
    trace.begin_execution_gap(10_000);
    assert!(trace.direct_boundary_active());

    trace.observe_direct_boundary_point(19_000, 4, PRE_DIRECT_TAIL_PC, 27);
    trace.observe_direct_boundary_point(19_002, 2, 0x2FA6, 23);
    trace.record_direct_boundary(19_004, 0x47);
    trace.record(FinalPhaseSample {
        kind: FINAL_PHASE_DIRECT_ADD,
        ..FinalPhaseSample::empty()
    });
    assert_eq!(trace.direct_boundary_len(), 1);
    assert_eq!(trace.direct_boundary_misses, 0);
    assert_eq!(trace.direct_boundary_tail_len(), 2);
    assert_eq!(trace.direct_boundary_tail_get(0).unwrap().callbacks_before_direct, 2);
    assert_eq!(trace.direct_boundary_tail_get(0).unwrap().pc, PRE_DIRECT_TAIL_PC);
    assert_eq!(trace.direct_boundary_tail_get(1).unwrap().callbacks_before_direct, 1);
    assert_eq!(trace.direct_boundary_tail_get(1).unwrap().pc, 0x2FA6);
    assert_eq!(
        trace.direct_boundary_get(0).unwrap(),
        DirectBoundarySample {
            ordinal: 1,
            elapsed: 9_000,
            control_cycle: 19_000,
            direct_cycle: 19_004,
            delta: 4,
            pc: PRE_DIRECT_TAIL_PC,
            countdown_before: 27,
            direct_div: 0x47,
        }
    );

    // The first witness was consumed and cannot be reused for ordinal 2.
    trace.record_direct_boundary(19_100, 0x4D);
    trace.record(FinalPhaseSample {
        kind: FINAL_PHASE_DIRECT_ADD,
        ..FinalPhaseSample::empty()
    });
    assert_eq!(trace.direct_boundary_len(), 1);
    assert_eq!(trace.direct_boundary_misses, 1);
    assert_eq!(trace.direct_boundary_tail_len(), 2);

    trace.observe_direct_boundary_point(19_200, 8, PRE_DIRECT_TAIL_PC, 51);
    trace.record_direct_boundary(19_208, 0x4F);
    trace.record(FinalPhaseSample {
        kind: FINAL_PHASE_DIRECT_ADD,
        ..FinalPhaseSample::empty()
    });
    assert_eq!(trace.direct_boundary_len(), 2);
    assert_eq!(trace.direct_boundary_get(1).unwrap().ordinal, 3);
    assert_eq!(trace.direct_boundary_get(1).unwrap().countdown_before, 51);
    assert_eq!(trace.direct_boundary_tail_len(), 3);
    assert_eq!(trace.direct_boundary_tail_get(2).unwrap().ordinal, 3);

    trace.end_direct_boundary_probe();
    assert!(!trace.direct_boundary_active());
    trace.reset();
    assert_eq!(trace.direct_boundary_len(), 0);
    assert_eq!(trace.direct_boundary_hits, 0);
    assert_eq!(trace.direct_boundary_misses, 0);
    assert_eq!(trace.direct_boundary_tail_len(), 0);
}

#[test]
fn host_phase_trace_pairs_direct_call_ordinals_and_is_bounded() {
    use celebi_trace::*;
    let mut trace = HostPhaseTrace::new();
    trace.record_trigger(HostPhaseSample::empty());
    for expected in 1..=5 {
        trace.record_direct_add(HostPhaseSample::empty());
        trace.record_direct_sub(HostPhaseSample::empty());
        assert_eq!(trace.get(trace.len() - 2).unwrap().direct_ordinal, expected);
        assert_eq!(trace.get(trace.len() - 1).unwrap().direct_ordinal, expected);
    }
    assert_eq!(trace.len(), 11);
    trace.record_direct_add(HostPhaseSample::empty());
    assert_eq!(trace.len(), HOST_PHASE_SAMPLE_CAPACITY);
    trace.record_direct_sub(HostPhaseSample::empty());
    assert!(trace.overflow);
    assert_eq!(trace.len(), HOST_PHASE_SAMPLE_CAPACITY);
}

#[test]
fn pre_direct_tail_control_point_is_exact_and_fail_closed() {
    use celebi_trace::{is_pre_direct_tail_control_point, PRE_DIRECT_TAIL_PC};
    assert_eq!(PRE_DIRECT_TAIL_PC, 0x2FA5);
    assert!(is_pre_direct_tail_control_point(0x2FA5));
    assert!(!is_pre_direct_tail_control_point(0x2FA4));
    assert!(!is_pre_direct_tail_control_point(0x2F8E));
}

#[test]
fn host_div_trace_is_bounded_and_seals_before_direct_rng() {
    use celebi_trace::*;
    let mut trace = HostDivTrace::new();
    let first = HostDivSample {
        kind: 1,
        advance: 1,
        cycle: 2,
        state: 3,
        storage: 4,
        ..HostDivSample::empty()
    };
    trace.record(first);
    assert_eq!(trace.get(0), Some(first));
    trace.seal();
    trace.record(HostDivSample {
        kind: 2,
        advance: 4,
        cycle: 5,
        state: 6,
        storage: 7,
        ..HostDivSample::empty()
    });
    assert_eq!(trace.len(), 1);
    assert!(trace.sealed());

    let mut full = HostDivTrace::new();
    for i in 0..HOST_DIV_TRACE_CAPACITY {
        full.record(HostDivSample {
            kind: 1,
            advance: i as u32,
            cycle: i as u32,
            state: i as u16,
            storage: i as u32,
            ..HostDivSample::empty()
        });
    }
    full.record(HostDivSample::empty());
    assert!(full.overflow);
    assert_eq!(full.len(), HOST_DIV_TRACE_CAPACITY);
}

#[test]
fn rng_trace_pairs_hook_reads_and_resets_between_runs() {
    use celebi_trace::*;
    assert_eq!(
        classify_rng_read(0x2b6),
        Some(RngRead::Add(RNG_SOURCE_VBLANK))
    );
    assert_eq!(
        classify_rng_read(0x2be),
        Some(RngRead::Sub(RNG_SOURCE_VBLANK))
    );
    assert_eq!(
        classify_rng_read(0x2f8e),
        Some(RngRead::Add(RNG_SOURCE_DIRECT))
    );
    assert_eq!(
        classify_rng_read(0x2f96),
        Some(RngRead::Sub(RNG_SOURCE_DIRECT))
    );
    assert_eq!(classify_rng_read(0), None);
    let mut path = RngTrace::new();
    path.start();
    path.observe_sub(RNG_SOURCE_VBLANK, 0x44, Some(9), 0x12345680);
    assert_eq!(path.len(), 0);
    path.observe_add(RNG_SOURCE_VBLANK, 7, 101, 0x1234, 0x20, Some(8), 0x12345678);
    path.observe_sub(RNG_SOURCE_VBLANK, 0x10, Some(9), 0x12345680);
    assert_eq!(path.len(), 1);
    let step = path.get(0).unwrap();
    assert_eq!(step.source, RNG_SOURCE_VBLANK);
    assert_eq!(step.frame, 7);
    assert_eq!(step.advance, 101);
    assert_eq!(step.add_cycle, 0x12345678);
    assert_eq!(step.sub_cycle, 0x12345680);
    assert_eq!(step.state, 0x1234);
    assert_eq!(step.adiv, 0x20);
    assert_eq!(step.sdiv, 0x10);
    assert_eq!(step.adiv_index, 8);
    assert_eq!(step.sdiv_index, 9);
    assert_eq!(step.post_state, 0x10000);
    path.observe_add(RNG_SOURCE_DIRECT, 7, 101, 0xabcd, 0x21, None, 0x12345690);
    path.observe_sub(RNG_SOURCE_DIRECT, 0x11, None, 0x12345690);
    assert_eq!(path.get(0).unwrap().post_state, 0xabcd);
    assert_eq!(path.get(1).unwrap().source, RNG_SOURCE_DIRECT);
    path.stop();
    path.observe_add(RNG_SOURCE_VBLANK, 8, 102, 0, 0, None, 0);
    path.observe_sub(RNG_SOURCE_VBLANK, 0, None, 0x12345691);
    assert_eq!(path.len(), 2);
    path.start();
    assert_eq!(path.len(), 0);
    assert!(!path.overflow);
}

#[test]
fn start_failures_report_specific_cause_and_never_step() {
    let good = obs(start());
    let cases = [
        (Observation { cpu_mhz: 0, ..good }, "CPU query unavailable"),
        (
            Observation {
                cpu_mhz: 536,
                ..good
            },
            "CPU mismatch; see values",
        ),
        (
            Observation {
                map: (0, 0),
                ..good
            },
            "Map mismatch; see values",
        ),
        (
            Observation { battle: 1, ..good },
            "Battle active; see values",
        ),
    ];
    for (observation, message) in cases {
        let mut session = Session::new();
        assert!(!session.tick(observation, true, Command::Search));
        assert_eq!(session.phase, Phase::Fault);
        assert_eq!(session.message, message);
        assert!(session.candidate.is_none());
        assert!(!session.tick(observation, true, Command::Arm));
    }

    let mut different_trainer = good;
    different_trainer.tid = 1;
    let mut session = Session::new();
    assert!(!session.tick(different_trainer, true, Command::Search));
    assert_ne!(session.phase, Phase::Fault);
}
#[path = "../core/src/crystal/celebi_auto.rs"]
mod celebi_auto;
#[path = "../core/src/crystal/celebi_rng/mod.rs"]
mod celebi_rng;
#[path = "../core/src/crystal/celebi_session.rs"]
mod celebi_session;
mod oracle;
use celebi_rng::*;
use celebi_session::*;

fn snapshot(rng: &GameboyRng, advance: u32) -> Snapshot {
    Snapshot {
        advance,
        state: rng.state(),
        div: (rng.adiv() as u16) << 8 | rng.sdiv() as u16,
        adiv_index: rng.add_div.index(),
        sdiv_index: rng.sub_div.index(),
    }
}
fn start() -> Snapshot {
    // Historical values are a software fixture ONLY; not a live target.
    Snapshot {
        advance: 15920,
        state: 0x0c0a,
        div: 0x2d2d,
        adiv_index: 779,
        sdiv_index: 66,
    }
}
fn obs(s: Snapshot) -> Observation {
    Observation {
        rng: Some(s),
        cpu_mhz: 268,
        tid: 12345,
        map: (3, 52),
        battle: 0,
        battle_start: 0,
        battle_ended: 0,
        species: 0,
        level: 0,
        hp: 0,
        max_hp: 0,
        dvs: [0; 2],
    }
}
fn prepared() -> (Session, Observation) {
    let o = obs(start());
    let mut session = Session::new();
    session.tick(o, true, Command::Search);
    for _ in 0..300 {
        if session.phase != Phase::Searching {
            break;
        }
        session.tick(o, true, Command::None);
    }
    assert_eq!(session.phase, Phase::Candidate);
    (session, o)
}

#[test]
fn celebi_suffix_matches_independent_upstream_oracle() {
    for snapshot in [
        start(),
        Snapshot {
            advance: 5328,
            state: 49201,
            div: 63994,
            adiv_index: 2803,
            sdiv_index: 9597,
        },
        Snapshot {
            advance: 11776,
            state: 62056,
            div: 26472,
            adiv_index: 7059,
            sdiv_index: 13853,
        },
    ] {
        let mut actual_prefix = snapshot.rng();
        let mut expected_prefix = oracle::rng::GameboyRng::new(
            snapshot.state,
            oracle::div::Div::new(snapshot.adiv_index, (snapshot.div >> 8) as u8),
            oracle::div::Div::new(snapshot.sdiv_index, snapshot.div as u8),
        );
        for _ in 0..594 {
            actual_prefix.next();
            expected_prefix.next();
        }
        let actual = outcomes_after_prefix(&actual_prefix);
        let expected = oracle::outcomes_from_prefix(&expected_prefix);
        assert_eq!(actual.values(), expected);
        assert_eq!(actual.values().len(), MAX_OUTCOMES);
    }
}

#[test]
fn auto_candidate_keeps_public_595_prefix_and_arms_one_before_target() {
    let mut search = Search::new(start(), 20000).unwrap();
    let candidate = loop {
        if let Some(candidate) = search.batch(4096) {
            break candidate;
        }
    };
    let mut expected_prefix = oracle::rng::GameboyRng::new(
        candidate.snapshot.state,
        oracle::div::Div::new(
            candidate.snapshot.adiv_index,
            (candidate.snapshot.div >> 8) as u8,
        ),
        oracle::div::Div::new(candidate.snapshot.sdiv_index, candidate.snapshot.div as u8),
    );
    for _ in 0..595 {
        expected_prefix.next();
    }
    assert_eq!(
        candidate.outcomes(),
        oracle::outcomes_from_prefix(&expected_prefix)
    );

    let trigger = candidate.auto_trigger_snapshot();
    assert_eq!(trigger.advance + 1, candidate.snapshot.advance);
    let mut armed = trigger.rng();
    armed.next();
    assert!(candidate.snapshot.matches(&armed));
}

#[test]
fn tail64_model1_exactly_replays_the_0244_hardware_result() {
    let candidate = Snapshot {
        advance: 2923,
        state: 0x4387,
        div: 0x4b4b,
        adiv_index: 10622,
        sdiv_index: 1032,
    };
    assert_eq!(tail64_model1_outcome(candidate), Some([0x1b, 0xdc]));
}

#[test]
fn tail64_model1_search_finds_the_preregistered_first_shiny_target() {
    let anchor = Snapshot {
        advance: 1900,
        state: 0x202a,
        div: 0x1d1e,
        adiv_index: 9599,
        sdiv_index: 9,
    };
    let mut search = Search::new(anchor, 131_072).unwrap();
    let candidate = search.batch_tail64_model1(20_000).unwrap();
    assert_eq!(candidate.trigger_snapshot.advance, 17_853);
    assert_eq!(candidate.snapshot.advance, 17_854);
    assert_eq!(candidate.snapshot.state, 0xed86);
    assert_eq!(candidate.snapshot.div, 0x5b5b);
    assert_eq!(candidate.outcomes(), &[[0xfa, 0xaa]]);
    assert_eq!(candidate.shiny_mask, 1);
}

#[test]
fn shiny_check_exhaustive_against_pret_bit_conditions() {
    let mut count = 0;
    for ad in 0..=255u8 {
        for ss in 0..=255u8 {
            let expected = (ad >> 4) & 2 != 0 && ad & 15 == 10 && ss >> 4 == 10 && ss & 15 == 10;
            assert_eq!(shiny(ad, ss), expected);
            count += shiny(ad, ss) as usize;
        }
    }
    assert_eq!(count, 8);
}

#[test]
fn div_cycle_and_overflow_match_reference() {
    let mut actual = start().rng();
    let mut expected = oracle::rng::GameboyRng::new(
        start().state,
        oracle::div::Div::new(779, 0x2d),
        oracle::div::Div::new(66, 0x2d),
    );
    for _ in 0..65536 {
        assert_eq!(actual.next(), expected.next());
        assert_eq!(actual.adiv(), expected.adiv());
        assert_eq!(actual.sdiv(), expected.sdiv());
    }
}

#[test]
fn search_range_overflow_and_invalid_indices_fail_closed() {
    let mut s = start();
    s.adiv_index = 0x4000;
    assert!(Search::new(s, 20).is_none());
    s = start();
    s.advance = u32::MAX - 2;
    assert!(Search::new(s, 20).is_none());
    assert!(Search::new(start(), 0).is_none());
}

#[test]
fn search_candidates_have_consistent_shiny_masks() {
    let mut search = Search::new(start(), 20000).unwrap();
    let mut found = 0;
    while search.remaining() > 0 {
        if let Some(c) = search.batch(127) {
            found += 1;
            assert!(c.outcome_count > 0);
            assert!(c.outcome_count as usize <= MAX_OUTCOMES);
            assert_ne!(c.shiny_mask, 0);
            for (i, [ad, ss]) in c.outcomes().iter().copied().enumerate() {
                assert_eq!((c.shiny_mask >> i) & 1 != 0, shiny(ad, ss));
            }
        }
    }
    assert!(found > 0);
}

#[test]
fn auto_arms_one_before_community_target() {
    let (mut session, mut o) = prepared();
    let candidate = session.candidate.unwrap();
    let target = candidate.auto_trigger_snapshot().advance;
    assert_eq!(target + 1, candidate.snapshot.advance);
    assert!(session.tick(o, true, Command::Arm));
    let mut rng = start().rng();
    for n in start().advance + 1..target {
        rng.next();
        o.rng = Some(snapshot(&rng, n));
        let step = session.tick(o, true, Command::None);
        assert_eq!(step, n < target - 1);
    }
    assert_eq!(session.phase, Phase::PreTarget);
    assert!(!session.tick(o, true, Command::None));
    rng.next();
    o.rng = Some(snapshot(&rng, target));
    assert!(!session.tick(o, true, Command::None));
    assert_eq!(session.phase, Phase::Ready);
    assert!(!session.tick(o, true, Command::Trigger));
    assert_eq!(session.phase, Phase::Waiting);
    o.battle = 1;
    o.species = 251;
    o.level = 30;
    o.hp = 100;
    o.max_hp = 100;
    o.dvs = [0x2a, 0xaa];
    for _ in 0..200 {
        session.tick(o, true, Command::None);
    }
    assert_eq!(
        session.phase,
        Phase::Waiting,
        "paused polls must not validate stale DVs"
    );
    // Trigger was accepted while the verified overworld was still visible.
    // A stable Celebi battle is therefore fresh even if the one-frame
    // wBattleMode=1 setup flag is missed while WRAM is bank-switched.
    for _ in 0..5 {
        assert!(!session.tick(o, false, Command::None));
    }
    assert_eq!(session.phase, Phase::Shiny);
    assert!(!session.tick(obs(start()), false, Command::Search));
    assert_eq!(session.phase, Phase::Shiny, "result must remain latched");
}

#[test]
fn wrong_context_unknown_tracking_and_cpu_change_stop() {
    for i in 0..4 {
        let (mut s, mut o) = prepared();
        assert!(s.tick(o, true, Command::Arm));
        match i {
            0 => o.cpu_mhz = 536,
            1 => o.rng = None,
            2 => o.map = (1, 1),
            _ => o.battle = 1,
        }
        assert!(!s.tick(o, true, Command::None));
        assert_eq!(s.phase, Phase::Fault);
        assert!(s.candidate.is_none());
    }
}

#[test]
fn live_rng_shift_discards_stale_target_and_researches_while_frozen() {
    let (mut s, mut o) = prepared();
    let stale = s.candidate;
    assert!(s.tick(o, true, Command::Arm));
    o.rng.as_mut().unwrap().state ^= 1;
    assert!(!s.tick(o, false, Command::None));
    assert_eq!(s.phase, Phase::Searching);
    assert!(s.candidate.is_none());
    assert_ne!(s.anchor, stale.map(|c| c.snapshot));
    assert_eq!(s.anchor, o.rng);
    assert_eq!(s.message, "RNG shifted: re-searching");
}

#[test]
fn hardware_027_div_boundary_fixture_reproduces_the_reported_shift() {
    // AUTO 0.2.7 run 00000001B28E08BE.  The model and hardware agree until
    // the displayed ADIV index wraps through the paired 8/9 adjustment.
    let origin = Snapshot {
        advance: 1899,
        state: 8651,
        div: 7454,
        adiv_index: 9599,
        sdiv_index: 8886,
    };
    let mut target_rng = origin.rng();
    for _ in 0..7000 {
        target_rng.next();
    }
    let target = snapshot(&target_rng, origin.advance + 7000);
    let mut track = Track::new(origin, target).unwrap();
    let mut model = origin.rng();
    for delta in 1..6794 {
        model.next();
        assert_eq!(
            track.observe(snapshot(&model, origin.advance + delta)),
            Position::Before
        );
    }
    model.next();
    let predicted = snapshot(&model, 8693);
    assert_eq!(predicted.state, 62918);
    assert_eq!(predicted.div, 7453);
    assert_eq!(predicted.adiv_index, 9);
    let hardware = Snapshot {
        advance: 8693,
        state: 62662,
        div: 7197,
        adiv_index: 9,
        sdiv_index: 15680,
    };
    assert_eq!(track.observe(hardware), Position::Invalid);
}

#[test]
fn changed_candidate_snapshot_researches_and_cancel_never_steps() {
    let mut s = Session::new();
    let mut o = obs(start());
    s.tick(o, true, Command::Search);
    o.rng.as_mut().unwrap().div ^= 1;
    assert!(!s.tick(o, true, Command::None));
    assert_eq!(s.phase, Phase::Searching);
    assert!(s.candidate.is_none());
    let (mut s, o) = prepared();
    assert!(!s.tick(o, true, Command::Cancel));
    assert_eq!(s.phase, Phase::Idle);
}

#[test]
fn invalid_early_trigger_unpause_and_stall_stop() {
    for cmd in [Command::Trigger, Command::Resume] {
        let (mut s, o) = prepared();
        assert!(!s.tick(o, true, cmd));
        assert_eq!(s.phase, Phase::Fault);
    }
    let (mut s, o) = prepared();
    s.tick(o, true, Command::Arm);
    for _ in 0..122 {
        s.tick(o, true, Command::None);
    }
    assert_eq!(s.phase, Phase::Fault);
}

#[test]
fn overshoot_is_not_ready() {
    let origin = start();
    let mut rng = origin.rng();
    rng.next();
    rng.next();
    let target = snapshot(&rng, origin.advance + 2);
    let mut track = Track::new(origin, target).unwrap();
    rng.next();
    assert_eq!(
        track.observe(snapshot(&rng, origin.advance + 3)),
        Position::Invalid
    );
}

#[test]
fn miss_latches_only_after_fresh_battle_setup_and_reset_is_rejected() {
    let (mut s, mut o) = prepared();
    s.phase = Phase::Waiting;
    o.battle = 1;
    o.species = 251;
    o.level = 30;
    o.hp = 100;
    o.max_hp = 100;
    o.dvs = [0xff, 0xff];
    o.battle_start = 1;
    for _ in 0..4 {
        assert!(!s.tick(o, false, Command::None));
    }
    assert_eq!(s.phase, Phase::Waiting);
    o.battle_start = 0;
    assert!(!s.tick(o, false, Command::None));
    assert_eq!(s.phase, Phase::Miss);
    assert!(!s.tick(o, false, Command::Arm));
    assert_eq!(s.phase, Phase::Miss);
    let (mut s, mut o) = prepared();
    s.phase = Phase::Waiting;
    s.tick(o, false, Command::None);
    o.rng.as_mut().unwrap().advance = 0;
    assert!(!s.tick(o, false, Command::None));
    assert_eq!(s.phase, Phase::Fault);
}

#[test]
fn waiting_ignores_transient_banked_environment_and_accepts_stable_celebi() {
    let (mut s, mut o) = prepared();
    let target = s.candidate.unwrap().auto_trigger_snapshot().advance;
    assert!(s.tick(o, true, Command::Arm));
    let mut rng = start().rng();
    for advance in start().advance + 1..=target {
        rng.next();
        o.rng = Some(snapshot(&rng, advance));
        s.tick(o, true, Command::None);
    }
    assert_eq!(s.phase, Phase::Ready);
    assert!(!s.tick(o, true, Command::Trigger));
    assert_eq!(s.phase, Phase::Waiting);

    // Exact failure mode seen on 0.2.8: encounter transition exposes a
    // non-player WRAM bank, so TID/map read as unrelated values.
    o.tid = 0;
    o.map = (0, 0);
    o.battle = 217;
    assert!(!s.tick(o, false, Command::None));
    assert_eq!(s.phase, Phase::Waiting);

    // The battle setup flag may already be clear by the time stable enemy
    // fields are visible. The accepted pre-battle trigger is the freshness
    // witness; three matching follow-up frames confirm the DVs.
    o.battle = 1;
    o.battle_start = 0;
    o.species = 251;
    o.level = 30;
    o.hp = 100;
    o.max_hp = 100;
    o.battle_ended = 0;
    o.dvs = [0xac, 0x2e];
    for _ in 0..4 {
        assert!(!s.tick(o, false, Command::None));
    }
    assert_eq!(s.phase, Phase::Miss);
    assert_eq!(s.result, Some([0xac, 0x2e]));
}

#[test]
fn auto_input_only_changes_selected_button_bits() {
    for raw in 0u8..=255 {
        for keys in [0, 1, 15] {
            let value = celebi_auto::joyp_with_buttons(raw, keys);
            assert_eq!(value & 0xf0, raw & 0xf0);
            if raw & 0x20 != 0 {
                assert_eq!(value, raw);
            } else {
                assert_eq!(value & 15, (raw & 15) & !keys);
            }
        }
    }
}
#[test]
fn auto_text_detector_does_not_cross_tile_rows() {
    let mut tiles = [0x7f; 360];
    for (i, b) in b"CONTINUE".iter().enumerate() {
        tiles[40 + i] = b - b'A' + 0x80;
    }
    assert!(celebi_auto::tile_contains(&tiles, b"CONTINUE"));
    assert!(!celebi_auto::tile_contains(&tiles, b"NEW GAME"));
    let mut split = [0x7f; 360];
    for (i, b) in b"CONTINUE".iter().enumerate() {
        split[17 + i] = b - b'A' + 0x80;
    }
    assert!(!celebi_auto::tile_contains(&split, b"CONTINUE"));
}

#[test]
fn generic_english_gs_ball_suffix_accepts_different_player_names() {
    fn put_row(tiles: &mut [u8; 360], row: usize, text: &[u8]) {
        for (i, &ch) in text.iter().enumerate() {
            tiles[row * 20 + i] = match ch {
                b'A'..=b'Z' => ch - b'A' + 0x80,
                b'a'..=b'z' => ch - b'a' + 0xa0,
                b' ' => 0x7f,
                b'.' => 0xe8,
                _ => panic!("unsupported fixture character"),
            };
        }
    }

    for first_line in [b"RED put in the".as_slice(), b"ALICE put in the".as_slice()] {
        let mut tiles = [0x7f; 360];
        put_row(&mut tiles, 4, first_line);
        put_row(&mut tiles, 5, b"GS BALL.");
        assert!(celebi_auto::tile_contains(&tiles, b"put in the"));
        assert!(celebi_auto::tile_contains(&tiles, b"GS BALL."));
    }
}
#[test]
fn startup_text_scans_restore_021_and_remain_disabled_in_search_and_battle() {
    use celebi_auto::{text_scan_due, Stage};
    assert!(text_scan_due(Stage::Title, 0, false));
    for tick in 1..16 {
        assert!(text_scan_due(Stage::Title, tick, false));
    }
    assert!(!text_scan_due(Stage::Battle, 0, true));
    assert!(!text_scan_due(Stage::DirectA, 0, true));
    assert!(!text_scan_due(Stage::Advance, 0, true));
    assert!(text_scan_due(Stage::Shrine, 7, true));
    assert!(!text_scan_due(Stage::Shrine, 0, false));
    assert!(text_scan_due(Stage::Track, 0, true));
    assert!(!text_scan_due(Stage::Track, 1, true));
}

#[test]
fn automatic_round_hides_hud_until_terminal_state() {
    use celebi_auto::{hide_hud, Stage};
    for stage in [
        Stage::Title,
        Stage::Continue,
        Stage::Load,
        Stage::Shrine,
        Stage::Track,
        Stage::Search,
        Stage::DirectA,
        Stage::Advance,
        Stage::Battle,
        Stage::SaveResult,
        Stage::Reset,
    ] {
        assert!(hide_hud(stage), "active stage {stage:?} must skip HUD work");
    }
    assert!(!hide_hud(Stage::Shiny));
    assert!(!hide_hud(Stage::Stopped));
}
#[test]
fn intro_uses_021_banks_and_waits_for_game_consumption() {
    use celebi_auto::*;
    let mut b = Bot::new();
    let o = obs(start());
    let mut s = Screen {
        text_sampled: true,
        bank: 0x39,
        wram_bank: 6,
        ..Screen::default()
    };
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.frame = 2;
    s.joy = 1;
    // Keep A held after only the low-level acknowledgement.
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.game_joy = 1;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Title);
    let mut blocked = Bot::new();
    s.bank = 0x24;
    s.joy = 0;
    s.game_joy = 0;
    assert_eq!(blocked.tick(o, s, false, true, false).buttons, 0);
}
#[test]
fn auto_title_continue_and_gs_search_require_observed_states() {
    use celebi_auto::*;
    let mut b = Bot::new();
    let mut o = obs(start());
    // Reset must not depend on any particular trainer ID.
    o.tid = 12345;
    let mut s = Screen {
        bank: 1,
        text_sampled: true,
        ..Screen::default()
    };
    let mut accelerated = Bot::new();
    let mut fast = o;
    fast.cpu_mhz = 804;
    assert_eq!(accelerated.tick(fast, s, false, true, false).buttons, 1);
    assert_eq!(b.tick(o, s, false, true, false).buttons, 1);
    s.frame = 2;
    s.joy = 1;
    s.game_joy = 1;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    s.menu = true;
    s.cursor = 1;
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Continue);
    s.frame = 20;
    s.joy = 0;
    s.game_joy = 0;
    b.tick(o, s, false, true, false);
    s.menu = false;
    s.save_info = true;
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Load);
    s.save_info = false;
    s.world = true;
    s.shrine = true;
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Shrine);
    s.gs = true;
    s.gs_position = true;
    b.tick(o, s, false, true, false);
    assert_eq!(b.stage, Stage::Track);
    let mut unavailable = o;
    unavailable.rng = None;
    assert_eq!(b.tick(unavailable, s, false, true, false).buttons, 0);
    #[cfg(feature = "celebi_natural")]
    {
        assert_eq!(b.stage, Stage::NaturalDelay);
        return;
    }
    #[cfg(feature = "celebi_resolver")]
    {
        assert_eq!(b.stage, Stage::SelectDvs);
        return;
    }
    #[cfg(not(any(feature = "celebi_natural", feature = "celebi_resolver")))]
    assert_eq!(b.stage, Stage::Track);
    assert_eq!(b.tick(o, s, false, true, false).motion, Motion::Pause);
    #[cfg(not(any(feature = "celebi_natural", feature = "celebi_resolver")))]
    assert_eq!(b.stage, Stage::Search);
    #[cfg(not(any(feature = "celebi_natural", feature = "celebi_resolver")))]
    for _ in 0..300 {
        b.tick(o, s, true, true, false);
        if b.stage == Stage::DirectA {
            break;
        }
    }
    #[cfg(not(any(feature = "celebi_natural", feature = "celebi_resolver")))]
    assert_eq!(b.stage, Stage::DirectA);
}
fn auto_at_battle() -> (celebi_auto::Bot, Observation, celebi_auto::Screen) {
    use celebi_auto::*;
    let (session, mut o) = prepared();
    let mut b = Bot::new();
    b.session = session;
    b.stage = Stage::Advance;
    let mut s = Screen {
        gs: true,
        gs_position: true,
        ..Screen::default()
    };
    let mut rng = start().rng();
    let mut paused = true;
    for _ in 0..131072 {
        let result = b.tick(o, s, paused, true, false);
        if b.stage == Stage::ManualA {
            assert_eq!(result.buttons, 0);
            assert_eq!(result.motion, Motion::Step);
            rng.next();
            o.rng = Some(snapshot(&rng, o.rng.unwrap().advance + 1));
            paused = true;
            let pressed = b.tick(o, s, paused, true, false);
            assert_eq!(pressed.buttons, 1);
            assert_eq!(pressed.motion, Motion::Run);
            assert_eq!(b.stage, Stage::Battle);
            o.battle = 1;
            o.species = 251;
            o.level = 30;
            o.hp = 100;
            o.max_hp = 100;
            s.gs = false;
            s.frame = 2;
            s.joy = 1;
            return (b, o, s);
        }
        match result.motion {
            Motion::Run => {
                paused = false;
                rng.next();
                o.rng = Some(snapshot(&rng, o.rng.unwrap().advance + 1));
            }
            Motion::Pause => paused = true,
            Motion::Step => {
                paused = true;
                rng.next();
                o.rng = Some(snapshot(&rng, o.rng.unwrap().advance + 1));
            }
        }
    }
    panic!("did not reach automatic target checkpoint");
}

#[test]
fn automatic_advance_runs_freely_then_pauses_for_the_final_step() {
    use celebi_auto::*;
    let (session, mut o) = prepared();
    let target = session.candidate.unwrap().auto_trigger_snapshot().advance;
    let mut b = Bot::new();
    b.session = session;
    b.stage = Stage::Advance;
    let s = Screen {
        wram_bank: 1,
        gs_position: true,
        ..Screen::default()
    };
    assert_eq!(b.tick(o, s, true, true, false).motion, Motion::Run);
    let mut rng = start().rng();
    for advance in start().advance + 1..target - 1 {
        rng.next();
        o.rng = Some(snapshot(&rng, advance));
        assert_eq!(b.tick(o, s, false, true, false).motion, Motion::Run);
    }
    rng.next();
    o.rng = Some(snapshot(&rng, target - 1));
    assert_eq!(b.tick(o, s, false, true, false).motion, Motion::Pause);
    assert_eq!(b.session.phase, Phase::PreTarget);
    assert_eq!(b.tick(o, s, true, true, false).motion, Motion::Step);
}
#[test]
fn auto_shiny_stops_and_never_resets() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    o.dvs = b.selected_candidate_dvs().unwrap();
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    for _ in 0..5 {
        s.frame += 1;
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Shiny);
    for _ in 0..100 {
        let out = b.tick(o, s, false, true, false);
        assert_eq!(out.buttons, 0);
        assert_eq!(out.motion, Motion::Pause);
    }
}

#[cfg(not(feature = "celebi_natural"))]
#[test]
fn auto_rejects_a_different_shiny_than_the_selected_candidate() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    let selected = b.selected_candidate_dvs().unwrap();
    o.dvs = if selected == [0x2A, 0xAA] {
        [0xFA, 0xAA]
    } else {
        [0x2A, 0xAA]
    };
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    for _ in 0..5 {
        s.frame = s.frame.wrapping_add(1);
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "Shiny differs from selected candidate");
}

#[test]
fn automatic_joyp_a_runs_cutscene_and_validates_fresh_frames() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    o.dvs = b.selected_candidate_dvs().unwrap();
    o.battle_start = 1;

    // The first presentation after automatic JOYP A remains in execution.
    let mut out = b.tick(o, s, false, true, false);
    assert_eq!(out.motion, Motion::Run);
    assert_eq!(b.stage, Stage::Battle);

    o.battle_start = 0;
    for _ in 0..6 {
        s.frame = s.frame.wrapping_add(1);
        out = b.tick(o, s, false, true, false);
        if b.stage == Stage::Shiny {
            break;
        }
        assert_eq!(out.motion, Motion::Run);
    }
    assert_eq!(b.stage, Stage::Shiny);
    assert_eq!(out.motion, Motion::Pause);
}

#[test]
fn automatic_joyp_a_keeps_running_during_graphics_bank_switches() {
    use celebi_auto::*;
    let (mut b, o, mut s) = auto_at_battle();
    s.wram_bank = 6;
    assert_eq!(b.tick(o, s, false, true, false).motion, Motion::Run);
    assert_eq!(b.tick(o, s, false, true, false).motion, Motion::Run);
}
#[test]
fn auto_confirmed_miss_logs_releases_reset_and_restarts_fresh() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    o.dvs = b
        .session
        .candidate
        .unwrap()
        .outcomes()
        .iter()
        .copied()
        .find(|d| !shiny(d[0], d[1]))
        .unwrap();
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    let mut out = b.tick(o, s, false, true, false);
    for _ in 0..6 {
        if b.stage == Stage::SaveResult {
            break;
        }
        s.frame += 1;
        out = b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::SaveResult);
    assert_eq!(out.buttons, 0);
    assert_eq!(out.motion, Motion::Pause);
    b.authorize_verified_reset(true);
    assert_eq!(b.stage, Stage::Reset);
    assert_eq!(b.tick(o, s, true, true, false).buttons, 15);
    s.joy = 15;
    out = b.tick(o, s, false, true, false);
    assert_eq!(out.buttons, 0);
    assert_eq!(b.stage, Stage::Reset);
    o.battle = 0;
    s.joy = 0;
    s.bank = 0x39;
    out = b.tick(o, s, false, true, false);
    assert_eq!(out.buttons, 0);
    assert_eq!(b.stage, Stage::Title);
    assert_eq!(b.session.phase, Phase::Idle);
    assert!(b.session.candidate.is_none());
}

#[test]
fn auto_missed_battle_start_still_logs_and_arms_reset() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    // Exact non-shiny DVs from hardware run 00000009D711362C. The trace had
    // already missed the short battle_start=1 interval.
    o.dvs = [0xac, 0x2e];
    o.battle_start = 0;
    for _ in 0..5 {
        s.frame += 1;
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::SaveResult);
    assert_eq!(b.last_miss, Some([0xac, 0x2e]));
    b.authorize_verified_reset(true);
    assert_eq!(b.stage, Stage::Reset);
    assert_eq!(b.tick(o, s, true, true, false).buttons, 15);
}
#[test]
fn auto_confirmed_nonshiny_outside_model_is_logged_for_retry() {
    use celebi_auto::*;
    let (mut b, mut o, mut s) = auto_at_battle();
    o.dvs = (0u16..=65535)
        .map(|n| [(n >> 8) as u8, n as u8])
        .find(|d| !shiny(d[0], d[1]) && !b.session.candidate.unwrap().outcomes().contains(d))
        .unwrap();
    o.battle_start = 1;
    b.tick(o, s, false, true, false);
    o.battle_start = 0;
    for _ in 0..8 {
        s.frame += 1;
        assert_ne!(b.tick(o, s, false, true, false).buttons, 15);
        if b.stage == Stage::SaveResult {
            break;
        }
    }
    assert_eq!(b.stage, Stage::SaveResult);
    assert_eq!(b.message, "MISS verified: reset pending");
    b.authorize_verified_reset(true);
    assert_eq!(b.stage, Stage::Reset);
    let mut b = Bot::new();
    assert_eq!(b.tick(o, s, false, false, false).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
    let (mut b, o, s) = auto_at_battle();
    assert_eq!(b.tick(o, s, false, true, true).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
}
#[test]
fn auto_wrong_continue_cursor_and_wrong_save_stop() {
    use celebi_auto::*;
    let mut b = Bot::new();
    b.stage = Stage::Continue;
    let s = Screen {
        menu: true,
        cursor: 2,
        text_sampled: true,
        ..Screen::default()
    };
    assert_eq!(b.tick(obs(start()), s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
    let mut b = Bot::new();
    b.stage = Stage::Load;
    let s = Screen {
        world: true,
        shrine: false,
        ..Screen::default()
    };
    assert_eq!(b.tick(obs(start()), s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
}

#[test]
fn auto_hook_return_preserves_registers_and_skips_only_overridden_call() {
    for value in [None, Some(0xce)] {
        let mut regs: [u32; 15] = core::array::from_fn(|i| 0x1000 + i as u32);
        let saved = regs;
        celebi_auto::prepare_read_return(&mut regs, 0x1690b0, value);
        regs.rotate_right(1);
        assert_eq!(regs[0], saved[13]); // trampoline pops LR first
        assert_eq!(regs[1], value.map(|v| v as u32).unwrap_or(saved[0]));
        assert_eq!(&regs[2..14], &saved[1..13]);
        assert_eq!(regs[14], if value.is_some() { saved[13] } else { 0x1690b0 });
    }
}
#[test]
fn auto_no_input_ack_timeout_and_lost_gs_stop_without_reset() {
    use celebi_auto::*;
    let mut b = Bot::new();
    let o = obs(start());
    let mut s = Screen {
        bank: 1,
        text_sampled: true,
        ..Screen::default()
    };
    b.tick(o, s, false, true, false);
    for _ in 0..125 {
        s.frame = s.frame.wrapping_add(1);
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "A not acknowledged");
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    let mut b = Bot::new();
    b.stage = Stage::Track;
    assert_eq!(b.tick(o, s, false, true, false).buttons, 0);
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "GS text lost; no reset");
}

#[test]
fn auto_track_waits_past_old_limit_for_full_div_cycle() {
    use celebi_auto::*;
    let mut b = Bot::new();
    b.stage = Stage::Track;
    let mut o = obs(start());
    o.rng = None;
    let s = Screen {
        wram_bank: 1,
        gs: true,
        gs_position: true,
        ..Screen::default()
    };
    #[cfg(feature = "celebi_natural")]
    {
        b.tick(o, s, false, true, false);
        assert_eq!(b.stage, Stage::NaturalDelay);
        return;
    }
    #[cfg(feature = "celebi_resolver")]
    {
        b.tick(o, s, false, true, false);
        assert_eq!(b.stage, Stage::SelectDvs);
        return;
    }
    for _ in 0..7201 {
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Track);
    for _ in 7201..20001 {
        b.tick(o, s, false, true, false);
    }
    assert_eq!(b.stage, Stage::Stopped);
    assert_eq!(b.message, "Stage timeout; no reset");
}

#[test]
fn divider_phase_tracker_narrows_real_0_2_17_samples_without_future_dvs() {
    let mut tracker = celebi_trace::DivPhaseTracker::new();
    // AUTO 0.2.17 run 000000013BE0564C, all before direct BattleRandom calls.
    tracker.observe(51_972_889, 75);
    tracker.observe(51_990_426, 93);
    tracker.observe(52_007_971, 111);
    let phase = tracker.snapshot().unwrap();
    assert_eq!(phase.base, 1959);
    assert_eq!(phase.count(), 54);
    assert_eq!(phase.mask, (1u64 << 54) - 1);
    tracker.reset();
    assert!(tracker.snapshot().is_none());
}

#[test]
fn divider_phase_tracker_uses_the_second_div_read_boundary() {
    let mut tracker = celebi_trace::DivPhaseTracker::new();
    tracker.observe(1000, 20);
    let before = tracker.snapshot().unwrap().count();
    tracker.observe(1060, 21);
    let after = tracker.snapshot().unwrap().count();
    assert!(after < before);
    for bit in 0..64 {
        if tracker.snapshot().unwrap().mask & (1u64 << bit) != 0 {
            let phase = tracker.snapshot().unwrap().base.wrapping_add(bit) & 0x3fff;
            assert_eq!((((1000u16 + phase) >> 6) & 0xff) as u8, 20);
            assert_eq!((((1060u16 + phase) >> 6) & 0xff) as u8, 21);
        }
    }
}

#[test]
fn pre_trigger_timing_history_pairs_reads_and_keeps_two_cycles() {
    let mut history = celebi_trace::TimingHistory::new();
    // A stray sub read cannot create a partial sample.
    history.observe_sub(9, 1);
    assert_eq!(history.len(), 0);
    for i in 0..520u32 {
        history.observe_add(i + 1, i as u8, 1000 + i * 100, i as u8);
        history.observe_sub(1010 + i * 100, (i + 1) as u8);
    }
    assert_eq!(history.len(), celebi_trace::PRE_TRIGGER_TIMING_CAPACITY);
    let first = history.get(0).unwrap();
    let last = history.get(511).unwrap();
    assert_eq!((first.advance, first.frame), (9, 8));
    assert_eq!((first.add_cycle, first.sub_cycle), (1800, 1810));
    assert_eq!((last.advance, last.frame), (520, 7));
    assert_eq!((last.adiv, last.sdiv), (7, 8));
    assert!(history.get(512).is_none());
    history.reset();
    assert_eq!(history.len(), 0);
}

#[test]
fn native_phase_target_solver_is_exhaustive_and_bounded() {
    use celebi_trace::{advance_native_countdown, phase_delay_to_target};
    for current in 1..=64u8 {
        for original in 1..=64u32 {
            for target in 1..=64u8 {
                let extra = phase_delay_to_target(current, original, target).unwrap();
                assert!(extra <= 63);
                assert_eq!(
                    advance_native_countdown(current, original + extra as u32),
                    Some(target)
                );
            }
        }
    }
    assert_eq!(phase_delay_to_target(0, 4, 1), None);
    assert_eq!(phase_delay_to_target(1, 4, 0), None);
    assert_eq!(phase_delay_to_target(1, 4, 65), None);
}

#[test]
fn native_phase_delay_chunks_never_exceed_one_div_period() {
    use celebi_trace::phase_delay_chunk;
    for original in 1..=64u32 {
        for requested in 0..=63u8 {
            let (adjusted, pending, applied) = phase_delay_chunk(original, requested).unwrap();
            assert!((1..=64).contains(&adjusted));
            assert_eq!(applied as u16 + pending as u16, requested as u16);
            assert_eq!(adjusted, original + applied as u32);
        }
    }
    assert!(phase_delay_chunk(0, 1).is_none());
    assert!(phase_delay_chunk(65, 1).is_none());
}

#[test]
fn native_phase_delay_chunks_preserve_the_exact_total_shift() {
    use celebi_trace::{advance_native_countdown, phase_delay_chunk};
    for initial in 1..=64u8 {
        for requested in 0..=63u8 {
            let instructions = [4u32, 8, 12, 16, 20, 24, 32, 64];
            let mut pending = requested;
            let mut controlled = initial;
            let mut natural = initial;
            for original in instructions {
                let (adjusted, next_pending, _) = phase_delay_chunk(original, pending).unwrap();
                controlled = advance_native_countdown(controlled, adjusted).unwrap();
                natural = advance_native_countdown(natural, original).unwrap();
                pending = next_pending;
            }
            assert_eq!(pending, 0);
            assert_eq!(
                controlled,
                advance_native_countdown(natural, requested as u32).unwrap()
            );
        }
    }
}

#[test]
fn direct_rng_resolver_forces_no_item_then_shiny_dvs() {
    use celebi_trace::DirectRngResolver;

    // Hardware 0.2.46 natural path: E548 --F8--> DD4F --FD--> DA51
    // --FF--> D951.  Only each sub-side FF04 byte is resolved; the add-side
    // byte and Crystal's own adc/sbc instructions remain untouched.
    let mut resolver = DirectRngResolver::new();
    resolver.start();

    resolver.observe_add(0xE548, 0xF8);
    let d1 = resolver.resolve_sub(0xDD48, 0xF8).unwrap();
    assert_eq!(0x48u8.wrapping_sub(d1).wrapping_sub(1), 0x00);

    resolver.observe_add(0xDD00, 0xFD);
    let d2 = resolver.resolve_sub(0xDA00, 0xFD).unwrap();
    assert_eq!(0x00u8.wrapping_sub(d2).wrapping_sub(1), 0xAA);

    resolver.observe_add(0xDAAA, 0xFF);
    let d3 = resolver.resolve_sub(0xD9AA, 0xFF).unwrap();
    assert_eq!(0xAAu8.wrapping_sub(d3).wrapping_sub(1), 0xAA);

    assert_eq!([d1, d2, d3], [0x47, 0x55, 0xFF]);
    assert_eq!(resolver.len(), 3);
    assert!(!resolver.active);
    assert!(!resolver.failed);
    assert_eq!(resolver.get(0).unwrap().desired_output, 0x00);
    assert_eq!(resolver.get(1).unwrap().desired_output, 0xAA);
    assert_eq!(resolver.get(2).unwrap().desired_output, 0xAA);
}

#[test]
fn direct_rng_resolver_uses_the_selected_candidate_dvs() {
    use celebi_trace::DirectRngResolver;

    let mut resolver = DirectRngResolver::new();
    resolver.start_for_dvs([0x2A, 0xAA]);

    resolver.observe_add(0xE548, 0xF8);
    let d1 = resolver.resolve_sub(0xDD48, 0xF8).unwrap();
    assert_eq!(0x48u8.wrapping_sub(d1).wrapping_sub(1), 0x00);

    resolver.observe_add(0xDD00, 0xFD);
    let d2 = resolver.resolve_sub(0xDA00, 0xFD).unwrap();
    assert_eq!(0x00u8.wrapping_sub(d2).wrapping_sub(1), 0x2A);

    resolver.observe_add(0xDA2A, 0xFF);
    let d3 = resolver.resolve_sub(0xD92A, 0xFF).unwrap();
    assert_eq!(0x2Au8.wrapping_sub(d3).wrapping_sub(1), 0xAA);

    assert_eq!(resolver.get(0).unwrap().desired_output, 0x00);
    assert_eq!(resolver.get(1).unwrap().desired_output, 0x2A);
    assert_eq!(resolver.get(2).unwrap().desired_output, 0xAA);
    assert!(!resolver.failed);
}

#[test]
fn direct_rng_resolver_rejects_a_non_shiny_target() {
    use celebi_trace::DirectRngResolver;

    let mut resolver = DirectRngResolver::new();
    resolver.start_for_dvs([0xFF, 0xFF]);
    assert!(!resolver.active);
    assert!(resolver.failed);
    assert_eq!(resolver.failure_code, 4);
}

#[test]
fn direct_rng_resolver_fails_closed_on_impossible_add_state() {
    use celebi_trace::DirectRngResolver;

    let mut resolver = DirectRngResolver::new();
    resolver.start();
    resolver.observe_add(0xE548, 0xF8);
    assert_eq!(resolver.resolve_sub(0x0048, 0xF8), None);
    assert!(resolver.failed);
    assert_eq!(resolver.failure_code, 3);
}

#[test]
fn direct_rng_resolver_math_is_exhaustive() {
    use celebi_trace::{divider_for_sbc_output, infer_adc_carry_out};

    // Exhaust every possible add-side state/DIV pair and both incoming carry
    // values.  For representative old-sub and desired-output bytes, verify
    // that the returned divider makes Crystal's own `sbc b` land exactly on
    // the requested byte, including wraparound.
    for pre_add in 0u16..=0xff {
        for div in 0u16..=0xff {
            for carry_in in 0u16..=1 {
                let sum = pre_add + div + carry_in;
                let post_add = sum as u8;
                let carry_out = sum > 0xff;
                assert_eq!(
                    infer_adc_carry_out(pre_add as u8, div as u8, post_add),
                    Some(carry_out)
                );
                for old_sub in [0x00u8, 0x01, 0x55, 0xaa, 0xff] {
                    for desired in [0x00u8, 0x2a, 0xaa, 0xff] {
                        let returned =
                            divider_for_sbc_output(old_sub, carry_out, desired);
                        assert_eq!(
                            old_sub
                                .wrapping_sub(returned)
                                .wrapping_sub(carry_out as u8),
                            desired
                        );
                    }
                }
            }
        }
    }
}

#[test]
fn branch_filtered_search_returns_a_shiny_candidate_for_that_branch() {
    use celebi_rng::{outcome_mask_satisfies, Search};
    assert!(outcome_mask_satisfies(0b0101, u64::MAX));
    assert!(outcome_mask_satisfies(0b0101, 0b0101));
    assert!(!outcome_mask_satisfies(0b0001, 0b0101));
    assert!(!outcome_mask_satisfies(0, u64::MAX));
    assert!(!outcome_mask_satisfies(0b1111, 0));
    for branch in 0..4 {
        let mut search = Search::new(start(), 131_072).unwrap();
        let candidate = search.batch_for_mask(131_072, 1u64 << branch).unwrap();
        assert_ne!(candidate.shiny_mask & (1u64 << branch), 0);
        assert_eq!(candidate.outcome_count, 4);
    }
}

#[test]
fn nearest_candidate_selects_its_actual_shiny_branch() {
    use celebi_rng::Candidate;

    let candidate = Candidate {
        snapshot: start(),
        trigger_snapshot: start(),
        outcomes: [[0x2A, 0xAA], [0xFA, 0xAA], [0x51, 0x51], [0xAA, 0xAA]],
        outcome_count: 4,
        shiny_mask: 0b1011,
    };

    assert_eq!(candidate.selected_shiny_dvs(0), Some([0x2A, 0xAA]));
    assert_eq!(candidate.selected_shiny_dvs(u64::MAX), Some([0x2A, 0xAA]));
    assert_eq!(candidate.selected_shiny_dvs(0b0010), Some([0xFA, 0xAA]));
    assert_eq!(candidate.selected_shiny_dvs(0b1000), Some([0xAA, 0xAA]));
    assert_eq!(candidate.selected_shiny_dvs(0b0100), Some([0x2A, 0xAA]));
}


#[cfg(not(any(feature = "celebi_natural", feature = "celebi_resolver")))]
#[test]
fn fast_direct_uses_nearest_candidate_dvs_without_waiting_for_its_advance() {
    use celebi_auto::*;
    let (session, o) = prepared();
    let candidate = session.candidate.unwrap();
    assert!(candidate.snapshot.advance > o.rng.unwrap().advance + 1);
    let selected = candidate.selected_shiny_dvs(u64::MAX).unwrap();

    let mut bot = Bot::new();
    bot.session = session;
    bot.stage = Stage::DirectA;
    let screen = Screen {
        wram_bank: 1,
        gs_position: true,
        ..Screen::default()
    };
    let output = bot.tick(o, screen, true, true, false);

    assert_eq!(bot.stage, Stage::Battle);
    assert_eq!(bot.selected_candidate_dvs(), Some(selected));
    assert_eq!(bot.session.phase, Phase::Waiting);
    assert_eq!(output.motion, Motion::Run);
    assert_eq!(output.buttons, 1);
    assert_eq!(o.rng, bot.session.anchor);
    assert_ne!(o.rng, Some(candidate.snapshot));
}

#[cfg(feature = "celebi_natural")]
#[test]
fn natural_release_waits_then_triggers_without_a_candidate() {
    use celebi_auto::*;
    let mut o = obs(start());
    // Human Timing must begin from the visible prompt immediately even when
    // a freshly reset DIV tracker has not identified a snapshot yet.
    o.rng = None;
    let mut bot = Bot::new();
    bot.set_run_entropy(0x1234_5678_9abc_def0);
    bot.stage = Stage::Track;
    let mut screen = Screen {
        wram_bank: 1,
        frame: 10,
        gs_position: true,
        gs: true,
        ..Screen::default()
    };
    let first = bot.tick(o, screen, false, true, false);
    assert_eq!(bot.stage, Stage::NaturalDelay);
    assert_eq!(first.motion, Motion::Pause);
    assert!((12..=180).contains(&bot.natural_delay_frames));
    assert!(bot.natural_timing_class <= 2);
    assert!(bot.session.candidate.is_none());

    let resume = bot.tick(o, screen, true, true, false);
    assert_eq!(resume.motion, Motion::Run);
    for _ in 0..=180 {
        if bot.stage == Stage::NaturalA {
            break;
        }
        screen.frame = screen.frame.wrapping_add(1);
        bot.tick(o, screen, false, true, false);
    }
    assert_eq!(bot.stage, Stage::NaturalA);
    let trigger = bot.tick(o, screen, true, true, false);
    assert_eq!(bot.stage, Stage::Battle);
    assert_eq!(bot.session.phase, Phase::Waiting);
    assert_eq!(trigger.buttons, 1);
    assert!(bot.session.candidate.is_none());
    assert!(bot.session.anchor.is_none());
}

#[cfg(feature = "celebi_natural")]
#[test]
fn natural_human_timing_profile_is_bounded_and_has_a_slow_tail() {
    use celebi_auto::*;
    let o = obs(start());
    let screen = Screen {
        wram_bank: 1,
        frame: 10,
        gs_position: true,
        gs: true,
        ..Screen::default()
    };
    let mut seen = [false; 3];
    for seed in 0..4096u64 {
        let mut bot = Bot::new();
        bot.set_run_entropy(seed.wrapping_mul(0x9E37_79B9_7F4A_7C15));
        bot.stage = Stage::Track;
        bot.tick(o, screen, false, true, false);
        assert!((12..=180).contains(&bot.natural_delay_frames));
        assert!(bot.natural_timing_class <= 2);
        seen[bot.natural_timing_class as usize] = true;
    }
    assert_eq!(seen, [true, true, true]);
}

#[cfg(feature = "celebi_resolver")]
#[test]
fn resolver_release_selects_one_of_eight_pairs_then_triggers_immediately() {
    use celebi_auto::*;
    let mut o = obs(start());
    // The selection screen must not wait for ADIV/SDIV to identify a complete
    // RNG snapshot. The resolver does not consume that snapshot.
    o.rng = None;
    let mut bot = Bot::new();
    bot.stage = Stage::Track;
    let mut screen = Screen {
        wram_bank: 1,
        frame: 10,
        gs_position: true,
        gs: true,
        ..Screen::default()
    };
    let first = bot.tick(o, screen, false, true, false);
    assert_eq!(bot.stage, Stage::SelectDvs);
    assert_eq!(first.motion, Motion::Pause);
    assert_eq!(bot.selected_resolver_dvs(), [0xfa, 0xaa]);

    screen.physical_x = true;
    bot.tick(o, screen, true, true, false);
    assert_eq!(bot.selected_resolver_dvs(), [0x2a, 0xaa]);
    screen.physical_x = false;
    bot.tick(o, screen, true, true, false);
    screen.physical_y = true;
    bot.tick(o, screen, true, true, false);
    assert_eq!(bot.selected_resolver_dvs(), [0xfa, 0xaa]);
    screen.physical_y = false;
    bot.tick(o, screen, true, true, false);
    screen.physical_a = true;
    bot.tick(o, screen, true, true, false);
    assert_eq!(bot.stage, Stage::DirectA);
    let trigger = bot.tick(o, screen, true, true, false);
    assert_eq!(bot.stage, Stage::Battle);
    assert_eq!(bot.session.phase, Phase::Waiting);
    assert_eq!(trigger.buttons, 1);
    assert!(bot.session.candidate.is_none());
}

#[test]
fn phase_learning_locks_a_matching_public_branch_then_rejects_drift() {
    use celebi_auto::Bot;
    use celebi_rng::Candidate;
    use celebi_trace::NativePhaseDelayState;

    let mut bot = Bot::new();
    bot.phase_target = 64;
    bot.session.candidate = Some(Candidate {
        snapshot: start(),
        trigger_snapshot: start(),
        outcomes: [[0x10, 0x20], [0x30, 0x40], [0x50, 0x60], [0x70, 0x80]],
        outcome_count: 4,
        shiny_mask: 0b0100,
    });
    bot.session.result = Some([0x50, 0x60]);
    let complete = NativePhaseDelayState {
        requested: 64,
        pending: 0,
        applied: 7,
        arm_source: 2,
        failure_code: 0,
        countdown_before: 31,
        input_delta: 4,
        written_cycles: 11,
        readback_cycles: 11,
        armed: false,
        complete: true,
        failed: false,
    };
    bot.observe_phase_result(complete);
    assert_eq!(bot.learned_phase_target, 64);
    assert_eq!(bot.learned_outcome_mask, 0b0100);
    assert_eq!(bot.phase_samples, 0);
    assert_eq!(bot.active_phase_target(), 64);

    bot.session.result = Some([0x30, 0x40]);
    bot.observe_phase_result(complete);
    assert_eq!(bot.learned_phase_target, 0);
    assert_eq!(bot.learned_outcome_mask, 0);
    assert_eq!(bot.phase_target, 1);
    assert_eq!(bot.phase_samples, 0);
}

#[test]
fn phase_scan_stops_after_one_complete_cycle_without_a_public_branch() {
    use celebi_auto::{Bot, Stage};
    use celebi_rng::Candidate;
    use celebi_trace::NativePhaseDelayState;

    let mut bot = Bot::new();
    bot.session.candidate = Some(Candidate {
        snapshot: start(),
        trigger_snapshot: start(),
        outcomes: [[1, 2], [3, 4], [5, 6], [7, 8]],
        outcome_count: 4,
        shiny_mask: 1,
    });
    bot.session.result = Some([9, 10]);
    let complete = NativePhaseDelayState {
        requested: 1,
        pending: 0,
        applied: 0,
        arm_source: 2,
        failure_code: 0,
        countdown_before: 1,
        input_delta: 4,
        written_cycles: 0,
        readback_cycles: 0,
        armed: false,
        complete: true,
        failed: false,
    };
    for _ in 0..63 {
        bot.observe_phase_result(complete);
        assert_ne!(bot.stage, Stage::Stopped);
    }
    bot.observe_phase_result(complete);
    assert_eq!(bot.phase_samples, 64);
    assert_eq!(bot.stage, Stage::Stopped);
    assert_eq!(bot.message, "64 phases tried; no public branch");
}
