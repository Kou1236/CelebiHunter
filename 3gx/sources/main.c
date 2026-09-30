#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include "plgldr.h"
#include "csvc.h"
#include "common.h"
#include "ov.h"
#include "pnp.h"
#include "celebi_hunter.h"
#include "title_info.h"
#include "hid.h"
#include "memmem.h"

static Handle thread;
static Handle memLayoutChanged;
static u8 stack[0x1000] __attribute__((aligned(8)));
static bool is_paused = false;

extern bool celebi_auto_enabled(void);
extern u32 celebi_auto_mode(void);
extern bool celebi_auto_busy(void);
extern u32 celebi_auto_service(bool paused, u32 physical_keys, u32 cpu_mhz);
extern u32 celebi_auto_host_buttons(void);
extern void celebi_auto_draw(void);
extern bool celebi_auto_stopped(void);
static bool auto_manual_resume = false;

// Top is submitted before the matching bottom callback. Retain it only across
// that pair, so the pause HUD updates the displayed top image, not an unsubmitted
// bottom backbuffer. Never carry these pointers into the next pair.
static u8 *pause_top_a = NULL;
static u8 *pause_top_b = NULL;
static u32 pause_stride = 0;
static u32 pause_format = 0;
static bool pending_cancel = false;
static u8 auto_hud_tick = 0;

static u32 celebi_cpu_mhz(void)
{
    s64 clock = 0;
    if (R_SUCCEEDED(svcGetSystemInfo(&clock, 0x10001, 0)))
        return (u32)clock;

    // Luma uses this query's success as its New 3DS test.  A failure therefore
    // identifies the Old 3DS family, whose only CPU mode is 268 MHz.  Apply the
    // fallback only to the two current public releases; legacy mode 2 keeps its
    // original fail-closed behavior.
    u32 mode = celebi_auto_mode();
    return celebi_auto_enabled() && (mode == 1 || mode == 3) ? 268 : 0;
}

static bool should_draw_plugin_overlay(void)
{
    // Keep the release-mode marker linked so packaging can prove that a file
    // labelled Reset or RNG contains the matching Rust feature.
    if (celebi_auto_enabled() && celebi_auto_mode() > 2)
        return false;
    // Once the user resumes a preserved shiny/stopped encounter with R, leave
    // the game framebuffer untouched for the rest of this process.  The game
    // overwrites the previously composited pause HUD on its next rendered
    // frame, so the battle remains visible without another plugin overlay.
    if (auto_manual_resume)
        return false;
    return !celebi_auto_enabled() || !celebi_auto_busy();
}

static void draw_pause_hud(void)
{
    u8 *buffers[2] = {pause_top_a, pause_top_b};
    if (!pause_top_a || (pause_format & 0xf) < 1 || (pause_format & 0xf) > 3)
        return;
    for (unsigned i = 0; i < 2; ++i)
    {
        u8 *fb = buffers[i];
        if (!fb || (i == 1 && fb == buffers[0]))
            continue;
        reset_print();
        // Reset prints only a compact terminal result/control card. RNG never
        // draws an overlay.
        celebi_auto_draw();
        draw_to_screen(0, fb, pause_stride, pause_format);
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb, pause_stride * 400);
    }
}

void handle_freeze(bool isTopScreen)
{
    if (auto_manual_resume || isTopScreen)
        return;
    // RNG becomes permanently inert after the one encounter has either been
    // verified or failed closed. Keep its overlay hidden and stop polling.
    if (celebi_auto_mode() > 2 && celebi_auto_stopped())
        return;
    for (;;)
    {
        scan_input();
        // Do not mistake Reset's previous HID injection for a physical B
        // press. The user can still cancel whenever no reset chord is active.
        u32 physical = (get_current_keys() & ~celebi_virtual_keys()) |
                       (pending_cancel ? KEY_B : 0);
        pending_cancel = false;
        if (celebi_auto_stopped() && host_is_just_pressed(KEY_R))
        {
            auto_manual_resume = true;
            is_paused = false;
            return;
        }
        u32 action = celebi_auto_service(is_paused, physical, celebi_cpu_mhz());
        u32 host_buttons = celebi_auto_host_buttons();
        celebi_inject_keys(host_buttons);
        is_paused = action != 1;
        bool busy = celebi_auto_busy();
        if (is_paused && !busy && ++auto_hud_tick >= 10)
        {
            auto_hud_tick = 0;
            draw_pause_hud();
        }
        else if (!is_paused)
            auto_hud_tick = 0;
        if (action == 1 || action == 3)
            return;
        svcSleepThread(busy ? 1000000 : 10000000);
    }
}

void run_hook(u32 _1, u32 _2, u32 _3, u32 _4, u32 screenId, u32 swap, u8 *fb_a, u8 *fb_b, u32 stride, u32 format)
{
    bool isTopScreen = screenId == 0;
    if (isTopScreen)
    {
        scan_input();
        pending_cancel |= (host_just_pressed() & ~celebi_virtual_keys() & KEY_B) != 0;
        // Active automation stages do not need a live HUD.
        // Skipping both formatting and compositing here leaves the full 268 MHz
        // frame budget to Crystal and prevents alternating game/overlay frames.
        if (should_draw_plugin_overlay())
        {
            run_frame();
            draw_to_screen(screenId, fb_a, stride, format);
        }
        pause_top_a = fb_a;
        pause_top_b = fb_b;
        pause_stride = stride;
        pause_format = format;
    }

    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb_a, SCREEN_WIDTH * SCREEN_HEIGHT);
    // Thanks to https://github.com/44670/NTR/blob/c764c0f68c08f3518a9f284f5fda1bf3b2636123/source/plg.c#L868-L870
    if (isTopScreen && fb_a != fb_b && fb_b != 0)
    {
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)fb_b, SCREEN_WIDTH * SCREEN_HEIGHT);
    }

    handle_freeze(isTopScreen);
    if (!isTopScreen)
        pause_top_a = pause_top_b = NULL;
}

Result map_input_hook(u32 memblock_handle, u32 addr, u32 _r2, u32 _r3, u32 _r4, u32 _r5)
{
    bool has_write_perm = _r5 == 0;
    if (!has_write_perm)
    {
        set_key_addr((vu32 *)(addr + 0x28));
    }
    u32 my_perm = has_write_perm ? MEMPERM_READ | MEMPERM_WRITE : MEMPERM_READ;
    // Mimic the game's permissions
    return svcMapMemoryBlock(memblock_handle, addr, my_perm, MEMPERM_DONTCARE);
}

u8 DRAW_PATCH[0x94] = {
    0xf0, 0x5f, 0x2d, 0xe9, // stmdb      sp!,{r4 r5 r6 r7 r8 r9 r10 r11 r12 lr}
    0x0f, 0x00, 0x2d, 0xe9, // stmdb      sp!,{r0 r1 r2 r3}
    // Injected reader call
    0x64, 0xc0, 0x9f, 0xe5, // ldr        r12,[run_hook_addr]
    0x3c, 0xff, 0x2f, 0xe1, // blx        r12
    // End reader call
    0xf0, 0x00, 0xbd, 0xe8, // ldmia      sp!,{r4 r5 r6 r7}
    0x28, 0x00, 0x8d, 0xe2, // add        r0,sp,#0x28
    0x00, 0x0e, 0x90, 0xe8, // ldmia      r0,{r9 r10 r11}
    0xd3, 0x03, 0x00, 0xeb, // bl         get_screen
    0x5c, 0x10, 0x80, 0xe2, // add        r1,r0,#0x5c
    0x04, 0x21, 0x91, 0xe7, // ldr        r2,[r1,r4,lsl #0x2]
    0x04, 0x30, 0xa0, 0xe3, // mov        r3,#0x4
    0x00, 0x00, 0xd2, 0xe5, // ldrb       r0,[r2,#0x0]
    0x01, 0x00, 0x60, 0xe2, // rsb        r0,r0,#0x1
    0xff, 0x00, 0x00, 0xe2, // and        r0,r0,#0xff
    0x80, 0xe1, 0x60, 0xe0, // rsb        lr,r0,r0, lsl #0x3
    0x0e, 0x31, 0x83, 0xe0, // add        r3,r3,lr, lsl #0x2
    0x03, 0x30, 0x82, 0xe0, // add        r3,r2,r3
    0xe0, 0x0e, 0x83, 0xe8, // stmia      r3,{ r5 r6 r7 r9 r10 r11 }
    0x9a, 0x8f, 0x07, 0xee, // mcr        p15,0x0,r8,cr7,cr10,0x4
    0x04, 0x21, 0x91, 0xe7, // ldr        r2,[r1,r4,lsl #0x2]
    0x9f, 0x3f, 0x92, 0xe1, // ldrex      r3,[r2]
    0xff, 0x30, 0xc3, 0xe3, // bic        r3,r3,#0xff
    0x00, 0x30, 0x83, 0xe1, // orr        r3,r3,r0
    0xff, 0x3c, 0xc3, 0xe3, // bic        r3,r3,#0xff00
    0x01, 0x3c, 0x83, 0xe3, // orr        r3,r3,#0x100
    0x93, 0x6f, 0x82, 0xe1, // strex      r6,r3,[r2]
    0x00, 0x00, 0x56, 0xe3, // cmp        r6,#0x0
    0xf6, 0xff, 0xff, 0x1a, // bne        loop
    0xf0, 0x9f, 0xbd, 0xe8, // ldmia      sp!,{r4 r5 r6 r7 r8 r9 r10 r11 r12 pc}
    0x00, 0x00, 0x00, 0x00, // run_hook_addr
    // Hook trampoline
    0xff, 0xdf, 0x2d, 0xe9, // stmdb      sp!,{r0-r12, lr, pc}
    0x0d, 0x00, 0xa0, 0xe1, // cpy        r0,sp
    0x08, 0xc0, 0x9f, 0xe5, // ldr        r12,[trampoline_addr]
    0x3c, 0xff, 0x2f, 0xe1, // blx        r12
    0x00, 0x40, 0xbd, 0xe8, // ldmia      sp!,{lr}
    0xff, 0x9f, 0xbd, 0xe8, // ldmia      sp!,{r0-r12, pc}    
    0x00, 0x00, 0x00, 0x00, // trampoline_addr
};

u8 HID_INPUT_MAP_PATCH[0x8] = {
    0x00, 0xe0, 0x1f, 0xe5, //     ldr        lr,[pc + 0x8]
    0x00, 0xf0, 0x1f, 0xe5, //     ldr        pc,[pc + 0x8]
};

u8 PRESENT_FRAMEBUFFER_BYTES[0X10] = {
    0x28, 0x00, 0x8d, 0xe2, 0x00, 0x80, 0xa0, 0xe3, 0x01, 0x70, 0xa0, 0xe1, 0x00, 0x0e, 0x90, 0xe8,
};

u8 MAP_INPUT_BLOCK[] = {
    0x01, 0x20, 0xa0, 0x13, 0x03, 0x20, 0xa0, 0x03, 0x01, 0x32, 0xa0, 0xe3, 0x1f, 0x00,
    0x00, 0xef, 0xa0, 0x1f, 0xb0, 0xe1, 0x01, 0x10, 0xa0, 0x03, 0x18, 0x10, 0xc4, 0x05
};

extern char *fake_heap_start;
extern char *fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;

u32 __ctru_heap_size = 0;
u32 __ctru_linear_heap_size = 0;

void __system_allocateHeaps(PluginHeader *header)
{
    __ctru_heap_size = header->heapSize;
    __ctru_heap = header->heapVA;

    // Set up newlib heap
    fake_heap_start = (char *)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

// Entrypoint, game will starts when you exit this function
void main(void)
{
    PluginHeader *header = (PluginHeader *)0x07000000;

    // Init heap
    __system_allocateHeaps(header);

    // Init services
    if (R_FAILED(srvInit()))
        return;

    // This experimental build is intentionally limited to English Crystal VC.
    if (get_title_id() != 0x0004000000172800ULL || get_remaster_version() != 0)
        return;

    // NTP epoch (milliseconds since 1st Jan 1900 00:00)
    u64 ms = osGetTime();
    // Adjust to Jan 2000, which is what the games use.
    u64 game_ms = ms - 3155673600000;
    set_game_start_ms(game_ms);

    // Get memory layout changed event
    svcControlProcess(CUR_PROCESS_HANDLE, PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT, (u32)&memLayoutChanged, 0);

    MemInfo info;
    PageInfo out;
    if (R_FAILED(svcQueryMemory(&info, &out, 0x100000)))
        return;

    // Fail before patching on an unexpected VC executable layout. These are
    // known English Crystal VC hook sites; both must still be ARM BL instructions.
    if (info.base_addr != 0x100000 || info.size < 0xaf180 ||
        (*(const u32 *)0x1a8360 >> 24) != 0xeb ||
        (*(const u32 *)0x1af17c >> 24) != 0xeb)
        return;

    // Capture the pristine executable before installing any CelebiHunter hooks.
    // Failure is reported in the terminal JSON; it never blocks game startup.
 
    void *present_match = memmem((u8*)info.base_addr, info.size, PRESENT_FRAMEBUFFER_BYTES, sizeof(PRESENT_FRAMEBUFFER_BYTES));
    u32 map_input_memory_block = (u32)memmem((u8*)info.base_addr, info.size, MAP_INPUT_BLOCK, sizeof(MAP_INPUT_BLOCK));
    if (!present_match || !map_input_memory_block || (u32)present_match < info.base_addr + 8)
        return;
    u32 present_buffer_ptr = (u32)present_match - 8;
    if (present_buffer_ptr > info.base_addr + info.size - sizeof(DRAW_PATCH) ||
        map_input_memory_block > info.base_addr + info.size - 16)
        return;

    u32 get_screen_branch = *(u32 *)(present_buffer_ptr + 0x20) + 1;
    u32 *present_buffer_pa = (u32 *)PA_FROM_VA_PTR(present_buffer_ptr);
    memcpy(present_buffer_pa, DRAW_PATCH, 0x94);
    present_buffer_pa[7] = get_screen_branch; // fix get_screen branch instruction
    present_buffer_pa[29] = (u32)run_hook;
    u32 trampoline_addr = (u32)present_buffer_ptr + (30 * 4);
    set_trampoline_addr(trampoline_addr);
    set_route_hook_addr(trampoline_addr + (6 * 4));

    u32 *map_input_memory_block_pa = (u32 *)PA_FROM_VA_PTR(map_input_memory_block);
    memcpy(map_input_memory_block_pa, HID_INPUT_MAP_PATCH, 0x8);
    // 4 instructions * 4 bytes per instruction
    map_input_memory_block_pa[0x2] = (u32)map_input_memory_block + (0x4 * 0x4); // set return address
    map_input_memory_block_pa[0x3] = (u32)map_input_hook;                       // set jump address

    initialize();
    svcInvalidateEntireInstructionCache();
}
