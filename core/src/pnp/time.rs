use super::bindings;
use chrono::NaiveDateTime;

const THIRTY_YEARS_MS: u64 = 946684800000;

pub fn datetime_from_console_ms(console_ms: u64) -> NaiveDateTime {
    let standard_ms = console_ms.saturating_add(THIRTY_YEARS_MS);
    let ms: i64 = standard_ms.try_into().unwrap_or_default();
    NaiveDateTime::from_timestamp_millis(ms).unwrap_or_default()
}

pub fn game_start_ms() -> u64 {
    unsafe { bindings::host_game_start_ms() }
}

/// Raw ARM11 system tick. This is sampled before the bounded VC context reads
/// so the log retains an independent host-time phase for hardware validation.
pub fn system_tick() -> u64 {
    unsafe { bindings::host_get_system_tick() }
}

pub fn os_time() -> NaiveDateTime {
    let ms = unsafe { bindings::osGetTime() };
    datetime_from_console_ms(ms.saturating_sub(3155673600000))
}
