use super::{
    celebi,
    celebi_auto::{
        hide_hud, natural_mode, resolver_mode, tile_contains, Bot, Motion, Screen, Stage,
    },
    game_lib::gb_mem,
    hook,
};
use crate::pnp;
#[cfg(feature = "celebi_diagnostics")]
use super::celebi_auto::build_mode_name;

static mut BOT: Bot = if cfg!(feature = "celebi_calibration") {
    Bot::calibration()
} else {
    Bot::new()
};
static mut BUTTONS: u8 = 0;
static mut LAST_SCREEN: Option<Screen> = None;
static mut LAST_CPU: u32 = 0;
#[cfg(feature = "celebi_diagnostics")]
static mut TRACE: super::celebi_trace::Trace = super::celebi_trace::Trace::new();
// Diagnostic builds only: 0 not attempted, 1 startup saved, 2 terminal saved, 3 failed.
#[cfg(feature = "celebi_diagnostics")]
static mut LOG_STATUS: u8 = 0;
#[cfg(feature = "celebi_diagnostics")]
static mut LOG_ERROR: u32 = 0;
static mut RUN_ID: u64 = 0;
static mut INITIALIZED: bool = false;
// One-shot terminal handling. In release builds this gates the in-memory
// result verification/reset transition; it does not imply filesystem I/O.
static mut LOG_FINAL_ATTEMPTED: bool = false;
static mut TEXT_STAGE: Stage = Stage::Stopped;
static mut TEXT_TICK: u8 = 0;
static mut TEXT_NO_SAVE: bool = false;
static mut TEXT_MENU: bool = false;
static mut TEXT_SAVE_INFO: bool = false;
static mut TEXT_GS: bool = false;

#[cfg(all(not(feature = "test_stubs"), feature = "celebi_diagnostics"))]
extern "C" {
    fn celebi_log_write(data: *const u8, size: u32) -> u32;
    fn celebi_hid_ready() -> u32;
    fn celebi_hid_injection_frames() -> u32;
}
#[cfg(all(feature = "test_stubs", feature = "celebi_diagnostics"))]
unsafe fn celebi_log_write(_: *const u8, _: u32) -> u32 {
    0
}
#[cfg(all(feature = "test_stubs", feature = "celebi_diagnostics"))]
unsafe fn celebi_hid_ready() -> u32 {
    1
}
#[cfg(all(feature = "test_stubs", feature = "celebi_diagnostics"))]
unsafe fn celebi_hid_injection_frames() -> u32 {
    0
}

#[no_mangle]
pub extern "C" fn celebi_auto_enabled() -> bool {
    cfg!(feature = "celebi_auto")
}

#[no_mangle]
pub extern "C" fn celebi_auto_mode() -> u32 {
    if natural_mode() {
        1
    } else if resolver_mode() {
        2
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn celebi_auto_busy() -> bool {
    unsafe { hide_hud(BOT.stage) }
}

pub fn buttons() -> u8 {
    unsafe { super::celebi_auto::joyp_buttons_for_stage(BOT.stage, BUTTONS) }
}

#[no_mangle]
pub extern "C" fn celebi_auto_host_buttons() -> u32 {
    unsafe { super::celebi_auto::host_buttons_for_stage(BOT.stage, BUTTONS) as u32 }
}

#[inline(never)]
fn screen(stage: Stage) -> Screen {
    let bank = gb_mem::read_u8(0xd439);
    // Script positions are little endian, unlike the big-endian Pokemon fields.
    let pos = gb_mem::read_u8(0xd43a) as u16 | (gb_mem::read_u8(0xd43b) as u16) << 8;
    let world = gb_mem::read_u8(0xcfbc) & 1 != 0 && gb_mem::read_u8(0xd432) == 2;
    let gs_position = bank == 0x1b && (pos == 0x6e53 || pos == 0x6e54);
    let text_sampled = unsafe {
        if TEXT_STAGE != stage {
            TEXT_STAGE = stage;
            TEXT_TICK = 0;
            TEXT_NO_SAVE = false;
            TEXT_MENU = false;
            TEXT_SAVE_INFO = false;
            TEXT_GS = false;
        }
        let sample = super::celebi_auto::text_scan_due(stage, TEXT_TICK, gs_position);
        TEXT_TICK = (TEXT_TICK + 1) & 15;
        if sample {
            let mut tiles = [0u8; 360];
            for (i, tile) in tiles.iter_mut().enumerate() {
                *tile = gb_mem::read_u8(0xc4a0 + i as u32);
            }
            let has = |text: &[u8]| tile_contains(&tiles, text);
            TEXT_NO_SAVE = has(b"NEW GAME") && has(b"OPTION") && !has(b"CONTINUE") && !has(b"PLAYER");
            TEXT_MENU = has(b"CONTINUE") && has(b"NEW GAME");
            // The save-summary labels are stable in English Crystal; the
            // rendered trainer name is intentionally not inspected.
            TEXT_SAVE_INFO = has(b"PLAYER") && has(b"BADGES");
            // Text_InsertGSBall begins with the variable <PLAYER> token. Match
            // only the invariant suffix and keep the exact script-position
            // guard, so every valid English trainer name is accepted without
            // widening this to unrelated text boxes.
            TEXT_GS = gs_position && has(b"put in the") && has(b"GS BALL.");
        }
        sample
    };
    Screen {
        wram_bank: gb_mem::read_u8(0xff70),
        script_bank: bank,
        script_pos: pos,
        x: gb_mem::read_u8(0xdcb8),
        y: gb_mem::read_u8(0xdcb7),
        bank: gb_mem::read_u8(0xff9d),
        frame: gb_mem::read_u8(0xff9b),
        joy: gb_mem::read_u8(0xffa4),
        joy_pressed: gb_mem::read_u8(0xffa7),
        game_joy: gb_mem::read_u8(0xffa8),
        joy_last: gb_mem::read_u8(0xffa9),
        text_sampled,
        no_save_menu: unsafe { TEXT_NO_SAVE },
        menu: unsafe { TEXT_MENU },
        cursor: gb_mem::read_u8(0xcfa9),
        save_info: unsafe { TEXT_SAVE_INFO },
        world,
        shrine: gb_mem::read_u8(0xdcb8) == 8
            && gb_mem::read_u8(0xdcb7) == 23
            && gb_mem::read_u8(0xd4de) & 12 == 4
            && gb_mem::read_u8(0xd437) == 0,
        dialog: bank == 0x1b && (0x6e39..=0x6e42).contains(&pos),
        gs_position,
        gs: gs_position && unsafe { TEXT_GS },
        physical_a: false,
        physical_x: false,
        physical_y: false,
    }
}

#[inline(never)]
fn battle_screen() -> Screen {
    // Keep only the HRAM values needed to acknowledge and release the automatic
    // JOYP A. Fixed host windows are captured separately in the hook.
    Screen {
        frame: gb_mem::read_u8(0xff9b),
        joy: gb_mem::read_u8(0xffa4),
        joy_pressed: gb_mem::read_u8(0xffa7),
        game_joy: gb_mem::read_u8(0xffa8),
        joy_last: gb_mem::read_u8(0xffa9),
        ..Screen::default()
    }
}

#[inline(never)]
#[cfg(feature = "celebi_diagnostics")]
fn append_terminal_evidence(report: &mut alloc::string::String) {
    // Keep the 360-byte tile buffer and its formatting frame out of the
    // service callback. The callback runs on the game's thread stack.
    let tiles: [u8; 360] = core::array::from_fn(|i| gb_mem::read_u8(0xc4a0 + i as u32));
    report.pop();
    report.pop(); // final newline and object brace
    let delay = hook::native_phase_delay();
    let bot = unsafe { &BOT };
    report.push_str(&alloc::format!(
        ",\"native_timer_layout\":{{\"last_instruction_cycles_addr\":2291204,\"guest_cycle_total_addr\":2292004,\"div_countdown_addr\":2292304}},\"phase_controller\":{{\"control_point\":\"disabled_direct_rng_resolver\",\"requested_target\":{},\"pending\":{},\"applied\":{},\"arm_source\":{},\"failure_code\":{},\"countdown_before\":{},\"input_delta\":{},\"written_cycles\":{},\"readback_cycles\":{},\"complete\":{},\"failed\":{},\"samples\":{},\"next_scan_target\":{},\"learned_target\":{},\"learned_outcome_mask\":{}}},\"hid_ready\":{},\"hid_injection_frames\":{},\"terminal_tiles\":{:?},\"terminal_wram_bank\":{}}}\n",
        delay.requested,
        delay.pending,
        delay.applied,
        delay.arm_source,
        delay.failure_code,
        delay.countdown_before,
        delay.input_delta,
        delay.written_cycles,
        delay.readback_cycles,
        delay.complete,
        delay.failed,
        bot.phase_samples,
        bot.phase_target,
        bot.learned_phase_target,
        bot.learned_outcome_mask,
        unsafe { celebi_hid_ready() },
        unsafe { celebi_hid_injection_frames() },
        tiles,
        gb_mem::read_u8(0xff70)
    ));
}

#[no_mangle]
pub extern "C" fn celebi_auto_service(paused: bool, physical_keys: u32, cpu_mhz: u32) -> u32 {
    let stage = unsafe { BOT.stage };
    let critical = stage == Stage::Battle;
    let obs = if critical {
        celebi::observe_battle(cpu_mhz)
    } else {
        celebi::observe(cpu_mhz)
    };
    let mut s = if critical { battle_screen() } else { screen(stage) };
    s.physical_a = physical_keys & 1 != 0;
    s.physical_x = physical_keys & (1 << 10) != 0;
    s.physical_y = physical_keys & (1 << 11) != 0;
    unsafe {
        LAST_SCREEN = Some(s);
        LAST_CPU = cpu_mhz;
        if !INITIALIZED {
            RUN_ID = crate::pnp::system_tick();
            BOT.set_run_entropy(RUN_ID);
            INITIALIZED = true;
            #[cfg(feature = "celebi_diagnostics")]
            {
            // Development builds can opt into a durable audit. Public release
            // builds compile this whole block out and create no SD files.
            let policy = if BOT.calibration {
                "single_sample_no_reset"
            } else if natural_mode() {
                "unmodified_rng_auto_reset_until_shiny"
            } else if resolver_mode() {
                "selected_shiny_resolver_fail_closed"
            } else {
                "single_shiny_resolver_fail_closed"
            };
            let (rng_model, trigger_mode, phase_control) = if natural_mode() {
                (
                    "game_rng_unmodified_human_profile_press_time",
                    "prompt_system_tick_plus_attempt_then_joyp_a",
                    "none_original_ff04_only",
                )
            } else if resolver_mode() {
                (
                    "user_selected_gen2_shiny_dvs",
                    "verified_gs_boundary_immediate_after_confirmation",
                    "natural_add_reads_resolved_sub_reads_00_selected_selected",
                )
            } else {
                (
                    "nearest_public_shiny_dvs_fast_direct",
                    "frozen_anchor_immediate_after_search",
                    "natural_add_reads_resolved_sub_reads_00_candidate_candidate",
                )
            };
            let header = alloc::format!("{{\"schema\":1,\"version\":\"2.1.2-dev\",\"kind\":\"startup\",\"run_id\":\"{:016X}\",\"title_id\":\"0004000000172800\",\"cpu_mhz\":{},\"build_mode\":\"{}\",\"policy\":\"{}\",\"input\":\"joyp_auto\",\"rng_model\":\"{}\",\"trigger_mode\":\"{}\",\"phase_control\":\"{}\",\"resolver_permitted\":{},\"log_limit_mib\":128}}\n", RUN_ID, cpu_mhz, build_mode_name(), policy, rng_model, trigger_mode, phase_control, resolver_mode());
            LOG_ERROR = celebi_log_write(header.as_ptr(), header.len() as u32);
            LOG_STATUS = if LOG_ERROR == 0 { 1 } else { 3 };
            if LOG_ERROR != 0 {
                BOT.stop("Log unavailable; no inputs");
                LOG_FINAL_ATTEMPTED = true;
                return 2;
            }
            }
        }
        let previous = BOT.stage;
        if natural_mode() && previous == Stage::Track {
            // A human cannot condition reaction time on hidden game RNG. Use
            // only independent host time at the visible prompt (plus attempt
            // number inside Bot) to choose the disclosed timing profile.
            BOT.set_run_entropy(RUN_ID ^ crate::pnp::system_tick());
        }
        let mut output = BOT.tick(
            obs,
            s,
            paused,
            hook::input_hook_verified(),
            physical_keys & 2 != 0,
        );
        if previous == Stage::Reset && BOT.stage == Stage::Stopped {
            // Preserve a cancellation/timeout after a verified MISS.
            LOG_FINAL_ATTEMPTED = false;
        }
        if !LOG_FINAL_ATTEMPTED {
            #[cfg(feature = "celebi_diagnostics")]
            {
                TRACE.update(previous, BOT.stage, obs, s, output.buttons, paused);
            }
            if !natural_mode()
                && matches!(previous, Stage::DirectA | Stage::ManualA)
                && BOT.stage == Stage::Battle
            {
                // Record the exact FF04 bytes consumed at candidate+607, the
                // classified final VBlank, and the following direct Random
                // calls. No host-memory window is copied on this path.
                // Keep native-cycle injection disabled. The direct resolver
                // leaves every add-side FF04 read natural and resolves only
                // the three sub-side values used for item/DV generation.
                let desired_dvs = BOT.active_target_dvs();
                if let Some(desired_dvs) = desired_dvs {
                    hook::begin_full_phase_probe(0, desired_dvs);
                } else {
                    output = BOT.stop("Candidate has no selected shiny DVs");
                }
            } else if previous == Stage::NaturalA && BOT.stage == Stage::Battle {
                // The production Natural build leaves every FF04 read alone.
                // Optional development diagnostics can observe JOYP without
                // changing the game-provided value.
                #[cfg(feature = "celebi_diagnostics")]
                {
                    hook::begin_passive_battle();
                    hook::start_input_trace();
                }
            }
        }
        if hook::native_phase_delay_failed()
            && !matches!(BOT.stage, Stage::Stopped | Stage::Shiny | Stage::SaveResult)
        {
            output = BOT.stop("Native phase control failed; no reset");
        }
        BUTTONS = output.buttons;
        if !LOG_FINAL_ATTEMPTED && matches!(BOT.stage, Stage::Stopped | Stage::Shiny | Stage::SaveResult) {
            // The callback will remain paused; all virtual buttons are released.
            LOG_FINAL_ATTEMPTED = true;
            #[cfg(feature = "celebi_diagnostics")]
            let reads = hook::finish_input_trace();
            hook::end_full_phase_probe();
            #[cfg(feature = "celebi_diagnostics")]
            {
                hook::end_passive_battle();
            }
            hook::end_lean_battle();
            #[cfg(feature = "celebi_diagnostics")]
            {
                hook::finish_battle_rng_trace();
            }
            if matches!(BOT.stage, Stage::SaveResult | Stage::Shiny) && !natural_mode() {
                let delay = hook::native_phase_delay();
                if delay.failed || !delay.complete {
                    hook::mark_native_phase_incomplete();
                    output = BOT.stop("Native phase incomplete; no reset");
                    BUTTONS = output.buttons;
                }
                let resolver = hook::direct_rng_resolver();
                if resolver.failed || resolver.len() != 3 {
                    output = BOT.stop("Direct RNG resolver incomplete; no reset");
                    BUTTONS = output.buttons;
                }
            }
            #[cfg(feature = "celebi_diagnostics")]
            if matches!(BOT.stage, Stage::SaveResult | Stage::Shiny) && natural_mode() {
                let resolver = hook::direct_rng_resolver();
                if resolver.active || resolver.failed || resolver.len() != 0 {
                    output = BOT.stop("Natural audit failed: resolver touched");
                    BUTTONS = output.buttons;
                }
            }
            #[cfg(feature = "celebi_diagnostics")]
            {
            let mut report = TRACE.report(
                RUN_ID,
                &BOT,
                reads,
                hook::action_edge_context(),
                hook::input_hook_target(),
                hook::battle_rng_trace(),
                hook::final_phase_trace(),
                hook::direct_rng_resolver(),
                hook::phase_barrier_trace(),
            );
            // Preserve raw text evidence for unrecognized title/menu failures.
            // This is read/formatted only after stopping, not during the A pulse.
            append_terminal_evidence(&mut report);
            LOG_ERROR = celebi_log_write(report.as_ptr(), report.len() as u32);
            LOG_STATUS = if LOG_ERROR == 0 { 2 } else { 3 };
            BOT.authorize_verified_reset(LOG_ERROR == 0);
            // Never apply reset keys on the callback that writes the result.
            // On failure, preserve this encounter and stop immediately.
            if LOG_ERROR != 0 {
                BOT.stop("Log failed; no reset");
            }
            }
            #[cfg(not(feature = "celebi_diagnostics"))]
            {
                // `Session::Miss` is latched only after stable, fresh Celebi
                // fields and a second shiny check. That in-memory verification
                // is the production reset gate; no SD audit file is required.
                BOT.authorize_verified_reset(true);
            }
            if BOT.stage == Stage::Reset && !natural_mode() {
                // A completed resolver encounter is expected to be shiny.
                // Any non-shiny result is preserved for diagnosis and never
                // enters an automatic reset loop in the stable release.
                output = BOT.stop("Unexpected non-shiny; stopped safely");
                BUTTONS = output.buttons;
                return 2;
            }
        }
        if previous == Stage::Reset && BOT.stage == Stage::Title {
            hook::reset_trackers();
            hook::reset_runtime_traces();
            LOG_FINAL_ATTEMPTED = false;
            #[cfg(feature = "celebi_diagnostics")]
            {
                TRACE.reset();
                LOG_STATUS = 0;
            }
        }
        match output.motion {
            Motion::Run => 1,
            Motion::Pause => 2,
            Motion::Step => 3,
        }
    }
}

#[no_mangle]
pub extern "C" fn celebi_auto_draw() {
    pnp::set_print_max_len(30);
    let bot = unsafe { &BOT };
    // Public release screens are deliberately small. Development diagnostics
    // are a separate opt-in build and never appear in the release binaries.
    if bot.stage == Stage::Shiny {
        pnp::println!("SHINY CELEBI FOUND!");
        if let Some(dvs) = bot.session.result {
            pnp::println!("DVs {:02X} {:02X}", dvs[0], dvs[1]);
        }
        pnp::println!("Attempts {}", bot.attempts);
        pnp::println!("R: continue + hide");
        return;
    }
    if bot.stage == Stage::Stopped {
        pnp::println!("CELEBI AUTO STOPPED");
        pnp::println!("{}", bot.message);
        pnp::println!("Attempts {}", bot.attempts);
        pnp::println!("R: continue + hide");
        return;
    }
    #[cfg(feature = "celebi_resolver")]
    if bot.stage == Stage::SelectDvs {
        let dvs = bot.selected_resolver_dvs();
        pnp::println!("UNSUPPORTED DEVELOPMENT MODE");
        pnp::println!("Shiny DVs > {:02X} {:02X}", dvs[0], dvs[1]);
        pnp::println!("X/Y: change");
        pnp::println!("A: confirm   B: stop");
        return;
    }
    #[cfg(not(feature = "celebi_diagnostics"))]
    {
        #[cfg(feature = "celebi_natural")]
        {
            pnp::println!("CELEBIHUNTER RESET 1.0.0");
            pnp::println!("{}", bot.message);
            if bot.stage == Stage::NaturalDelay {
                pnp::println!("A in {} frames", bot.natural_delay_remaining);
            } else {
                pnp::println!("Final A: automatic");
            }
            pnp::println!("Attempts {}", bot.attempts);
            pnp::println!("B: stop");
            return;
        }
        #[cfg(feature = "celebi_resolver")]
        {
            pnp::println!("CELEBI 2.1.2 RESOLVER");
            pnp::println!("{}", bot.message);
            pnp::println!("B: stop");
            return;
        }
    }
    #[cfg(feature = "celebi_diagnostics")]
    {
    if cfg!(feature = "celebi_calibration") {
        pnp::println!("CELEBI CAL 2.1.2 DEV");
        match unsafe { LOG_STATUS } {
            2 => pnp::println!("LOG SAVED (SD root)"),
            3 => pnp::println!("LOG ERROR {:08X}", unsafe { LOG_ERROR }),
            _ => pnp::println!("LOG: sampling / no reset"),
        }
        pnp::println!("Development logger disabled");
    } else if natural_mode() {
        pnp::println!("CELEBI 2.1.2 NATURAL DEV");
        match unsafe { LOG_STATUS } {
            2 => pnp::println!("AUDIT LOG SAVED"),
            3 => pnp::println!("LOG ERROR {:08X}", unsafe { LOG_ERROR }),
            _ => pnp::println!("RNG/DVs: UNMODIFIED"),
        }
        pnp::println!("Development logger disabled");
    } else if resolver_mode() {
        pnp::println!("CELEBI 2.1.2 RESOLVER DEV");
        match unsafe { LOG_STATUS } {
            2 => pnp::println!("LOG SAVED (SD root)"),
            3 => pnp::println!("LOG ERROR {:08X}", unsafe { LOG_ERROR }),
            _ => pnp::println!("X/Y choose, A confirms"),
        }
        pnp::println!("Development logger disabled");
    } else {
        pnp::println!("CELEBI AUTO 2.1.2 LEGACY DEV");
        match unsafe { LOG_STATUS } {
            2 => pnp::println!("LOG SAVED (SD root)"),
            3 => pnp::println!("LOG ERROR {:08X}", unsafe { LOG_ERROR }),
            _ => pnp::println!("LOG: SD root / sampling"),
        }
    }
    pnp::println!("{:?}", bot.stage);
    pnp::println!("{}", bot.message);
    pnp::println!("Final A: automatic JOYP");
    if natural_mode() {
        pnp::println!("Original RNG; shiny stops");
        pnp::println!(
            "Delay {}/{} class {}",
            bot.natural_delay_remaining,
            bot.natural_delay_frames,
            bot.natural_timing_class
        );
    } else if resolver_mode() {
        let dvs = bot.selected_resolver_dvs();
        pnp::println!("Selected DVs {:02X} {:02X}", dvs[0], dvs[1]);
    } else {
        pnp::println!("No input needed; shiny stops");
    }
    pnp::println!("B: STOP / R: resume + hide HUD");
    pnp::println!(
        "CPU {} / {}",
        unsafe { LAST_CPU },
        super::celebi_session::EXPECTED_CPU_MHZ
    );
    pnp::println!(
        "Attempts {} / {}",
        bot.attempts,
        if natural_mode() { 32_768 } else { 1_024 }
    );
    pnp::println!(
        "Phase {} scan {} learned {}/{}",
        bot.active_phase_target(),
        bot.phase_samples,
        bot.learned_phase_target,
        bot.learned_outcome_mask
    );
    pnp::println!("RNG {}", hook::rng_advance());
    if let Some(c) = bot.session.candidate {
        pnp::println!(
            "Target {} Trigger {}",
            c.snapshot.advance,
            c.auto_trigger_snapshot().advance
        );
        if let Some(dvs) = bot.active_target_dvs() {
            pnp::println!("Target DVs {:02X} {:02X}", dvs[0], dvs[1]);
        }
    }
    if let Some(dvs) = bot.session.result.or(bot.last_miss) {
        pnp::println!("DVs {:02X} {:02X}", dvs[0], dvs[1]);
    }
    if let Some(s) = unsafe { LAST_SCREEN } {
        pnp::println!("Menu {} World {} GS {}", s.menu as u8, s.world as u8, s.gs as u8);
        pnp::println!(
            "Keys {:02X}/{:02X} WRAM {}",
            s.joy,
            s.joy_pressed,
            s.wram_bank & 7
        );
        pnp::println!("Game keys {:02X} cursor {}", s.game_joy, s.cursor);
        pnp::println!("Script {:02X}:{:04X}", s.script_bank, s.script_pos);
        pnp::println!("XY {}:{}", s.x, s.y);
    }
    pnp::println!(
        "Hook {} read {:08X}",
        hook::input_hook_verified() as u8,
        hook::input_hook_target()
    );
    pnp::println!("HID {} inject {}", unsafe { celebi_hid_ready() }, unsafe {
        celebi_hid_injection_frames()
    });
    }
}

#[no_mangle]
pub extern "C" fn celebi_auto_stopped() -> bool {
    unsafe { matches!(BOT.stage, Stage::Stopped | Stage::Shiny) }
}
