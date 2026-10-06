#include "present_context.h"
#include <string.h>
static const uint32_t wrapper[8]={
    0xe92d4030,0xe24dd00c,0xe28de018,0xe89e1030,0xe88d1030,RAW_PRESENT_ORIGINAL_BL,0xe28dd00c,0xe8bd8030
};
static const uint32_t callee[37]={
    0xe92d5ff0,0xe1a06000,0xe28d0028,0xe3a08000,0xe1a07001,0xe8900e00,
    0xe1a04002,0xe1a05003,0xeb00b516,0xe3560002,0xe280105c,0x2a000017,
    0xe7912106,0xe3a03004,0xe5d20000,0xe2600001,0xe20000ff,0xe060e180,
    0xe083310e,0xe7a27003,0xe2823004,0xe5828018,0xe8830e30,0xee078f9a,
    0xe3a0c001,0xe7912106,0xe1923f9f,0xe3c330ff,0xe1a0440c,0xe1833000,
    0xe2044cff,0xe3c33cff,0xe1833004,0xe1824f93,0xe3540000,0x1afffff4,0xe8bd9ff0
};
static const uint32_t accessor[4]={0xe59f0004,0xe590001c,0xe12fff1e,0x0023237c};
static const uint32_t upstream[14]={
    0xe595114c,0xe1a00004,0xe1cd80f0,0xe58d1008,0xebff3266,0xe595014c,
    0xe3500000,0x03a00001,0x13a00000,0xe585014c,0xe2844001,0xe3540002,0xbaffffa2,0xe28dd00c
};
static int same(const ChReadOps *r,uint32_t addr,const uint32_t *expected,uint32_t count){
    uint32_t words[37];
    return count<=37&&r->read_bytes(r->user,addr,words,count*4)&&!memcmp(words,expected,count*4);
}
static int code(const ChReadOps *r,uint32_t site_word){uint32_t current[8];
    if(!r||!r->read_bytes||!r->read_bytes(r->user,0x145468,current,sizeof(current)))return 0;
    if(current[5]!=site_word)return 0;
    current[5]=RAW_PRESENT_ORIGINAL_BL;
    return !memcmp(current,wrapper,sizeof(current))&&same(r,RAW_PRESENT_TARGET,callee,37)&&
        same(r,0x177ea4,accessor,4)&&same(r,0x178ab8,upstream,14);
}
int raw_present_prepare(RawPresentBinding *b,const ChReadOps *r){
    if(!b||!r||!r->read_bytes)return 0;
    memset(b,0,sizeof(*b));
    if(!code(r,RAW_PRESENT_ORIGINAL_BL))return 0;
    b->read=*r;b->prepared=1;b->site_word=RAW_PRESENT_ORIGINAL_BL;return 1;
}
int raw_present_activate(RawPresentBinding *b,uint32_t word){
    if(!b||!b->prepared||b->active||(word>>24)!=0xeb||!code(&b->read,word))return 0;
    b->site_word=word;b->active=1;return 1;
}
int raw_present_contract_current(const RawPresentBinding *b){
    return b&&b->prepared&&b->active&&code(&b->read,b->site_word);
}
int raw_present_decode(const RawPresentBinding *b,const ChNativeContext *c,RawPresentArgs *a){
    RawPresentArgs v={0};uint32_t stack[3];size_t span;
    if(a)memset(a,0,sizeof(*a));
    if(!a||!b||!b->prepared||!b->active||!c||c->lr!=RAW_PRESENT_RETURN||
        !c->original_sp||(c->original_sp&3)||c->original_sp>UINT32_MAX-11u||
        !code(&b->read,b->site_word)||!b->read.read_bytes(b->read.user,c->original_sp,stack,sizeof(stack)))return 0;
    v.screen_id=c->r[0];v.swap=c->r[1];v.fb_a=c->r[2];v.fb_b=c->r[3];
    v.stride=stack[0];v.format=stack[1];v.display_select=stack[2];
    v.width=v.screen_id==0?400u:320u;v.height=240;v.caller_sp=c->original_sp;v.caller_lr=c->lr;
    if(v.screen_id>1||v.swap>1||v.display_select>1||v.display_select!=v.swap||!v.fb_a||
        (v.screen_id==1&&v.fb_b)||!raw_hud_surface_span(v.width,v.height,v.stride,v.format,&span)||
        span>UINT32_MAX-v.fb_a||(v.fb_b&&span>UINT32_MAX-v.fb_b))return 0;
    v.surface_bytes=(uint32_t)span;*a=v;return 1;
}
