#include "platform_binding.h"
#include "restart_lifecycle.h"
#include <string.h>
int raw_platform_sample(void *user,RawSample *out) {
    RawPlatformBinding *b=user;ChBoundarySample *a;RawFrontendState before,after;
    if(!b||!out)return 0;
    memset(out,0,sizeof(*out));
    b->last_sample_status=CH_SAMPLE_READ_FAILED;b->last_scene_status=0;
    memset(&b->last_boundary,0,sizeof(b->last_boundary));memset(&b->last_prompt,0,sizeof(b->last_prompt));
    if(!raw_frontend_read(&b->read,&before))return 0;
    b->last_sample_status=ch_sample_boundary(&b->read,&b->last_boundary);a=&b->last_boundary;
    out->counter=a->counter;out->counter_read=a->counter_valid;
    out->sample_complete=(uint32_t)(a->complete&&a->coherent);
    out->ordinary_supported=a->ordinary_supported;out->batch=a->batch_count;
    out->host_phase=a->host_phase;out->input_type=a->engine_type;
    out->host_held=a->host_cache[0];out->host_down=a->host_cache[1];out->host_up=a->host_cache[2];
    out->host_intersection=a->host_cache[3];out->input_enable=a->input_enable;
    out->guest_mask=a->guest_active_low_mask;out->scene_epoch=b->source_epoch;
    if(!raw_frontend_read(&b->read,&after)||memcmp(&before,&after,sizeof(before))){
        out->sample_complete=0u;b->last_sample_status=CH_SAMPLE_INCOHERENT;return 0;
    }
    out->frontend_valid=1u;out->frontend_mode=before.mode;out->frontend_scan_gate=before.scan_gate;
    if(b->omit_arrival_check||before.mode!=1u||before.scan_gate==2u){
        /* Once original A is observed, the independent result gate follows
           the actual encounter. Rechecking the old waiting stack cannot
           add a source admission and must not cost every animation unit. */
        memset(&b->last_prompt,0,sizeof(b->last_prompt));b->last_scene_status=0;
    }else b->last_scene_status=ch_scene_final_prompt(&b->read,&b->last_prompt);
    out->script_final_prompt=(uint32_t)(b->last_scene_status==1&&b->last_prompt.match);
    return out->sample_complete?1:0;
}
int raw_platform_keys(void *user,uint32_t *keys) {
    RawPlatformBinding *b=user;return b&&ch_read_physical_keys(&b->read,keys);
}
