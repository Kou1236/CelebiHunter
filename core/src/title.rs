use crate::pnp;

const ENGLISH_CRYSTAL_TITLE_ID: u64 = 0x0004_0000_0017_2800;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadedTitle {
    CrystalEn,
}

#[derive(Debug, Clone)]
pub enum TitleError {
    InvalidTitle,
    InvalidUpdate { remaster_version: u16 },
}

static mut LOADED: bool = false;
static mut LOAD_RESULT: Result<LoadedTitle, TitleError> = Err(TitleError::InvalidTitle);

pub fn loaded_title() -> &'static Result<LoadedTitle, TitleError> {
    unsafe {
        if LOADED {
            return &LOAD_RESULT;
        }
        LOADED = true;
        LOAD_RESULT = if pnp::title_id() != ENGLISH_CRYSTAL_TITLE_ID {
            Err(TitleError::InvalidTitle)
        } else {
            let remaster_version = pnp::update_version();
            if remaster_version == 0 {
                Ok(LoadedTitle::CrystalEn)
            } else {
                Err(TitleError::InvalidUpdate { remaster_version })
            }
        };
        &LOAD_RESULT
    }
}
