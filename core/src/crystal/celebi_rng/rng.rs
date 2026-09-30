// Adapted from zaksabeast/PokemonRNGGuides b6d7a2467093d1a8349bbefa233a32dc9618829e.
// GPL-3.0; see LICENSE and docs/SOURCES.md. Original algorithm retained.
use super::div::Div;

#[derive(Debug, Clone, Copy)]
pub enum Offset {
    Plus(u8),
    Minus(u8),
}

impl Offset {
    pub fn from_i8(value: i8) -> Self {
        if value < 0 {
            Offset::Minus(value.unsigned_abs())
        } else {
            Offset::Plus(value as u8)
        }
    }

    pub fn apply(&self, value: u8) -> u8 {
        match self {
            Offset::Plus(offset) => value.wrapping_add(*offset),
            Offset::Minus(offset) => value.wrapping_sub(*offset),
        }
    }
}

impl Default for Offset {
    fn default() -> Self {
        Offset::Plus(0)
    }
}

#[derive(Debug, Clone)]
pub struct GameboyRng {
    pub r_add: u8,
    pub r_sub: u8,
    pub add_div: Div,
    pub sub_div: Div,
}

impl GameboyRng {
    pub fn new(state: u16, add_div: Div, sub_div: Div) -> Self {
        let r_add = (state >> 8) as u8;
        let r_sub = state as u8;
        Self {
            r_add,
            r_sub,
            add_div,
            sub_div,
        }
    }

    pub fn state(&self) -> u16 {
        ((self.r_add as u16) << 8) | self.r_sub as u16
    }

    pub fn advance_state(r_add: u8, r_sub: u8, a_div: u8, s_div: u8) -> [u8; 2] {
        let (r_add, add_overload) = r_add.overflowing_add(a_div);
        let r_sub = r_sub.wrapping_sub(s_div.wrapping_add(add_overload as u8));
        [r_add, r_sub]
    }

    pub fn next_with_div_offset(&mut self, div_offset: Offset) -> [u8; 2] {
        self.add_div.next();
        self.sub_div.next();

        [self.r_add, self.r_sub] = Self::advance_state(
            self.r_add,
            self.r_sub,
            div_offset.apply(self.add_div.value()),
            div_offset.apply(self.sub_div.value()),
        );

        [self.r_add, self.r_sub]
    }

    pub fn next_with_div_inc(&mut self, div_offset: Offset) -> [u8; 2] {
        self.add_div.next();
        self.sub_div.next();

        self.add_div.set_value(div_offset.apply(self.add_div.value()));
        self.sub_div.set_value(div_offset.apply(self.sub_div.value()));

        [self.r_add, self.r_sub] =
            Self::advance_state(self.r_add, self.r_sub, self.add_div.value(), self.sub_div.value());

        [self.r_add, self.r_sub]
    }

    pub fn next(&mut self) -> [u8; 2] {
        self.next_with_div_offset(Offset::default())
    }

    pub fn next_u16(&mut self) -> u16 {
        let [r_add, r_sub] = self.next();
        ((r_add as u16) << 8) | r_sub as u16
    }

    pub fn adiv(&self) -> u8 {
        self.add_div.value()
    }

    pub fn sdiv(&self) -> u8 {
        self.sub_div.value()
    }
}
