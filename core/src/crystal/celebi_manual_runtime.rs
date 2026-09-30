//! Invisible fixed-target resolver. All player input remains physical.

use super::{
    celebi,
    celebi_auto::tile_contains,
    celebi_manual::{supported_cpu_mhz, BattleSample, ManualController, ManualEvent, ManualState},
    game_lib::gb_mem,
    hook,
};

const TARGET_DVS: [u8; 2] = [0xfa, 0xaa];

static mut CONTROLLER: ManualController = ManualController::new();
static mut RESOLVER_RELEASED: bool = false;

fn exact_gs_ball_prompt(cpu_mhz: u32) -> bool {
    if !supported_cpu_mhz(cpu_mhz) {
        return false;
    }
    let bank = gb_mem::read_u8(0xd439);
    let pos = gb_mem::read_u8(0xd43a) as u16 | (gb_mem::read_u8(0xd43b) as u16) << 8;
    if bank != 0x1b || !matches!(pos, 0x6e53 | 0x6e54) {
        return false;
    }
    let obs = celebi::observe(cpu_mhz);
    if obs.map != (3, 52) || obs.battle != 0 {
        return false;
    }
    let mut tiles = [0u8; 360];
    for (i, tile) in tiles.iter_mut().enumerate() {
        *tile = gb_mem::read_u8(0xc4a0 + i as u32);
    }
    tile_contains(&tiles, b"put in the") && tile_contains(&tiles, b"GS BALL.")
}

fn battle_sample(cpu_mhz: u32) -> BattleSample {
    let obs = celebi::observe_battle(cpu_mhz);
    BattleSample {
        battle: obs.battle,
        battle_start: obs.battle_start,
        battle_ended: obs.battle_ended,
        species: obs.species,
        level: obs.level,
        hp: obs.hp,
        max_hp: obs.max_hp,
        dvs: obs.dvs,
    }
}

#[no_mangle]
pub extern "C" fn celebi_auto_enabled() -> bool {
    true
}

#[no_mangle]
pub extern "C" fn celebi_auto_mode() -> u32 {
    3
}

#[no_mangle]
pub extern "C" fn celebi_auto_busy() -> bool {
    true
}

pub fn buttons() -> u8 {
    0
}

#[no_mangle]
pub extern "C" fn celebi_auto_host_buttons() -> u32 {
    0
}

#[no_mangle]
pub extern "C" fn celebi_auto_service(_paused: bool, physical_keys: u32, cpu_mhz: u32) -> u32 {
    let physical_a = physical_keys & 1 != 0;
    unsafe {
        match CONTROLLER.state {
            ManualState::SeekPrompt | ManualState::Ready => {
                let exact_prompt = exact_gs_ball_prompt(cpu_mhz);
                if CONTROLLER.observe_prompt(exact_prompt, physical_a) == ManualEvent::ArmResolver {
                    hook::begin_full_phase_probe(0, TARGET_DVS);
                    RESOLVER_RELEASED = false;
                }
            }
            ManualState::Running => {
                let resolver = hook::direct_rng_resolver();
                if resolver.failed {
                    hook::end_full_phase_probe();
                    RESOLVER_RELEASED = true;
                    CONTROLLER.finish(false);
                    return 1;
                }
                if !RESOLVER_RELEASED && resolver.len() == 3 && !resolver.active {
                    hook::end_full_phase_probe();
                    RESOLVER_RELEASED = true;
                }
                if let ManualEvent::BattleResult(dvs) = CONTROLLER.observe_battle(battle_sample(cpu_mhz)) {
                    let complete = RESOLVER_RELEASED && dvs == TARGET_DVS;
                    if !RESOLVER_RELEASED {
                        hook::end_full_phase_probe();
                        RESOLVER_RELEASED = true;
                    }
                    CONTROLLER.finish(complete);
                }
            }
            ManualState::Done | ManualState::Failed => {}
        }
    }
    1
}

#[no_mangle]
pub extern "C" fn celebi_auto_draw() {}

#[no_mangle]
pub extern "C" fn celebi_auto_stopped() -> bool {
    unsafe { CONTROLLER.terminal() }
}
