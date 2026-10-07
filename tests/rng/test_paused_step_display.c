/* Real pause state machine, sink and painter; mock native submission and SDK.
 * Check the original first L image before any delayed pause-clone update. */
#ifndef _WIN32
#define _GNU_SOURCE
#include <sys/mman.h>
#else
#include <windows.h>
#endif
#include "hud/paused_display.h"
#include "hud/sink_3ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static RawPausedDisplay panel;
static RawHud3dsSink sink;
static RawPauseSnapshot current;
static RawPauseRecord game_record;
static uint8_t *game;
static uint8_t clean[RAW_PAUSE_PIXEL_BYTES],expected[RAW_PAUSE_PIXEL_BYTES];
static uint8_t private_pixels[2][RAW_PAUSE_ALLOC_BYTES];
static uint32_t checks,allocations,publications,backgrounds,cache_flushes;
static uint32_t display_events_during_flush,drift_during_flush;
static uint32_t flushes,head_during_flush,events_during_publish;
static LightLock *held;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"paused step line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
void LightLock_Init(LightLock *l){*l=0;}
void LightLock_Lock(LightLock *l){CHECK(!held&&!(*l));held=l;*l=1;}
void LightLock_Unlock(LightLock *l){CHECK(held==l&&*l);held=0;*l=0;}
int LightLock_TryLock(LightLock *l){if(*l)return 1;LightLock_Lock(l);return 0;}
int32_t svcQueryMemory(MemInfo *m,PageInfo *p,uint32_t a){
    CHECK(!held&&(uint64_t)a>=(uintptr_t)game&&(uint64_t)a<(uintptr_t)game+RAW_PAUSE_PIXEL_BYTES);
    *m=(MemInfo){(uint32_t)(uintptr_t)game,RAW_PAUSE_PIXEL_BYTES,MEMPERM_READ|MEMPERM_WRITE,5};
    memset(p,0,sizeof(*p));return 0;
}
int32_t svcFlushProcessDataCache(uint32_t handle,uint32_t a,uint32_t bytes){
    CHECK(!held&&handle==CUR_PROCESS_HANDLE&&(uint64_t)a>=(uintptr_t)game&&
        (uint64_t)a+bytes<=(uintptr_t)game+RAW_PAUSE_PIXEL_BYTES);cache_flushes++;return 0;
}
uint32_t svcConvertVAToPA(const void *p,bool write){(void)p;(void)write;CHECK(0);return 0;}
void mock_dsb(void){CHECK(!held);}
static int snapshot(void *u,RawPauseSnapshot *s){(void)u;*s=current;return 1;}
static int allocate(void *u,uint32_t bytes,RawPauseBuffer *b){uint32_t i=allocations++;(void)u;
    CHECK(i<2&&bytes==RAW_PAUSE_ALLOC_BYTES);
    *b=(RawPauseBuffer){private_pixels[i],0x06000000u+i*0x00100000u,
        0x24000000u+i*0x00100000u,bytes,0x18000000u+i*0x00100000u};return 1;
}
static int release(void *u,RawPauseBuffer *b){(void)u;(void)b;CHECK(0);return 0;}
static int flush(void *u,const RawPauseBuffer *b,uint32_t bytes){(void)u;
    CHECK(bytes==RAW_PAUSE_PIXEL_BYTES&&b->publish_vaddr!=current.record.fb_a);
    flushes++;
    current.top_epoch=(current.top_epoch+display_events_during_flush)%0x7fffffffu;
    current.queue_control=(current.queue_control&0xffffff00u)|(((current.queue_control&0xffu)+head_during_flush)%52u);
    if(drift_during_flush==1u)current.record.fb_a+=4096u;
    if(drift_during_flush==2u)current.context+=4u;
    if(drift_during_flush==3u)current.control|=0x100u;
    if(drift_during_flush==4u)current.queue_control|=0x100u;
    if(drift_during_flush==5u)current.graphics_busy=1u;
    if(drift_during_flush==6u)current.top_epoch--;
    return 1;
}
static int publish(void *u,const RawPauseSnapshot *s,const RawPauseRecord *r,uint32_t *epoch){(void)u;
    current.top_epoch=(current.top_epoch+events_during_publish)%0x7fffffffu;
    CHECK(raw_pause_snapshot_same_image(s,&current)&&!(current.control&0xff00u));
    *epoch=current.top_epoch;
    current.record=*r;current.control=(1-(current.control&0xffu))|0x100u;
    publications++;return 1;
}
static int background(void *u,const RawPauseSnapshot *s,uint8_t *out,uint32_t bytes){(void)u;
    CHECK(!memcmp(s,&current,sizeof(*s))&&s->record.fb_a==(uint32_t)(uintptr_t)game);
    CHECK(bytes==RAW_PAUSE_PIXEL_BYTES);backgrounds++;memcpy(out,game,bytes);
    CHECK(raw_hud_3ds_restore_background(&sink,s->record.fb_a,s->record.fb_b,
        s->record.stride,s->record.format,out,bytes)==1);
    CHECK(!memcmp(out,clean,bytes));return 1;
}
static RawHud hud(const char *advance){RawHud h;
    memset(&h,0,sizeof(h));h.visible=1;h.count=2;h.color[0]=h.color[1]=0xffffff;
    strcpy(h.line[0],"CelebiHunter");strcpy(h.line[1],advance);return h;
}
static void fresh(uint32_t seed){uint32_t i;
    for(i=0;i<RAW_PAUSE_PIXEL_BYTES;i++)clean[i]=(uint8_t)(i*31u+seed);
    memcpy(game,clean,sizeof(clean));
}
static void expect_hud(const RawHud *h){RawHudSurface s={expected,sizeof(expected),400,240,720,1};
    memcpy(expected,clean,sizeof(expected));
    CHECK(raw_hud_paint_transparent(h,&s,&sink.layout,0)==RAW_HUD_PAINTED);
}
static void acknowledged(void){current.control&=~0xff00u;current.top_epoch++;}
static void first_L_and_clone(uint32_t events,uint32_t drift){RawHud before=hud("Advance 41 | Target 100"),after=hud("Advance 42 | Target 100");
    RawPauseOps ops={0,snapshot,allocate,release,flush,publish,background};uint32_t count;
    allocations=publications=backgrounds=cache_flushes=display_events_during_flush=drift_during_flush=0u;
    flushes=head_during_flush=events_during_publish=0u;
    CHECK(raw_hud_3ds_sink_init(&sink,0,0));CHECK(raw_paused_display_init(&panel,&ops,0));
    memset(&current,0,sizeof(current));current.context=0x232500;current.shared_base=0x10002000;
    current.descriptor=current.shared_base+0x200;current.event_queue=current.shared_base;
    game_record=(RawPauseRecord){0,(uint32_t)(uintptr_t)game,0,720,1,0,0};
    current.record=game_record;current.top_epoch=10;fresh(5);raw_hud_3ds_publish(&sink,&before);
    CHECK(raw_hud_3ds_present(&sink,0,0,game,0,720,1)==RAW_HUD_SINK_PAINTED);
    CHECK(raw_paused_display_enter(&panel,&before,1)==RAW_PAUSE_WAIT);
    current.top_epoch++;CHECK(raw_paused_display_update(&panel,&before,1)==RAW_PAUSE_WAIT);
    current.top_epoch++;CHECK(raw_paused_display_update(&panel,&before,1)==RAW_PAUSE_WAIT);
    acknowledged();CHECK(raw_paused_display_update(&panel,&before,1)==RAW_PAUSE_WAIT);
    current.top_epoch++;CHECK(raw_paused_display_update(&panel,&before,1)==RAW_PAUSE_READY);
    CHECK(panel.state==RAW_PAUSE_ACTIVE&&publications==1);
    count=publications;CHECK(raw_paused_display_step(&panel,1)==RAW_PAUSE_READY);
    CHECK(panel.state==RAW_PAUSE_STEP_WAIT&&publications==count);
    /* Native engine renders one genuine fresh image, then the admitted
       STEP_WAIT presenter paints it before chaining the original writer. */
    fresh(91);raw_hud_3ds_publish(&sink,&after);
    CHECK(raw_paused_display_game_present(&panel,&game_record));
    CHECK(raw_hud_3ds_present(&sink,0,0,game,0,720,1)==RAW_HUD_SINK_PAINTED);
    expect_hud(&after);CHECK(!memcmp(game,expected,sizeof(expected)));
    current.record=game_record;current.control=0x100;
    /* The first submitted frame already has HUD. Its pending display and
       additional ownership event must not create an unpainted waiting gap. */
    CHECK(raw_paused_display_update(&panel,&after,1)==RAW_PAUSE_WAIT);
    CHECK(!memcmp(game,expected,sizeof(expected))&&publications==count);
    acknowledged();CHECK(raw_paused_display_update(&panel,&after,1)==RAW_PAUSE_WAIT);
    CHECK(!memcmp(game,expected,sizeof(expected))&&publications==count);
    current.top_epoch++;if(events==2u&&!drift)current.top_epoch=0x7ffffffeu;
    if(events){current.queue_control=51u;head_during_flush=1u;events_during_publish=2u;}
    display_events_during_flush=events;drift_during_flush=drift;
    CHECK(raw_paused_display_update(&panel,&after,1)==RAW_PAUSE_WAIT);
    if(drift){CHECK(publications==count&&panel.state==RAW_PAUSE_STEP_WAIT);
        CHECK(panel.prepared_attempts==2u&&panel.prepared_snapshot_retries+panel.prepared_source_retries==1u);return;}
    CHECK(panel.state==RAW_PAUSE_PANEL_PENDING&&publications==count+1&&backgrounds==2);
    CHECK(flushes==2u);
    CHECK(panel.prepared_attempts==2u&&!panel.prepared_snapshot_retries&&!panel.prepared_source_retries&&!panel.native_publish_retries);
    CHECK(panel.submitted_epoch==current.top_epoch);
    CHECK(!memcmp(panel.buffers[panel.current].pixels,expected,sizeof(expected)));
    CHECK(!memcmp(panel.background,clean,sizeof(clean))&&!memcmp(game,expected,sizeof(expected)));
    acknowledged();CHECK(raw_paused_display_update(&panel,&after,1)==RAW_PAUSE_WAIT);
    current.top_epoch++;CHECK(raw_paused_display_update(&panel,&after,1)==RAW_PAUSE_READY);
    CHECK(panel.state==RAW_PAUSE_ACTIVE&&cache_flushes==2);
}
int main(void){void *hint=(void *)(uintptr_t)0x11000000;
#ifdef _WIN32
    game=VirtualAlloc(hint,RAW_PAUSE_PIXEL_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    game=mmap(hint,RAW_PAUSE_PIXEL_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(game==MAP_FAILED)game=0;
#endif
    CHECK(game&&(uintptr_t)game<=UINT32_MAX-RAW_PAUSE_PIXEL_BYTES);
    first_L_and_clone(0u,0u);first_L_and_clone(17u,0u);first_L_and_clone(2u,0u);
    {uint32_t n;for(n=1u;n<=6u;n++)first_L_and_clone(0u,n);}
#ifdef _WIN32
    CHECK(VirtualFree(game,0,MEM_RELEASE));
#else
    CHECK(!munmap(game,RAW_PAUSE_PIXEL_BYTES));
#endif
    printf("paused first L: %u checks passed (real state machine, sink and painter).\n",checks);return 0;
}
