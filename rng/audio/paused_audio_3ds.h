#ifndef CH_RAW_PAUSED_AUDIO_3DS_H
#define CH_RAW_PAUSED_AUDIO_3DS_H
#include "paused_audio.h"
#include "../platform/platform.h"
typedef struct {
    ChReadOps read;
    uint32_t initialized,contract_checked,stage,error,sets,busy;
} RawAudio3ds;
/* The pinned English Crystal pristine-prefix identity must have succeeded
   at startup. This module additionally pins the complete gain setter path. */
int raw_paused_audio_3ds_init(RawAudio3ds *,const ChReadOps *,RawPauseAudioOps *);
#endif
