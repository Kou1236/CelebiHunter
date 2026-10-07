#ifndef CH_MANUAL_SCENE_H
#define CH_MANUAL_SCENE_H
#include "platform.h"
typedef struct {
    uint32_t rom_pointer,wram0_pointer,wram1_pointer,hram_pointer,io_pointer;
    uint16_t guest_pc,guest_sp,script_next;
    uint8_t script_mode,script_running,script_bank,map_group,map_number;
    uint8_t joyp,joy_mirrors[8],match,complete;
    uint32_t rejection;
    uint32_t rejection_detail;
    uint8_t rom_bank,stack_wait_seen,stack_joy_seen;
} ChFinalPrompt;
enum {
    CH_SCENE_OK=0,CH_SCENE_POINTER_READ=1,CH_SCENE_POINTER_INVALID=2,
    CH_SCENE_POINTER_LAYOUT=3,CH_SCENE_ROM_READ=4,CH_SCENE_ROM_SIGNATURE=5,
    CH_SCENE_BANK_READ=6,CH_SCENE_BANK_UNSUPPORTED=7,CH_SCENE_STATE_READ=8,
    CH_SCENE_SCRIPT_STATE=9,CH_SCENE_STACK_READ=10,CH_SCENE_STACK_PATH=11,
    CH_SCENE_INPUT_HELD=12
};
/* Conservative actual memory predicate, adapted from final_prompt.py. This
   is not a physical-input timing, global-pause, or complete-model certificate. */
int ch_scene_final_prompt(const ChReadOps *,ChFinalPrompt *);
#endif
