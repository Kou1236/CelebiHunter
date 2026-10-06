#ifndef CH_MANUAL_SCENE_H
#define CH_MANUAL_SCENE_H
#include "platform.h"
typedef struct {
    uint32_t rom_pointer,wram0_pointer,wram1_pointer,hram_pointer,io_pointer;
    uint16_t guest_pc,guest_sp,script_next;
    uint8_t script_mode,script_running,script_bank,map_group,map_number;
    uint8_t joyp,joy_mirrors[8],match,complete;
} ChFinalPrompt;
/* Conservative actual memory predicate, adapted from final_prompt.py. This
   is not a physical-input timing, global-pause, or complete-model certificate. */
int ch_scene_final_prompt(const ChReadOps *,ChFinalPrompt *);
#endif
