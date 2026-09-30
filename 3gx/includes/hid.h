#include <3ds.h>

void set_key_addr(vu32 *key_addr);
void scan_input();
u32 get_current_keys();
u32 get_previous_keys();
void celebi_inject_keys(u32 keys);
u32 celebi_virtual_keys();
u32 celebi_hid_ready();
u32 celebi_hid_injection_frames();
