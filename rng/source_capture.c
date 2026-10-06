#include "source_capture.h"
#include <string.h>
static const uint32_t globals[8]={0x22f640,0x22f644,0x22f698,0x22f6c8,0x22f6d4,0x22f6d8,0x22f6dc,0x22f768};
static uint32_t little(const uint8_t *p,uint32_t n){uint32_t v=0,i;for(i=0;i<n;i++)v|=(uint32_t)p[i]<<(i*8);return v;}
static int word(const ChReadOps *o,uint32_t a,uint32_t *v){uint8_t b[4];
    if(!o->read_bytes(o->user,a,b,4))return 0;
    *v=little(b,4);return 1;}
static int byte_at(const RawSourceCapture *s,uint32_t address,uint8_t *v) {
    uint32_t i;for(i=0;i<s->region_count;i++){const RawSourceRegion *r=&s->regions[i];
        if(address>=r->address&&address-r->address<r->size){*v=s->bytes[r->offset+address-r->address];return 1;}}
    return 0;
}
static int number(const RawSourceCapture *s,uint32_t a,uint32_t n,uint32_t *out) {
    uint8_t b[4];uint32_t i;for(i=0;i<n;i++)if(!byte_at(s,a+i,&b[i]))return 0;
    *out=little(b,n);return 1;
}
static int structural(RawSourceCapture *s,uint16_t mask) {
    uint32_t v,i;const uint32_t cpu[][3]={{0x22f5fc,2,0x460},{0x22f5fe,2,0xc0d5},
        {0x22f608,4,1},{0x22f60c,4,1},{0x22f764,1,0}};
    for(i=0;i<sizeof(cpu)/sizeof(cpu[0]);i++)
        if(!number(s,cpu[i][0],cpu[i][1],&v)||v!=cpu[i][2])return 0;
    if(!number(s,0x22f600,4,&v)||v<1||v>64)return 0;
    if(!number(s,0x22f766,2,&v)||v!=mask)return 0;
    {static const uint32_t housekeeping[]={0x22f6e4,0x22f858,0x22f85c,0x22f924,0x22f925};
     for(i=0;i<5;i++)if(!number(s,housekeeping[i],1,&v)||v)return 0;}
    /* This first original scheduler has drained the event admission words.
       Preserve audio instead of clearing bytes to manufacture that state. */
    {static const uint32_t drained[]={0x22f858,0x22f85c,0x22f924};
     for(i=0;i<3;i++)if(!number(s,drained[i],4,&v)||v)return 0;}
    for(i=0;i<3;i++)if(!number(s,s->pointers[2]+0x144+4*i,4,&v)||v)return 0;
    if(!number(s,s->pointers[5]+0x61,1,&s->rng_add)||
       !number(s,s->pointers[5]+0x62,1,&s->rng_sub))return 0;
    return 1;
}
int raw_capture_applied_source(const ChReadOps *o,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,uint32_t first,uint16_t mask,RawSourceCapture *out) {
    ChBoundarySample before,after;ChFinalPrompt prompt;uint32_t i,j,total=0,seen;
    uint8_t again[256];
    if(!o||!o->read_bytes||!c||!out||!epoch||!first||c->lr!=0x1a8340||
        !(mask==0xffff||mask==0xfffe))return 0;
    memset(out,0,sizeof(*out));out->abi=1;out->caller=*c;out->epoch=epoch;out->tls=tls;
    if(ch_sample_boundary(o,&before)!=CH_SAMPLE_OK||ch_scene_final_prompt(o,&prompt)!=1||!prompt.match)return 0;
    out->boundary=before;out->counter=before.counter;
    for(i=0;i<8;i++)if(!word(o,globals[i],&out->pointers[i])||!out->pointers[i]||(out->pointers[i]&3))return 0;
    if(out->pointers[1]!=out->pointers[0]+8||out->pointers[5]!=out->pointers[6]+128||
       out->pointers[7]!=out->pointers[3]+4096||out->pointers[2]!=before.engine_pointer)return 0;
    {RawSourceRegion regions[RAW_SOURCE_REGION_COUNT]={{out->pointers[3],32768,0,{0}},
        {out->pointers[6],256,0,{0}},{out->pointers[5],128,0,{0}},
        {out->pointers[4],160,0,{0}},{0x22f5fc,0x504,0,{0}},
        {0x232d00,0x900,0,{0}},{out->pointers[0],16,0,{0}},
        {out->pointers[2]+0x140,16,0,{0}},{0x1aa528,4,0,{0}},{0x230f7d,1,0,{0}}};
     memcpy(out->regions,regions,sizeof(regions));}
    for(i=0;i<RAW_SOURCE_REGION_COUNT;i++) {
        RawSourceRegion *r=&out->regions[i];
        if((uint64_t)r->address+r->size>UINT64_C(0x100000000)||total>RAW_SOURCE_MAX_BYTES-r->size)return 0;
        for(j=0;j<i;j++) {
            RawSourceRegion *a=&out->regions[j];
            if(r->address<(uint64_t)a->address+a->size&&a->address<(uint64_t)r->address+r->size&&
                !(i==2&&j==1))return 0;
        }
        r->offset=total;total+=r->size;
        if(!o->read_bytes(o->user,r->address,out->bytes+r->offset,r->size))return 0;
        /* This compares complete regions again, not only a shared counter.
           It detects external sound/RTC writers while the engine caller is
           held inside the owned callback. Never fill unknown bytes with zero. */
        for(j=0;j<r->size;j+=sizeof(again)) {
            seen=r->size-j;if(seen>sizeof(again))seen=sizeof(again);
            if(!o->read_bytes(o->user,r->address+j,again,seen)||
               memcmp(again,out->bytes+r->offset+j,seen))return 0;
        }
        ch_platform_sha256(out->bytes+r->offset,r->size,r->identity);
    }
    out->byte_count=total;out->region_count=RAW_SOURCE_REGION_COUNT;
    for(i=0;i<8;i++)if(!word(o,globals[i],&seen)||seen!=out->pointers[i])return 0;
    if(ch_sample_boundary(o,&after)!=CH_SAMPLE_OK||memcmp(&before,&after,sizeof(before)))return 0;
    if(memcmp(out->bytes+out->regions[1].offset+128,out->bytes+out->regions[2].offset,128))return 0;
    out->structural_match=(uint32_t)structural(out,mask);if(!out->structural_match)return 0;
    /* Hash initialized metadata and actual data, excluding the digest itself.
       Future conditions retain flags=0: this identity is a snapshot receipt. */
    ch_platform_sha256((const uint8_t *)out,(size_t)((uint8_t *)out->identity-(uint8_t *)out),out->identity);
    return 1;
}
int raw_capture_source(const ChReadOps *o,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,uint32_t first,RawSourceCapture *out) {
    return raw_capture_applied_source(o,c,tls,epoch,first,0xffff,out);
}
