#include "platform.h"
#include <string.h>

static int word(const ChReadOps *o, uint32_t address, uint32_t *value) {
    uint8_t b[4];
    if ((address & 3u) || !o->read_bytes(o->user,address,b,4u)) return 0;
    *value=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
    return 1;
}
static int once(const ChReadOps *o, ChBoundarySample *s) {
    uint32_t i,p=0,ok=1u; uint8_t b[16];
    memset(s,0,sizeof(*s));
    if (!word(o,CH_ENGINE_POINTER_ADDRESS,&s->engine_pointer) ||
        !s->engine_pointer || (s->engine_pointer&3u) ||
        s->engine_pointer>UINT32_MAX-0x154u) return 0;
    p=s->engine_pointer;
    s->counter_valid=(uint8_t)word(o,p+0x140u,&s->counter);
    ok&=s->counter_valid;
    ok&=(uint32_t)word(o,p,&s->engine_type);
    ok&=(uint32_t)word(o,p+0x80u,&s->recording_gate);
    ok&=(uint32_t)word(o,p+0xdcu,&s->queued_tasks);
    ok&=(uint32_t)word(o,p+0x144u,&s->engine_mode);
    ok&=(uint32_t)word(o,p+0x150u,&s->engine_flags);
    if(o->read_bytes(o->user,CH_HOST_CACHE_ADDRESS,b,16u)) {
        for(i=0;i<4u;i++)s->host_cache[i]=(uint32_t)b[i*4u]|(uint32_t)b[i*4u+1u]<<8|
            (uint32_t)b[i*4u+2u]<<16|(uint32_t)b[i*4u+3u]<<24;
    } else ok=0u;
    ok&=(uint32_t)word(o,CH_HOST_BATCH_ADDRESS,&s->batch_count);
    ok&=(uint32_t)word(o,CH_HOST_PHASE_ADDRESS,&s->host_phase);
    ok&=(uint32_t)word(o,CH_CONFIG_POINTER_ADDRESS,&s->selected_config_pointer);
    if(s->selected_config_pointer && !(s->selected_config_pointer&3u))
        ok&=(uint32_t)word(o,s->selected_config_pointer,&s->selected_config_mode);
    else ok=0u;
    ok&=(uint32_t)word(o,0x0023278cu,&s->previous_provider_keys);
    if(o->read_bytes(o->user,0x0022f764u,b,4u)) {
        s->input_enable=b[0];
        s->guest_active_low_mask=(uint16_t)((uint16_t)b[2]|(uint16_t)b[3]<<8);
    } else ok=0u;
    s->complete=(uint8_t)ok;
    return (int)ok;
}
int ch_sample_boundary(const ChReadOps *o, ChBoundarySample *out) {
    ChBoundarySample a,b;int first,second;
    if(!o||!out||!o->read_bytes)return CH_SAMPLE_READ_FAILED;
    first=once(o,&a);second=once(o,&b);*out=a;
    out->counter_valid=(uint8_t)(a.counter_valid&&b.counter_valid&&
        a.engine_pointer==b.engine_pointer&&a.counter==b.counter);
    out->complete=(uint8_t)(first&&second);
    if(!out->complete)return CH_SAMPLE_READ_FAILED;
    /* Compare only initialized, complete snapshots; padding is zero-filled. */
    out->coherent=(uint8_t)(memcmp(&a,&b,sizeof(a))==0);
    if(!out->coherent)return CH_SAMPLE_INCOHERENT;
    /* The engine object can move with host allocation. once() has already
       checked alignment, readability and stability across both snapshots. */
    out->ordinary_supported=(uint8_t)(a.engine_type==1u&&a.recording_gate==0u&&a.queued_tasks==0u&&
        a.engine_mode==0u&&a.engine_flags==0u&&a.batch_count==1u&&
        a.host_phase==0u&&a.selected_config_mode==0u&&a.input_enable==0u);
    return out->ordinary_supported?CH_SAMPLE_OK:CH_SAMPLE_UNSUPPORTED;
}
int ch_read_physical_keys(const ChReadOps *o,uint32_t *keys) {
    if(!o||!keys||!o->read_physical_keys)return 0;
    return o->read_physical_keys(o->user,keys)==1;
}
