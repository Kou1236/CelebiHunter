#include "aligned_waiting.h"
#include <string.h>
static uint32_t word(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static int regular_mask(uint32_t m){return m<=65535u&&(m&255u)==255u&&m!=255u;}
static int read_at(const ChReadOps *o,uint32_t a,void *v,uint32_t n,uint32_t ordinal,SourceAlignedDiagnostic *d) {
    int good=a&&n&&(uint64_t)a+n<=UINT64_C(0x100000000)&&o->read_bytes(o->user,a,v,n);
    if(!good&&d){d->read_ordinal=ordinal;d->read_address=a;d->read_size=n;d->read_pass=ordinal<=13?1:2;}
    return good;
}
static int fields(const ChReadOps *o,const ChFinalPrompt *p,AlignedFields *f,uint32_t base,SourceAlignedDiagnostic *d) {
    memset(f,0,sizeof(*f));
    return read_at(o,0x22f6d8,f->pointers,8,base+1,d)&&read_at(o,0x22f5fc,f->cpu,20,base+2,d)&&
        read_at(o,0x22f6f0,f->lcd,20,base+3,d)&&read_at(o,0x22f738,f->lcd_mode,8,base+4,d)&&
        read_at(o,0x22f75c,f->enabled,4,base+5,d)&&read_at(o,0x22fa48,f->phase,12,base+6,d)&&
        read_at(o,p->io_pointer+4,f->io_clocks,4,base+7,d)&&read_at(o,p->io_pointer+15,&f->iflags,1,base+8,d)&&
        read_at(o,p->io_pointer+255,&f->ie,1,base+9,d)&&read_at(o,p->io_pointer+0x41,f->lcd_io,4,base+10,d)&&
        read_at(o,p->hram_pointer+0x1b,f->hram_flags,4,base+11,d)&&
        read_at(o,p->hram_pointer+0x55,&f->bg,1,base+12,d)&&read_at(o,p->hram_pointer+0x61,f->rng,2,base+13,d);
}
static int failure(SourceAlignedDiagnostic *d,uint32_t stage,uint32_t guard,const AlignedFields *a,const AlignedFields *b) {
    if(d){d->stage=stage;d->domain_guard=guard;d->raw_valid=a!=0;
        if(a)d->first=*a;else memset(&d->first,0,sizeof(d->first));
        if(b)d->second=*b;else memset(&d->second,0,sizeof(d->second));}
    return 0;
}
int source_aligned_waiting_diagnostic(const ChReadOps *o,const ChBoundarySample *b,const ChFinalPrompt *p,
    uint64_t epoch,SourceObservation *out,CQWaitingState *waiting,SourceAlignedDiagnostic *d) {
    AlignedFields a,again;SourceObservation observed;CQWaitingState seed;uint32_t phase,cd,budget,deadline;
    if(d){d->abi=1;d->stage=SOURCE_ALIGNED_OK;d->read_ordinal=d->read_address=d->read_size=d->read_pass=0;
        d->domain_guard=d->raw_valid=0;}
    if(!o||!o->read_bytes||!b||!p||!out||!waiting||!epoch||!b->complete||!b->coherent||
       !b->ordinary_supported||!b->counter_valid||b->engine_type!=1||b->batch_count!=1||
       b->recording_gate||b->queued_tasks||b->host_phase||b->engine_mode||b->engine_flags||
       b->input_enable||!regular_mask(b->guest_active_low_mask)||!p->complete||!p->match||
       p->guest_pc!=0x460||p->guest_sp!=0xc0d5||!p->io_pointer||!p->hram_pointer||
       (p->io_pointer&3u)||(p->hram_pointer&3u)||p->io_pointer>UINT32_MAX-256u||
       p->hram_pointer>UINT32_MAX-128u||p->hram_pointer!=p->io_pointer+128u)
        return failure(d,SOURCE_ALIGNED_BOUNDARY,0,0,0);
    if(!fields(o,p,&a,0,d))return failure(d,SOURCE_ALIGNED_READ,0,&a,0);
    if(!fields(o,p,&again,13,d))return failure(d,SOURCE_ALIGNED_READ,0,&a,&again);
    if(memcmp(&a,&again,sizeof(a)))return failure(d,SOURCE_ALIGNED_DRIFT,0,&a,&again);
    phase=word(a.phase);deadline=word(a.phase+4);cd=word(a.phase+8);budget=word(a.cpu+4);
#define DOMAIN(ok,id) do{if(!(ok))return failure(d,SOURCE_ALIGNED_DOMAIN,id,&a,&again);}while(0)
    DOMAIN(word(a.pointers)==p->hram_pointer&&word(a.pointers+4)==p->io_pointer,1);
    DOMAIN(((uint32_t)a.cpu[0]|(uint32_t)a.cpu[1]<<8)==0x460&&
       ((uint32_t)a.cpu[2]|(uint32_t)a.cpu[3]<<8)==0xc0d5&&word(a.cpu+12)==1&&word(a.cpu+16)==1,2);
    DOMAIN(a.io_clocks[2]==0,3);DOMAIN(a.io_clocks[3]==4,4);DOMAIN(a.iflags&a.ie&1u,5);
    DOMAIN(!(a.hram_flags[3]&7u),6);DOMAIN(word(a.lcd)==113,7);
    DOMAIN(word(a.lcd+4)==51&&word(a.lcd+8)==114&&word(a.lcd+12)==20&&word(a.lcd+16)==43,8);
    DOMAIN(word(a.lcd_mode)==1&&word(a.lcd_mode+4)==0,9);
    DOMAIN(a.lcd_io[3]==144&&(a.lcd_io[0]&3u)==1,10);DOMAIN(word(a.enabled)==1,11);DOMAIN(a.bg<=2,12);
    DOMAIN(phase<=255,13);DOMAIN(cd>=1&&cd<=64,14);DOMAIN(deadline==phase+1,15);
    DOMAIN(budget==(cd<phase+1?cd:phase+1),16);
#undef DOMAIN
    memset(&observed,0,sizeof(observed));observed.abi=SOURCE_OBSERVER_ABI;observed.complete=1;
    observed.epoch=epoch;observed.boundary=*b;observed.scene=*p;observed.counter=b->counter;
    observed.applied_mask=b->guest_active_low_mask;observed.add=a.rng[0];observed.sub=a.rng[1];
    observed.vblank=a.hram_flags[0];observed.bg=a.bg;observed.io_div=a.io_clocks[0];observed.budget=budget;
    observed.clock_phase[0]=phase;observed.clock_phase[1]=deadline;observed.clock_phase[2]=cd;
    memset(&seed,0,sizeof(seed));seed.a=observed.add;seed.s=observed.sub;seed.bg=observed.bg;
    seed.clock.div=observed.io_div;seed.clock.div_countdown=cd;seed.clock.timer_phase=phase;
    seed.clock.budget=budget;seed.clock.lcd_countdown=113;
    *out=observed;*waiting=seed;return 1;
}
int source_aligned_waiting(const ChReadOps *o,const ChBoundarySample *b,const ChFinalPrompt *p,
    uint64_t epoch,SourceObservation *out,CQWaitingState *waiting) {
    return source_aligned_waiting_diagnostic(o,b,p,epoch,out,waiting,0);
}
