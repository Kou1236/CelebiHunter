#include "paused_3ds.h"
#include <string.h>
#include "paused_native_contract.h"
extern uint32_t svcConvertVAToPA(const void *,bool);
static struct {uint32_t base,size;RawPause3ds *owner[2];} reserved;
static int failed(RawPause3ds *b,uint32_t stage,uint32_t code){
    if(b){RawPause3dsDiagnostic *d=&b->diagnostic;
        d->stage=stage;d->error_stage=stage;d->error_code=code;
        d->error_result=d->last_result;d->error_address=d->last_address;
        d->error_memory_state=d->memory_state;d->error_memory_permission=d->memory_permission;
        d->error_physical=d->physical;d->error_expected=d->expected;
    }
    return -1;
}
int raw_paused_3ds_reserve(uint32_t base,uint32_t size){
    if(!base||(base&4095)||size!=2*RAW_PAUSE_ALLOC_BYTES||
       base>UINT32_MAX-size||reserved.owner[0]||reserved.owner[1])return 0;
    reserved.base=base;reserved.size=size;return 1;
}
static int read_words(const RawPause3ds *b,uint32_t a,void *out,uint32_t size){
    return b&&b->initialized&&b->binding&&b->binding->read.read_bytes&&
        b->binding->read.read_bytes(b->binding->read.user,a,out,size);
}
static int pause_contract(const RawPause3ds *b){uint32_t i,words[44];
    for(i=0;i<sizeof(raw_pause_guard_blocks)/sizeof(raw_pause_guard_blocks[0]);i++){
        if(raw_pause_guard_blocks[i].count>44||
           !read_words(b,raw_pause_guard_blocks[i].address,words,raw_pause_guard_blocks[i].count*4)||
           memcmp(words,raw_pause_guard_blocks[i].words,raw_pause_guard_blocks[i].count*4))return 0;
    }
    return 1;
}
static int observe(void *user,RawPauseSnapshot *out){RawPause3ds *b=user;RawPauseSnapshot s,t;uint32_t slot;
    if(!out||!b)return -1;
    b->diagnostic.stage=RAW_PAUSE_3DS_OBSERVE;b->diagnostic.observes++;
    if(!raw_present_contract_current(b->binding)||!pause_contract(b))return failed(b,RAW_PAUSE_3DS_OBSERVE,1);
    memset(out,0,sizeof(*out));memset(&s,0,sizeof(s));memset(&t,0,sizeof(t));
    if(!read_words(b,0x232398,&s.context,4)||!s.context||(s.context&3)||s.context>UINT32_MAX-0x75||
       !read_words(b,s.context+0x5c,&s.descriptor,4)||!s.descriptor||(s.descriptor&3)||
       s.descriptor>UINT32_MAX-64||!read_words(b,s.context+0x34,&s.event_queue,4)||
       !read_words(b,s.context+0x40,&s.shared_base,4)||!read_words(b,s.context+0x74,&s.thread_id,1)||
       !s.shared_base||(s.shared_base&3)||s.shared_base>UINT32_MAX-0x400||s.thread_id>3||
       s.event_queue!=s.shared_base+64u*s.thread_id||
       s.descriptor!=s.shared_base+0x200u+128u*s.thread_id||
       !read_words(b,s.event_queue,&s.queue_control,4)||!read_words(b,s.descriptor,&s.control,4)||
       !read_words(b,RAW_PAUSE_GRAPHICS_BUSY,&s.graphics_busy,1)||
       !read_words(b,RAW_PAUSE_TOP_EPOCH,&s.top_epoch,4))return failed(b,RAW_PAUSE_3DS_OBSERVE,2);
    if((s.control&0xff)>1||(s.queue_control&0xff)>=52||((s.queue_control>>8)&0xff)>52||
       ((s.queue_control>>16)&0xff)>1)return failed(b,RAW_PAUSE_3DS_OBSERVE,3);
    if(!read_words(b,s.descriptor+4+28*(s.control&0xff),&s.record,sizeof(s.record))||
       !read_words(b,RAW_PAUSE_TOP_EPOCH,&t.top_epoch,4)||
       !read_words(b,RAW_PAUSE_GRAPHICS_BUSY,&t.graphics_busy,1)||
       !read_words(b,s.descriptor,&t.control,4)||!read_words(b,s.event_queue,&t.queue_control,4)||
       !read_words(b,s.context+0x34,&t.event_queue,4)||!read_words(b,s.context+0x40,&t.shared_base,4)||
       !read_words(b,s.context+0x74,&t.thread_id,1)||!read_words(b,s.context+0x5c,&t.descriptor,4)||
       !read_words(b,0x232398,&slot,4))return failed(b,RAW_PAUSE_3DS_OBSERVE,4);
    if(slot!=s.context||t.descriptor!=s.descriptor||t.control!=s.control||
       t.graphics_busy!=s.graphics_busy||t.top_epoch!=s.top_epoch||t.queue_control!=s.queue_control||
       t.event_queue!=s.event_queue||t.shared_base!=s.shared_base||t.thread_id!=s.thread_id)return 0;
    *out=s;return 1;
}
static int mapping(RawPause3ds *owner,const RawPauseBuffer *b,uint32_t *physical){
    MemInfo m;PageInfo page;uint32_t offset,pa0=0,pa,last;Result result;
    if(!b||!b->pixels||b->bytes!=RAW_PAUSE_ALLOC_BYTES||b->vaddr!=(uint32_t)(uintptr_t)b->pixels||
       (b->vaddr&4095)||b->vaddr>UINT32_MAX-b->bytes){failed(owner,RAW_PAUSE_3DS_QUERY,1);return 0;}
    /* Luma maps its dedicated physical plugin memory as Shared, not the
     * application's Continuous linear allocation. Validate every page: an
     * endpoint-only test would miss a discontiguous interior physical page. */
    for(offset=0;offset<b->bytes;offset+=4096){
        uint32_t va=b->vaddr+offset;
        owner->diagnostic.stage=RAW_PAUSE_3DS_QUERY;owner->diagnostic.last_address=va;
        memset(&m,0,sizeof(m));result=svcQueryMemory(&m,&page,va);
        owner->diagnostic.last_result=(uint32_t)result;
        owner->diagnostic.memory_state=m.state;owner->diagnostic.memory_permission=m.perm;
        if(R_FAILED(result)||m.state!=MEMSTATE_SHARED||m.perm!=MEMPERM_READWRITE||
           m.base_addr>va||m.size<4096||va-m.base_addr>m.size-4096){
            failed(owner,RAW_PAUSE_3DS_QUERY,2);return 0;
        }
        owner->diagnostic.stage=RAW_PAUSE_3DS_PHYSICAL;
        pa=svcConvertVAToPA((const void *)(uintptr_t)va,true);
        if(!offset)pa0=pa;
        owner->diagnostic.physical=pa;owner->diagnostic.expected=pa0+offset;
        /* Match GSP's proven old-linear translation VA 14000000..1C000000. */
        if(pa0<UINT32_C(0x20000000)||pa0>UINT32_C(0x28000000)-b->bytes||
           (pa0&4095)||pa!=pa0+offset){failed(owner,RAW_PAUSE_3DS_PHYSICAL,1);return 0;}
        last=svcConvertVAToPA((const void *)(uintptr_t)(va+4095),true);
        owner->diagnostic.physical=last;owner->diagnostic.expected=pa+4095;
        if(last!=pa+4095){failed(owner,RAW_PAUSE_3DS_PHYSICAL,2);return 0;}
    }
    if((b->paddr&&b->paddr!=pa0)||(b->publish_vaddr&&b->publish_vaddr!=pa0-UINT32_C(0x0c000000))){
        failed(owner,RAW_PAUSE_3DS_PHYSICAL,3);return 0;
    }
    if(physical)*physical=pa0;
    return 1;
}
static int release(void *user,RawPauseBuffer *b){RawPause3ds *owner=user;uint32_t i;
    if(!b||!b->pixels)return 1;
    if(!owner)return -1;
    owner->diagnostic.stage=RAW_PAUSE_3DS_RELEASE;
    for(i=0;i<2;i++)if(owner->owned[i]==b->vaddr&&reserved.owner[i]==owner&&
        b->vaddr==reserved.base+i*RAW_PAUSE_ALLOC_BYTES)break;
    if(i==2||b->pixels!=owner->lease[i].pixels||b->bytes!=owner->lease[i].bytes||
       b->vaddr!=owner->lease[i].vaddr||b->paddr!=owner->lease[i].paddr||
       b->publish_vaddr!=owner->lease[i].publish_vaddr)return failed(owner,RAW_PAUSE_3DS_RELEASE,1);
    /* RawPausedDisplay calls this only before publication or after original
     * restoration plus both display-event acknowledgements. No Luma unmap. */
    reserved.owner[i]=0;owner->owned[i]=0;memset(&owner->lease[i],0,sizeof(owner->lease[i]));owner->diagnostic.releases++;
    owner->diagnostic.stage=RAW_PAUSE_3DS_RELEASE;memset(b,0,sizeof(*b));return 1;
}
static int allocate(void *user,uint32_t bytes,RawPauseBuffer *b){RawPause3ds *owner=user;RawPauseBuffer candidate;uint32_t pa,i;
    if(!owner||!b)return -1;
    memset(b,0,sizeof(*b));
    owner->diagnostic.stage=RAW_PAUSE_3DS_ALLOCATE;
    if(bytes!=RAW_PAUSE_ALLOC_BYTES||!reserved.base||reserved.size!=2*bytes)
        return failed(owner,RAW_PAUSE_3DS_ALLOCATE,1);
    for(i=0;i<2;i++)if(!reserved.owner[i]&&!owner->owned[i])break;
    if(i==2)return failed(owner,RAW_PAUSE_3DS_ALLOCATE,2);
    memset(&candidate,0,sizeof(candidate));candidate.vaddr=reserved.base+i*bytes;
    candidate.pixels=(uint8_t *)(uintptr_t)candidate.vaddr;candidate.bytes=bytes;
    if(!mapping(owner,&candidate,&pa))return -1;
    candidate.paddr=pa;candidate.publish_vaddr=pa-UINT32_C(0x0c000000);
    reserved.owner[i]=owner;owner->owned[i]=candidate.vaddr;owner->lease[i]=candidate;*b=candidate;
    owner->diagnostic.allocations++;owner->diagnostic.stage=RAW_PAUSE_3DS_ALLOCATE;return 1;
}
static int flush(void *user,const RawPauseBuffer *b,uint32_t bytes){RawPause3ds *owner=user;uint32_t i;Result result;
    if(!owner||!b)return -1;
    for(i=0;i<2;i++)if(reserved.owner[i]==owner&&owner->owned[i]==b->vaddr)break;
    if(i==2||!bytes||bytes>b->bytes||b->pixels!=owner->lease[i].pixels||
       b->bytes!=owner->lease[i].bytes||b->vaddr!=owner->lease[i].vaddr||
       b->paddr!=owner->lease[i].paddr||b->publish_vaddr!=owner->lease[i].publish_vaddr)
        return failed(owner,RAW_PAUSE_3DS_FLUSH,1);
    owner->diagnostic.stage=RAW_PAUSE_3DS_FLUSH;owner->diagnostic.last_address=b->vaddr;
    result=svcFlushProcessDataCache(CUR_PROCESS_HANDLE,b->vaddr,bytes);
    owner->diagnostic.last_result=(uint32_t)result;
    if(R_FAILED(result))return failed(owner,RAW_PAUSE_3DS_FLUSH,2);
    owner->diagnostic.flushes++;return 1;
}
static int publish(void *user,const RawPauseSnapshot *expected,const RawPauseRecord *r){
    RawPause3ds *b=user;RawPauseSnapshot now;int result;
    typedef void (*OriginalPresent)(uint32_t,uint32_t,const void *,const void *,uint32_t,uint32_t,uint32_t);
    if(!b||!expected||!r)return -1;
    if(!raw_present_contract_current(b->binding))return failed(b,RAW_PAUSE_3DS_PUBLISH,1);
    if(expected->queue_control&0x00ffff00u)return 0;
    result=observe(b,&now);if(result!=1)return result;
    if(memcmp(&now,expected,sizeof(now))||now.graphics_busy||(now.control&0xff00)||
       (now.queue_control&0x00ffff00u))return 0;
    /* GSPGPU_FramebufferInfo is a VA record. The original executable writer
     * copies these seven arguments without physical conversion or engine work. */
    ((OriginalPresent)(uintptr_t)RAW_PRESENT_TARGET)(0,r->swap,(const void *)(uintptr_t)r->fb_a,
        (const void *)(uintptr_t)r->fb_b,r->stride,r->format,r->display_select);
    b->diagnostic.publishes++;b->diagnostic.stage=RAW_PAUSE_3DS_PUBLISH;
    return 1;
}
static int background(void *user,const RawPauseSnapshot *expected,uint8_t *out,uint32_t bytes){
    RawPause3ds *b=user;RawPauseSnapshot before,after;size_t span;int r;
    if(!b||!expected||!out||!raw_hud_surface_span(400,240,expected->record.stride,expected->record.format,&span)||
       span!=bytes||bytes>RAW_PAUSE_PIXEL_BYTES)return -1;
    r=observe(b,&before);if(r!=1)return r;
    if(before.graphics_busy||(before.control&0xff00)||(before.queue_control&0x00ffff00u)||
       before.event_queue!=expected->event_queue||before.shared_base!=expected->shared_base||
       before.thread_id!=expected->thread_id||before.context!=expected->context||
       before.descriptor!=expected->descriptor||before.control!=expected->control||
       memcmp(&before.record,&expected->record,sizeof(before.record)))return 0;
    if(!read_words(b,expected->record.fb_a,out,bytes))return failed(b,RAW_PAUSE_3DS_BACKGROUND,1);
    if(b->clean_overlay){int clean=b->clean_overlay(b->clean_user,expected->record.fb_a,expected->record.fb_b,
        expected->record.stride,expected->record.format,out,bytes);
        if(clean!=1)return clean<0?failed(b,RAW_PAUSE_3DS_BACKGROUND,2):0;
    }
    r=observe(b,&after);if(r!=1)return r;
    /* Display events may progress without changing the source image. */
    if(after.graphics_busy||(after.control&0xff00)||(after.queue_control&0x00ffff00u)||
       before.event_queue!=after.event_queue||before.shared_base!=after.shared_base||
       before.thread_id!=after.thread_id||before.context!=after.context||
       before.descriptor!=after.descriptor||before.control!=after.control||
       memcmp(&before.record,&after.record,sizeof(before.record)))return 0;
    b->diagnostic.stage=RAW_PAUSE_3DS_BACKGROUND;return 1;
}
int raw_paused_3ds_init(RawPause3ds *b,const RawPresentBinding *binding,RawPauseOps *ops){
    if(!b||!binding||!ops||!binding->prepared||reserved.owner[0]==b||reserved.owner[1]==b)return 0;
    memset(b,0,sizeof(*b));b->binding=binding;b->initialized=1;
    *ops=(RawPauseOps){b,observe,allocate,release,flush,publish,background};return 1;
}
