/* Exercise the real sink and painter. Mock SDK/memory ownership only; this
 * does not model LCD scanout timing or establish a hardware flicker fix. */
#ifndef _WIN32
#define _GNU_SOURCE
#include <sys/mman.h>
#else
#include <windows.h>
#endif
#include "hud/sink_3ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_BYTES (400u*240u*3u)
static RawHud3dsSink sink;
static uint8_t *pages[2];
static uint8_t reference[PAGE_BYTES],clean[PAGE_BYTES],clone[PAGE_BYTES];
static RawHud queued;
static uint32_t checks,locks,unlocks,waits,held,queries,flushes,queue_at_query;
static uint32_t background_locks,background_unlocks,background_waits,background_held;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"HUD failed line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)

/* A deterministic interleaving: a publisher already holds the value lock.
 * Blocking acquisition lets that pending copy finish; TryLock would drop
 * the fresh image's overlay. No drawing operation may acquire this lock. */
void LightLock_Init(LightLock *l){*l=0;}
void LightLock_Lock(LightLock *l){
    CHECK(!held&&!background_held);
    if(l==&sink.background_lock){
        background_locks++;
        if(*l){*l=0;background_waits++;}
        *l=1;background_held=1;return;
    }
    CHECK(l==&sink.lock);locks++;
    if(*l){sink.published=queued;*l=0;waits++;}
    *l=1;held=1;
}
void LightLock_Unlock(LightLock *l){
    if(l==&sink.background_lock){
        CHECK(background_held&&*l&&!held);*l=0;background_held=0;background_unlocks++;return;
    }
    CHECK(l==&sink.lock&&held&&*l);*l=0;held=0;unlocks++;
}
int LightLock_TryLock(LightLock *l){if(*l)return 1;LightLock_Lock(l);return 0;}
int32_t svcQueryMemory(MemInfo *m,PageInfo *p,uint32_t a){uint32_t i;
    CHECK(!held&&!background_held);queries++;memset(p,0,sizeof(*p));
    if(queue_at_query){queue_at_query=0;sink.lock=1;sink.background_lock=1;}
    for(i=0;i<2;i++)if((uint64_t)a>=(uintptr_t)pages[i]&&
       (uint64_t)a<(uintptr_t)pages[i]+PAGE_BYTES){
        *m=(MemInfo){(uint32_t)(uintptr_t)pages[i],PAGE_BYTES,MEMPERM_READ|MEMPERM_WRITE,5};return 0;
    }
    return -1;
}
int32_t svcFlushProcessDataCache(uint32_t handle,uint32_t a,uint32_t bytes){uint32_t i;
    CHECK(!held&&!background_held&&handle==CUR_PROCESS_HANDLE&&bytes);flushes++;
    for(i=0;i<2;i++)if((uint64_t)a>=(uintptr_t)pages[i]&&
       (uint64_t)a+bytes<=(uintptr_t)pages[i]+PAGE_BYTES)return 0;
    CHECK(0);return -1;
}
uint32_t svcConvertVAToPA(const void *p,bool write){(void)p;(void)write;CHECK(0);return 0;}
void mock_dsb(void){CHECK(!held&&!background_held);}

static uint8_t *low_page(uint32_t index){void *p;
    void *hint=(void *)(uintptr_t)(0x11000000u+index*0x01000000u);
#ifdef _WIN32
    p=VirtualAlloc(hint,PAGE_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    p=mmap(hint,PAGE_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(p==MAP_FAILED)p=0;
#endif
    CHECK(p&&(uintptr_t)p<=UINT32_MAX-PAGE_BYTES);return p;
}
static void free_page(uint8_t *p){
#ifdef _WIN32
    CHECK(VirtualFree(p,0,MEM_RELEASE));
#else
    CHECK(!munmap(p,PAGE_BYTES));
#endif
}
static RawHud hud(const char *target,uint32_t color){RawHud h;
    memset(&h,0,sizeof(h));h.visible=1;h.count=2;
    strcpy(h.line[0],"CelebiHunter");strcpy(h.line[1],target);
    h.color[0]=0xffffff;h.color[1]=color;return h;
}
static void fresh_page(uint32_t index,uint32_t seed){uint32_t n;
    /* Different game pixels every frame, including in old glyph positions. */
    for(n=0;n<PAGE_BYTES;n++)clean[n]=(uint8_t)(n*17u+seed);
    memcpy(pages[index],clean,PAGE_BYTES);memcpy(reference,clean,PAGE_BYTES);
}
static void expected(const RawHud *h){RawHudSurface s={reference,PAGE_BYTES,400,240,720,1};
    CHECK(raw_hud_paint_transparent(h,&s,&sink.layout,0)==RAW_HUD_PAINTED);
}
static void present(uint32_t index,const RawHud *h){uint32_t before=locks;
    expected(h);
    CHECK(raw_hud_3ds_present(&sink,0,index,pages[index],0,720,1)==RAW_HUD_SINK_PAINTED);
    CHECK(locks==before+1&&!held&&memcmp(pages[index],reference,PAGE_BYTES)==0);
}
static void restore(uint32_t index){
    memcpy(clone,pages[index],PAGE_BYTES);
    CHECK(raw_hud_3ds_restore_background(&sink,(uint32_t)(uintptr_t)pages[index],0,720,1,clone,PAGE_BYTES)==1);
    CHECK(memcmp(clone,clean,PAGE_BYTES)==0);
}
static void page_flip_and_publish_races(void){RawHud old=hud("Target 123456789",0xffdd70);
    RawHud next=hud("Target 7",0xffffff),fault=hud("Display fault [R20]",0xff9090);
    uint32_t before;
    CHECK(raw_hud_3ds_sink_init(&sink,0,0));raw_hud_3ds_publish(&sink,&old);
    /* A pending clone restore also completes before receipt/capture proceeds;
     * it cannot share the publisher's lock or cause a skipped overlay. */
    queued=old;sink.lock=1;sink.background_lock=1;
    fresh_page(0,11);present(0,&old);restore(0);

    /* Contention before snapshot cannot produce a frame without a HUD. */
    queued=next;sink.lock=1;before=waits;
    fresh_page(1,52);present(1,&next);CHECK(waits==before+1);restore(1);
    CHECK(raw_hud_3ds_restore_background(&sink,(uint32_t)(uintptr_t)pages[0],0,720,1,clone,PAGE_BYTES)==-1);

    /* A publisher arriving after the snapshot cannot cancel capture/paint.
     * This frame uses its complete prior value; the next frame uses the new
     * complete value. Genuine red error text still gets rendered. */
    queued=fault;queue_at_query=1;
    fresh_page(0,94);present(0,&next);CHECK(sink.lock==1);restore(0);
    fresh_page(1,135);present(1,&fault);restore(1);

    /* Hiding/step receipt never restores an older game image or draws from
     * the publisher. The exact current unpainted clone remains untouched. */
    next.visible=0;before=flushes;raw_hud_3ds_publish(&sink,&next);
    fresh_page(0,176);
    CHECK(raw_hud_3ds_present(&sink,0,0,pages[0],0,720,1)==RAW_HUD_SINK_SKIPPED);
    CHECK(!memcmp(pages[0],clean,PAGE_BYTES)&&flushes==before);restore(0);
    before=locks;
    CHECK(raw_hud_3ds_original_unpainted_present(&sink,0,1,pages[1],0,720,1)==1);
    CHECK(locks==before);
    memcpy(clone,clean,PAGE_BYTES);
    CHECK(raw_hud_3ds_restore_background(&sink,(uint32_t)(uintptr_t)pages[1],0,720,1,clone,PAGE_BYTES)==1);
    CHECK(!memcmp(clone,clean,PAGE_BYTES));
    CHECK(raw_hud_3ds_present(&sink,0,2,pages[0],0,720,1)==RAW_HUD_SINK_ERROR);
    CHECK(raw_hud_3ds_restore_background(&sink,(uint32_t)(uintptr_t)pages[1],0,720,1,clone,PAGE_BYTES)==-1);
    CHECK(locks==unlocks&&background_locks==background_unlocks&&background_waits==2);
    CHECK(!background_held&&queries&&flushes==4);
}
static int line_index(const RawHud *h,const char *line){uint32_t n;
    for(n=0;n<h->count;n++)if(!strcmp(h->line[n],line))return (int)n;
    return -1;
}
static void target_recheck_hud(void){RawRuntime r;RawHud h;int n;
    memset(&r,0,sizeof(r));r.runtime=CH_RUNTIME_PAUSED;r.counter_valid=1;
    r.counter=42;r.raw_target=42;r.controls.candidate_valid=1;
    r.controls.overlay_visible=1;r.waiting_valid=1;
    raw_runtime_hud(&r,&h);
    CHECK(line_index(&h,"A starts | L step | R run")>=0);
    CHECK(line_index(&h,"Start+Up: show/hide HUD")==6&&!h.line[4][0]);
    r.waiting_valid=0;r.waiting_recheck=1;raw_runtime_hud(&r,&h);
    CHECK(line_index(&h,"Advance 42 | Target 42")>=0);
    CHECK(line_index(&h,"A starts | L step | R run")==-1);
    CHECK(line_index(&h,"Live state recheck; target held")==-1&&!h.line[3][0]);
    CHECK(line_index(&h,"L step | R run")==5&&line_index(&h,"Start+Up: show/hide HUD")==6);
    r.fault=RAW_FAULT_DISPLAY;raw_runtime_hud(&r,&h);
    n=line_index(&h,"Display fault [R20]");CHECK(n>=0&&h.color[n]==0xff9090u);
    CHECK(line_index(&h,"Live state recheck; target held")==-1);
    CHECK(line_index(&h,"R or A: exit pause")==5&&line_index(&h,"Start+Up: show/hide HUD")==6);
}
int main(void){pages[0]=low_page(0);pages[1]=low_page(1);
    page_flip_and_publish_races();target_recheck_hud();free_page(pages[0]);free_page(pages[1]);
    printf("HUD present: %u checks passed (mock SDK, no hardware timing claim).\n",checks);return 0;
}
