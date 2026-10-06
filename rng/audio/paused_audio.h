#ifndef CH_RAW_PAUSED_AUDIO_H
#define CH_RAW_PAUSED_AUDIO_H
#include <stdint.h>
typedef struct {
    void *user;
    /* 1: snapshot complete; 0: temporarily unavailable; -1: rejected. */
    int (*gain_bits)(void *,uint32_t *);
    /* 1: applied and verified; 0: definitely no mutation; -1: ambiguous. */
    int (*set_gain_bits)(void *,uint32_t);
} RawPauseAudioOps;
typedef struct {
    RawPauseAudioOps ops;
    uint32_t initialized,saved_valid,owned,saved_gain,failed;
    uint32_t enter_attempts,leave_attempts;
} RawPausedAudio;
int raw_paused_audio_init(RawPausedAudio *,const RawPauseAudioOps *);
/* Original pre-HID caller only, while the player has paused. Repeated polls
   are idempotent after a successful mute. No game/audio-emulator state. */
int raw_paused_audio_enter(RawPausedAudio *,uint32_t caller_owned);
/* Call before allowing the original pre-HID call to continue, including
   step, run and physical-A continuation. Repeated successful leave is inert. */
int raw_paused_audio_leave(RawPausedAudio *,uint32_t caller_owned);
#endif
