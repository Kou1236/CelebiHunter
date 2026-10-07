#include <3ds.h>
#include <string.h>
#include "environment_backend_3ds.h"
#include "restart_lifecycle.h"
extern uint32_t svcConvertVAToPA(const void *,bool);
extern void svcFlushEntireDataCache(void),svcInvalidateEntireInstructionCache(void);
static int owned(RawEnvironmentBackend3ds *s){uint32_t thread_id;
    return s&&s->active&&s->backend&&s->backend->identity_checked&&s->backend->mapped_page&&
        !s->backend->startup_owned&&R_SUCCEEDED(svcGetThreadId(&thread_id,CUR_THREAD_HANDLE))&&
        thread_id==s->thread_id;
}
static int read(void *u,uint32_t a,void *b,uint32_t n){RawEnvironmentBackend3ds *s=u;
    return s&&s->original_read.read_bytes(s->original_read.user,a,b,n);}
static int physical(void *u,uint32_t *keys){RawEnvironmentBackend3ds *s=u;
    return s&&s->original_read.read_physical_keys&&s->original_read.read_physical_keys(s->original_read.user,keys);}
static int writable(uint32_t a){MemInfo m;PageInfo p;
    return R_SUCCEEDED(svcQueryMemory(&m,&p,a))&&m.state!=MEMSTATE_FREE&&
        (m.perm&(MEMPERM_READ|MEMPERM_WRITE))==(MEMPERM_READ|MEMPERM_WRITE)&&
        m.base_addr<=a&&(uint64_t)a<(uint64_t)m.base_addr+m.size;}
static int data(void *u,uint32_t a,uint8_t value){RawEnvironmentBackend3ds *s=u;
    if(!owned(s)||s->cleanup_only||!raw_environment_data_address(&s->original_read,a)||!writable(a))return 0;
    *(volatile uint8_t *)a=value;return 1;
}
static int data_span(void *u,uint32_t a,const uint8_t *values,uint32_t n){
    RawEnvironmentBackend3ds *s=u;MemInfo m;PageInfo p;uint32_t i;
    if(!values||!n||n>256u||(uint64_t)a+n>UINT64_C(0x100000000)||!owned(s)||s->cleanup_only)return 0;
    if(!raw_environment_data_range(&s->original_read,a,n))return 0;
    /* The byte backend used the same mapping/thread checks once per byte.
       A span cannot cross the single queried RW mapping, and its complete
       typed address range is checked before even the first byte is stored. */
    if(R_FAILED(svcQueryMemory(&m,&p,a))||m.state==MEMSTATE_FREE||
       (m.perm&(MEMPERM_READ|MEMPERM_WRITE))!=(MEMPERM_READ|MEMPERM_WRITE)||
       m.base_addr>a||(uint64_t)a+n>(uint64_t)m.base_addr+m.size)return 0;
    for(i=0;i<n;i++)*(volatile uint8_t *)(a+i)=values[i];
    return 1;
}
static int rtc(void *u,uint32_t value){RawEnvironmentBackend3ds *s=u;uint32_t pa,current;
    if(!owned(s)||(s->cleanup_only&&value!=0x0bfef301)||!(value==0x0bfef301||value==0xe1a00000)||
       !read(s,0x1aa528,&current,4)||!(current==0x0bfef301||current==0xe1a00000)||
       !(pa=svcConvertVAToPA((const void *)0x1aa528,true)))return 0;
    *(volatile uint32_t *)(pa|0x80000000u)=value;return 1;
}
static int publish(void *u){if(!owned(u))return 0;
    svcFlushEntireDataCache();svcInvalidateEntireInstructionCache();return 1;}
int raw_environment_backend_init(RawEnvironmentBackend3ds *s,Ch3dsBackend *b,const ChReadOps *r,RawEnvironmentOps *o){
    if(!s||!b||!r||!r->read_bytes||!o)return 0;
    memset(s,0,sizeof(*s));s->backend=b;s->original_read=*r;
    *o=(RawEnvironmentOps){{s,read,physical},data,rtc,publish,data_span};return 1;
}
int raw_environment_backend_enter(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    if(!s||s->active||!c||c->lr!=0x1a8340||R_FAILED(svcGetThreadId(&s->thread_id,CUR_THREAD_HANDLE)))return 0;
    s->active=1;if(!owned(s)){s->active=0;return 0;}return 1;
}
int raw_environment_backend_enter_restart(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    if(!s||s->active||!raw_restart_receipt(&s->original_read,c)||
       R_FAILED(svcGetThreadId(&s->thread_id,CUR_THREAD_HANDLE)))return 0;
    s->active=1;s->cleanup_only=1;
    if(!owned(s)){s->active=s->cleanup_only=0;return 0;}return 1;
}
void raw_environment_backend_leave(RawEnvironmentBackend3ds *s){if(s){s->active=0;s->thread_id=0;s->cleanup_only=0;}}
int raw_environment_backend_enter_restart_boundary(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    if(!s||s->active||!c||c->lr!=0x1042f4u||
       R_FAILED(svcGetThreadId(&s->thread_id,CUR_THREAD_HANDLE)))return 0;
    s->active=1u;s->cleanup_only=1u;
    if(!owned(s)){s->active=s->cleanup_only=0u;return 0;}return 1;
}
