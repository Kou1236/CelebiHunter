#include "paused_audio.h"
#include <string.h>
int raw_paused_audio_init(RawPausedAudio *a,const RawPauseAudioOps *o){
    if(!a||!o||!o->gain_bits||!o->set_gain_bits)return 0;
    memset(a,0,sizeof(*a));a->ops=*o;a->initialized=1;return 1;
}
int raw_paused_audio_enter(RawPausedAudio *a,uint32_t owner){int status;
    if(!a||!a->initialized||owner!=1u)return -1;
    if(a->owned)return a->failed?-1:1;
    if(a->failed)return -1;
    a->enter_attempts++;
    if(!a->saved_valid){
        status=a->ops.gain_bits(a->ops.user,&a->saved_gain);
        if(status!=1){if(status<0)a->failed=1;return status;}
        a->saved_valid=1;
    }
    /* An already silent output requires no setter and still owns an exact
       prior snapshot for this pause, including the negative-zero bit pattern. */
    if(!(a->saved_gain&UINT32_C(0x7fffffff))){a->owned=1;return 1;}
    a->owned=1;status=a->ops.set_gain_bits(a->ops.user,0u);
    if(!status)a->owned=0;
    if(status<0)a->failed=1;
    return status;
}
int raw_paused_audio_leave(RawPausedAudio *a,uint32_t owner){int status;
    if(!a||!a->initialized||owner!=1u)return -1;
    if(!a->owned){a->saved_valid=0;return 1;}
    a->leave_attempts++;
    if(!(a->saved_gain&UINT32_C(0x7fffffff)))status=1;
    else status=a->ops.set_gain_bits(a->ops.user,a->saved_gain);
    if(status==1){a->owned=0;a->saved_valid=0;}
    else if(status<0)a->failed=1;
    return status;
}
