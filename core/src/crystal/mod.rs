mod celebi;
mod celebi_auto;
mod celebi_manual;
#[cfg(feature = "celebi_natural")]
mod celebi_auto_runtime;
#[cfg(feature = "celebi_resolver_manual")]
use celebi_manual_runtime as celebi_auto_runtime;
#[cfg(feature = "celebi_resolver_manual")]
mod celebi_manual_runtime;
mod celebi_rng;
mod celebi_session;
mod celebi_trace;
mod game_lib;
mod hook;
mod pk2;
mod reader;

pub use hook::init_crystal;

pub fn run_frame() {
    celebi_auto_runtime::celebi_auto_draw();
}
