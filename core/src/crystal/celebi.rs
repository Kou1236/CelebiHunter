//! Small, read-only observation layer shared by Reset and RNG.

use super::{
    celebi_session::Observation,
    game_lib::gb_mem,
    reader::Gen2Reader,
};

pub(super) fn observe(cpu_mhz: u32) -> Observation {
    let reader = Gen2Reader::crystal();
    let wild = reader.wild();
    Observation {
        // Public Reset and RNG builds do not use the retired advance-search UI.
        rng: None,
        cpu_mhz,
        tid: reader.trainer_id(),
        // Verified against pret/pokecrystal symbols/pokecrystal11_vc.sym.
        map: (gb_mem::read_u8(0xdcb5), gb_mem::read_u8(0xdcb6)),
        battle: gb_mem::read_u8(0xd22d),
        species: wild.spec_index,
        battle_start: gb_mem::read_u8(0xd264),
        battle_ended: gb_mem::read_u8(0xc734),
        level: gb_mem::read_u8(0xd213),
        hp: gb_mem::read_u16(0xd216),
        max_hp: gb_mem::read_u16(0xd218),
        dvs: [wild.atk << 4 | wild.def, wild.spe << 4 | wild.spc],
    }
}

// The cutscene path needs only the fields used to prove a fresh, stable Celebi
// and read its DVs. Avoid unrelated reads while timing-sensitive delivery is
// in flight.
pub(super) fn observe_battle(cpu_mhz: u32) -> Observation {
    Observation {
        rng: None,
        cpu_mhz,
        tid: 0,
        map: (0, 0),
        battle: gb_mem::read_u8(0xd22d),
        species: gb_mem::read_u8(0xd206),
        battle_start: gb_mem::read_u8(0xd264),
        battle_ended: gb_mem::read_u8(0xc734),
        level: gb_mem::read_u8(0xd213),
        hp: gb_mem::read_u16(0xd216),
        max_hp: gb_mem::read_u16(0xd218),
        dvs: [gb_mem::read_u8(0xd20c), gb_mem::read_u8(0xd20d)],
    }
}
