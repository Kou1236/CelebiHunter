#include <3ds.h>
#include <string.h>
#include "backend_3ds.h"
extern Result svcMapProcessMemoryEx(Handle,uint32_t,Handle,uint32_t,uint32_t,uint32_t);
extern Result svcUnmapProcessMemoryEx(Handle,uint32_t,uint32_t);
extern uint32_t svcConvertVAToPA(const void *,bool);
extern void svcFlushEntireDataCache(void),svcInvalidateEntireInstructionCache(void);

static int region(void *u,uint32_t a,ChReadRegion *out) {
    MemInfo m;PageInfo p;(void)u;
    if(R_FAILED(svcQueryMemory(&m,&p,a))||m.state==MEMSTATE_FREE||
       !(m.perm&MEMPERM_READ))return 0;
    out->base=m.base_addr;out->size=m.size;return 1;
}
static int readable(void *u,uint32_t a,uint32_t size) {
    Ch3dsBackend *s=u;
    return s&&ch_read_scope_readable(&s->read_scope,region,s,a,size);
}
static int readbytes(void *user,uint32_t a,void *out,uint32_t n) {
    Ch3dsBackend *s=user;uint8_t *dst=out;uint32_t i;
    if(s&&s->read_scope.active)s->scoped_read_calls++;
    if(!out||!readable(user,a,n))return 0;
    for(i=0;i<n;i++)dst[i]=*(volatile const uint8_t *)(a+i);
    return 1;
}
static int readword(void *u,uint32_t a,uint32_t *v) {
    if((a&3u)||!v||!readable(u,a,4u))return 0;
    (void)u;*v=*(volatile const uint32_t *)a;return 1;
}
static int keys(void *u,uint32_t *out) {
    Ch3dsBackend *s=u;if(!s||!s->hid_ready||!out)return 0;
    /* libctru reads its own HID shared-memory/cache. The game's cached host
       words/provider/intersection are untouched and still sampled originally. */
    hidScanInput();*out=hidKeysHeld();return 1;
}
static int owned(void *u) {return ((Ch3dsBackend *)u)->startup_owned==1u;}
static int identity(void *u) {
    Ch3dsBackend *s=u;uint8_t digest[32];uint32_t i;s64 title=0;
    if(!owned(u)||R_FAILED(svcGetProcessInfo(&title,CUR_PROCESS_HANDLE,0x10001u))||
        (uint64_t)title!=UINT64_C(0x0004000000172800)||
        !readable(u,0x00100000u,CH_PRISTINE_PREFIX_SIZE))return 0;
    ch_platform_sha256((const uint8_t *)0x00100000u,CH_PRISTINE_PREFIX_SIZE,digest);
    for(i=0;i<32u;i++)if(digest[i]!=ch_pristine_prefix_sha256[i])return 0;
    s->identity_checked=1u;return 1;
}
static int freepage(void *u,uint32_t a) {
    MemInfo m;PageInfo p;(void)u;
    return !(a&4095u)&&R_SUCCEEDED(svcQueryMemory(&m,&p,a))&&
        m.state==MEMSTATE_FREE&&m.base_addr<=a&&
        (uint64_t)a+4096u<=(uint64_t)m.base_addr+m.size;
}
static int mapalias(void *u,uint32_t a,uint32_t source) {
    Ch3dsBackend *s=u;
    if(!owned(u)||!s->identity_checked||s->mapped_page||
        (source&4095u)||!freepage(u,a)||!readable(u,source,4096u)||
        R_FAILED(svcMapProcessMemoryEx(CUR_PROCESS_HANDLE,a,
            CUR_PROCESS_HANDLE,source,4096u,0u)))return 0;
    s->mapped_page=a;s->source_page=source;return 1;
}
static int unmapalias(void *u,uint32_t a) {
    Ch3dsBackend *s=u;
    if(!owned(u)||s->mapped_page!=a||
        R_FAILED(svcUnmapProcessMemoryEx(CUR_PROCESS_HANDLE,a,4096u)))return 0;
    s->mapped_page=0u;s->source_page=0u;return 1;
}
static int store(void *u,uint32_t a,uint32_t v) {
    uint32_t pa;Ch3dsBackend *s=u;
    if(!owned(u)||!s->identity_checked||!s->mapped_page||
        !(a==CH_RAW_PRE_SITE||a==CH_POST_SCAN_SITE||a==CH_MARKER_SITE||a==CH_SOURCE_SITE||a==CH_PRESENT_SITE||a==CH_RESTART_RECEIPT_SITE||a==CH_MENU_RESET_RECEIPT_SITE)||
        !readable(u,a,4u)||!(pa=svcConvertVAToPA((const void *)a,true)))return 0;
    *(volatile uint32_t *)(pa|0x80000000u)=v;return 1;
}
static int publish(void *u,uint32_t a,uint32_t n) {
    if(!owned(u)||!readable(u,a,n))return 0;
    svcFlushEntireDataCache();svcInvalidateEntireInstructionCache();return 1;
}
static int probe(void *u,uint32_t a) {
    Ch3dsBackend *s=u;uint32_t i,v;
    if(!owned(u)||!s->mapped_page||a<s->mapped_page||
        a>s->mapped_page+4088u||!readable(u,s->mapped_page,4096u)||
        !readable(u,s->source_page,4096u))return 0;
    for(i=0;i<4096u;i++)if(*(volatile const uint8_t *)(s->mapped_page+i)!=
        *(volatile const uint8_t *)(s->source_page+i))return 0;
    if(!readword(u,a,&v)||v!=0xe2200c61u||
        !readword(u,a+4u,&v)||v!=0xe12fff1eu)return 0;
    /* The pinned Luma plugin loader sets ForceRWXPages before plugin entry.
       MapProcessMemoryEx's logical query permission is RW|0x18; mmu.c clears
       XN for the new mapping under that flag. QueryMemory's EXEC bit is not
       an execution attestation. Do not use this backend outside that loader. */
    return ((uint32_t (*)(uint32_t))a)(0xabc123u)==(0xabc123u^0x6100u);
}
void ch_3ds_backend_ops(Ch3dsBackend *s,ChInstallOps *i,ChReadOps *r) {
    if(i)*i=(ChInstallOps){s,owned,identity,freepage,mapalias,unmapalias,
        readword,store,publish,probe};
    if(r)*r=(ChReadOps){s,readbytes,keys};
}
int ch_3ds_hid_init(Ch3dsBackend *s) {
    if(!s||s->hid_ready||R_FAILED(hidInit()))return 0;
    s->hid_ready=1u;return 1;
}
void ch_3ds_hid_exit(Ch3dsBackend *s) {
    if(s&&s->hid_ready){hidExit();s->hid_ready=0u;}
}
void ch_3ds_startup_finished(Ch3dsBackend *s) {if(s)s->startup_owned=0u;}
int ch_3ds_read_begin(Ch3dsBackend *s) {
    if(!s||!s->identity_checked||!ch_read_scope_begin(&s->read_scope))return 0;
    s->scoped_read_calls=0u;return 1;
}
void ch_3ds_read_end(Ch3dsBackend *s) {
    if(!s||!s->read_scope.active)return;
    s->last_read_calls=s->scoped_read_calls;
    s->last_mapping_queries=s->read_scope.queries;
    s->last_mapping_hits=s->read_scope.hits;
    s->last_mapping_failures=s->read_scope.failures;
    ch_read_scope_end(&s->read_scope);s->scoped_read_calls=0u;
}
int ch_3ds_worker_read_ops(const Ch3dsBackend *s,Ch3dsBackend *local,ChReadOps *r){
    if(!s||!local||local==s||!r||s->startup_owned||!s->identity_checked||!s->mapped_page)return 0;
    memset(local,0,sizeof(*local));
    local->identity_checked=s->identity_checked;local->mapped_page=s->mapped_page;
    local->source_page=s->source_page;
    *r=(ChReadOps){local,readbytes,0};return 1;
}
