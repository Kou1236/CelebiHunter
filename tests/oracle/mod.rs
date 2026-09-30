// Upstream GPL-3.0 reference algorithm at b6d7a246; see docs/SOURCES.md.
pub mod div;
pub mod rng;
use rng::{GameboyRng, Offset};
fn generate_celebi_rands(rng: &GameboyRng, extra_consumed_rands: u8, div_off: u8) -> [[u8; 2]; 2] {
    let mut rng = rng.clone();

    for _ in 0..595 {
        rng.next();
    }

    let adiv_index = rng.add_div.index();
    let sdiv_index = rng.sub_div.index();

    rng.next_with_div_offset(Offset::Minus(0xc));

    for _ in 0..extra_consumed_rands {
        rng.next();
    }

    rng.next_with_div_inc(Offset::Plus(div_off));
    rng.add_div.set_index(adiv_index);
    rng.add_div.decrement_index(2);
    rng.sub_div.set_index(sdiv_index);
    rng.sub_div.decrement_index(2);

    for _ in 0..12 {
        rng.next();
    }

    rng.next_with_div_inc(Offset::Plus(0x6f));

    rng.add_div.set_index(0);
    rng.sub_div.set_index(3);

    let poke_rand_1 = rng.next_with_div_inc(Offset::Plus(0xc));
    let poke_rand_2 = rng.next_with_div_inc(Offset::Plus(0xe4));

    [poke_rand_1, poke_rand_2]
}

fn generate_celebi_rands_from_prefix(
    rng: &GameboyRng,
    extra_consumed_rands: u8,
    div_off: u8,
) -> [[u8; 2]; 2] {
    let mut rng = rng.clone();
    let adiv_index = rng.add_div.index();
    let sdiv_index = rng.sub_div.index();
    rng.next_with_div_offset(Offset::Minus(0xc));
    for _ in 0..extra_consumed_rands {
        rng.next();
    }
    rng.next_with_div_inc(Offset::Plus(div_off));
    rng.add_div.set_index(adiv_index);
    rng.add_div.decrement_index(2);
    rng.sub_div.set_index(sdiv_index);
    rng.sub_div.decrement_index(2);
    for _ in 0..12 {
        rng.next();
    }
    rng.next_with_div_inc(Offset::Plus(0x6f));
    rng.add_div.set_index(0);
    rng.sub_div.set_index(3);
    let poke_rand_1 = rng.next_with_div_inc(Offset::Plus(0xc));
    let poke_rand_2 = rng.next_with_div_inc(Offset::Plus(0xe4));
    [poke_rand_1, poke_rand_2]
}

pub fn outcomes(rng: &GameboyRng) -> [[u8; 2]; 4] {
    let mut out = [[0; 2]; 4];
    let mut i = 0;
    for extra in [2, 3] {
        for off in [0xba, 0xbb] {
            let [[_, a], [_, b]] = generate_celebi_rands(rng, extra, off);
            out[i] = [a, b];
            i += 1;
        }
    }
    out
}

pub fn outcomes_from_prefix(rng: &GameboyRng) -> [[u8; 2]; 4] {
    let mut out = [[0; 2]; 4];
    let mut i = 0;
    for extra in [2, 3] {
        for off in [0xba, 0xbb] {
            let [[_, a], [_, b]] = generate_celebi_rands_from_prefix(rng, extra, off);
            out[i] = [a, b];
            i += 1;
        }
    }
    out
}
