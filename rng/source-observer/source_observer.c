#include "source_observer.h"
#include <string.h>
static int read_at(const ChReadOps *o,uint32_t a,void *v,uint32_t n) {
    return a&&n&&(uint64_t)a+n<=UINT64_C(0x100000000)&&o->read_bytes(o->user,a,v,n);
}
static uint32_t word(const uint8_t *b) {
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static int fields(const ChReadOps *o,SourceObservation *s) {
    uint8_t rng[2],v,raw[12];uint32_t i;
    if(!read_at(o,s->scene.hram_pointer+0x61,rng,2))return 0;s->add=rng[0];s->sub=rng[1];
    if(!read_at(o,s->scene.hram_pointer+0x1b,&v,1))return 0;s->vblank=v;
    if(!read_at(o,s->scene.hram_pointer+0x55,&v,1))return 0;s->bg=v;
    if(!read_at(o,s->scene.io_pointer+4,&v,1))return 0;s->io_div=v;
    if(!read_at(o,0x22f600,raw,4))return 0;s->budget=word(raw);
    if(!read_at(o,0x22fa48,raw,12))return 0;
    for(i=0;i<3;i++)s->clock_phase[i]=word(raw+4*i);return 1;
}
int source_observer_capture(const ChReadOps *o,uint64_t epoch,SourceObservation *out) {
    SourceObservation before,after;
    if(!out)return SOURCE_OBSERVER_READ_FAILED;
    memset(out,0,sizeof(*out));memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    if(!o||!o->read_bytes||!epoch)return SOURCE_OBSERVER_READ_FAILED;
    before.abi=after.abi=SOURCE_OBSERVER_ABI;before.epoch=after.epoch=epoch;
    if(ch_sample_boundary(o,&before.boundary)!=CH_SAMPLE_OK||!ch_scene_final_prompt(o,&before.scene)||
       !before.scene.complete)return SOURCE_OBSERVER_READ_FAILED;
    before.counter=before.boundary.counter;before.applied_mask=before.boundary.guest_active_low_mask;
    if(!fields(o,&before))return SOURCE_OBSERVER_READ_FAILED;
    if(ch_sample_boundary(o,&after.boundary)!=CH_SAMPLE_OK||!ch_scene_final_prompt(o,&after.scene)||
       !after.scene.complete)return SOURCE_OBSERVER_READ_FAILED;
    after.counter=after.boundary.counter;after.applied_mask=after.boundary.guest_active_low_mask;
    if(!fields(o,&after))return SOURCE_OBSERVER_READ_FAILED;
    if(memcmp(&before,&after,sizeof(before)))return SOURCE_OBSERVER_INCOHERENT;
    before.complete=1;*out=before;return SOURCE_OBSERVER_OK;
}
static int released(const SourceObservation *s) {
    return s->abi==SOURCE_OBSERVER_ABI&&s->complete&&s->epoch&&s->add<=255&&s->sub<=255&&
        s->vblank<=255&&s->bg<=2&&s->io_div<=255&&s->boundary.complete&&s->boundary.coherent&&
        s->boundary.ordinary_supported&&s->boundary.counter_valid&&s->boundary.engine_type==1&&
        s->boundary.batch_count==1&&!s->boundary.host_phase&&!s->boundary.recording_gate&&
        !s->boundary.queued_tasks&&s->counter==s->boundary.counter&&s->applied_mask<=65535u&&
        (s->applied_mask&255u)==255u&&s->applied_mask!=255u&&
        s->applied_mask==s->boundary.guest_active_low_mask&&s->scene.complete&&s->scene.match&&
        s->scene.guest_pc==0x460&&s->scene.guest_sp==0xc0d5;
}
static int same_scene(const ChFinalPrompt *a,const ChFinalPrompt *b) {
    return a->rom_pointer==b->rom_pointer&&a->wram0_pointer==b->wram0_pointer&&
        a->wram1_pointer==b->wram1_pointer&&a->hram_pointer==b->hram_pointer&&a->io_pointer==b->io_pointer&&
        a->script_mode==b->script_mode&&a->script_running==b->script_running&&
        a->script_bank==b->script_bank&&a->script_next==b->script_next&&
        a->map_group==b->map_group&&a->map_number==b->map_number;
}
int source_observer_infer(const SourceObservation *a,const SourceObservation *b,
    const SourceNormalConditions *e,SourceInferredNormalPair *out) {
    SourceInferredNormalPair p;
    if(!a||!b||!out||!released(a)||!released(b)||a->epoch!=b->epoch||
       a->boundary.engine_pointer!=b->boundary.engine_pointer||b->counter-a->counter!=1u||
       ((b->vblank-a->vblank)&255u)!=1u||b->bg!=(a->bg+1u)%3u||
       !same_scene(&a->scene,&b->scene))return SOURCE_OBSERVER_GUARD_REJECTED;
    if(!e||e->abi!=SOURCE_OBSERVER_ABI||!e->restricted_wait_path_model||e->epoch!=a->epoch||
       e->before_counter!=a->counter||e->after_counter!=b->counter||e->normal_pair_count!=1||
       e->entry_carry!=0)return SOURCE_OBSERVER_NORMAL_PROOF_MISSING;
    memset(&p,0,sizeof(p));p.div1=(b->add-a->add)&255u;
    p.carry=(a->add+p.div1)>>8;p.div2=(a->sub-b->sub-p.carry)&255u;
    p.epoch=a->epoch;p.before_counter=a->counter;p.after_counter=b->counter;
    *out=p;return SOURCE_OBSERVER_OK;
}
