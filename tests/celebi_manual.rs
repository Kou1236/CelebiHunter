#![allow(dead_code)]

#[path = "../core/src/crystal/celebi_manual.rs"]
mod celebi_manual;

use celebi_manual::{supported_cpu_mhz, BattleSample, ManualController, ManualEvent, ManualState};

#[test]
fn accepts_old_and_new_3ds_clock_modes() {
    assert!(supported_cpu_mhz(268));
    assert!(supported_cpu_mhz(804));
    assert!(!supported_cpu_mhz(0));
    assert!(!supported_cpu_mhz(536));
}

fn sample(dvs: [u8; 2]) -> BattleSample {
    BattleSample {
        battle: 1,
        battle_start: 0,
        battle_ended: 0,
        species: 251,
        level: 30,
        hp: 80,
        max_hp: 80,
        dvs,
    }
}

#[test]
fn arms_after_release_at_exact_prompt_before_final_physical_a() {
    let mut c = ManualController::new();
    assert_eq!(c.observe_prompt(true, true), ManualEvent::None);
    assert_eq!(c.state, ManualState::Ready);
    assert_eq!(c.observe_prompt(true, false), ManualEvent::ArmResolver);
    assert_eq!(c.state, ManualState::Running);
}

#[test]
fn never_arms_outside_exact_prompt() {
    let mut c = ManualController::new();
    c.observe_prompt(false, false);
    assert_eq!(c.observe_prompt(false, true), ManualEvent::None);
    assert_eq!(c.state, ManualState::SeekPrompt);
}

#[test]
fn waits_for_four_identical_fresh_celebi_reads() {
    let mut c = ManualController::new();
    assert_eq!(c.observe_prompt(true, false), ManualEvent::ArmResolver);
    for _ in 0..3 {
        assert_eq!(c.observe_battle(sample([0x2a, 0xaa])), ManualEvent::None);
    }
    assert_eq!(
        c.observe_battle(sample([0x2a, 0xaa])),
        ManualEvent::BattleResult([0x2a, 0xaa])
    );
}

#[test]
fn rejects_wrong_species_and_unstable_dvs() {
    let mut c = ManualController::new();
    c.observe_prompt(true, false);
    let mut wrong = sample([0xaa, 0xaa]);
    wrong.species = 250;
    for _ in 0..8 {
        assert_eq!(c.observe_battle(wrong), ManualEvent::None);
    }
    assert_eq!(c.observe_battle(sample([0xaa, 0xaa])), ManualEvent::None);
    assert_eq!(c.observe_battle(sample([0xfa, 0xaa])), ManualEvent::None);
    assert_eq!(c.state, ManualState::Running);
}

#[test]
fn terminal_state_is_inert() {
    let mut c = ManualController::new();
    c.finish(true);
    assert!(c.terminal());
    assert_eq!(c.observe_prompt(true, true), ManualEvent::None);
    assert_eq!(c.observe_battle(sample([0xaa, 0xaa])), ManualEvent::None);
}
