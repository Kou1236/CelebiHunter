#![no_std]
#![allow(static_mut_refs)]
#![feature(naked_functions)]

extern crate alloc;

#[cfg(target_os = "horizon")]
mod allocator;

mod crystal;
mod pnp;
mod title;
mod utils;

use title::{loaded_title, LoadedTitle, TitleError};

#[cfg(target_os = "horizon")]
#[panic_handler]
fn panic_handler(info: &core::panic::PanicInfo) -> ! {
    if let Some(location) = info.location() {
        let file = location.file().as_bytes();
        let start = file.len().saturating_sub(4);
        let mut marker = [0u8; 4];
        marker[..file.len() - start].copy_from_slice(&file[start..]);
        let partial_file_name = u32::from_ne_bytes(marker);
        unsafe {
            core::arch::asm!("mov r9, {}", in(reg) partial_file_name);
            core::arch::asm!("mov r10, {}", in(reg) location.line());
        }
    }

    unsafe { core::arch::asm!("svc 0x3C", in("r0") 0u32) };
    loop {}
}

fn initialize_loaded_title(title: &LoadedTitle) {
    if matches!(title, LoadedTitle::CrystalEn) {
        crystal::init_crystal();
    }
}

#[cfg(target_os = "horizon")]
#[no_mangle]
pub extern "C" fn initialize() {
    if let Ok(title) = loaded_title() {
        initialize_loaded_title(title);
    }
}

#[no_mangle]
pub extern "C" fn run_frame() {
    match loaded_title() {
        Ok(LoadedTitle::CrystalEn) => crystal::run_frame(),
        Err(TitleError::InvalidUpdate { .. }) => pnp::println!("Unsupported title or update"),
        _ => {}
    }
}
