#include <3ds.h>
#include <3ds/allocator/mappable.h>
#include <string.h>
#include "plugin_header.h"
#include "../device_3ds.h"
#include "../hud/paused_3ds.h"

/* Research entry for the pinned Luma game-start hook only. The startup-owned
 * device coordinator performs the English Crystal title/pristine-code guard
 * before any alias or instruction store. No other release entry is linked. */
extern char __end__[];
extern void __system_initSyscalls(void);
extern Result __sync_init(void);
extern void __sync_fini(void);
char *fake_heap_start,*fake_heap_end;
extern u32 __ctru_heap,__ctru_linear_heap;
u32 __ctru_heap_size,__ctru_linear_heap_size;
static u8 saved_threadvars[0x20];
static u32 saved_fpscr;
volatile s32 ch_loader_startup_status;

static int mapped(u32 address,u32 size,u32 permission){
    u64 end=(u64)address+size;u32 cursor=address;
    if(!size||end>0x100000000ULL)return 0;
    while((u64)cursor<end){
        MemInfo m;PageInfo p;
        if(R_FAILED(svcQueryMemory(&m,&p,cursor))||m.state==MEMSTATE_FREE||
           (m.perm&permission)!=permission||m.base_addr>cursor||
           (u64)m.base_addr+m.size<=cursor)return 0;
        if((u64)m.base_addr+m.size>=end)return 1;
        cursor=m.base_addr+m.size;
    }
    return 0;
}
static int heap_header(ChPluginHeader *h){
    u64 heap_end,exe_end;
    if(!mapped(CH_LOADER_BASE,sizeof(*h),MEMPERM_READ))return 0;
    memcpy(h,(const void *)CH_LOADER_BASE,sizeof(*h));
    heap_end=(u64)h->heapVA+h->heapSize;exe_end=(u64)CH_LOADER_BASE+h->exeSize;
    if(h->magic!=CH_LOADER_MAGIC||h->heapVA!=CH_LOADER_HEAP_BASE||
       h->heapSize<2u*RAW_PAUSE_ALLOC_BYTES+CH_LOADER_MIN_HEAP||((h->heapSize|h->exeSize)&4095u)||
       h->exeSize<0x1000u||exe_end<(u32)(uintptr_t)__end__||
       heap_end>CH_LOADER_BASE||exe_end>0x08000000u||
       !mapped(h->heapVA,h->heapSize,MEMPERM_READ|MEMPERM_WRITE))return 0;
    return 1;
}
static void restore_thread(void){
    memcpy(getThreadLocalStorage(),saved_threadvars,sizeof(saved_threadvars));
    __asm__ volatile("vmsr fpscr,%0"::"r"(saved_fpscr));
}
static void clear_heap(void){
    /* Loader mappings stay owned by Luma; only discard our allocator range. */
    fake_heap_start=fake_heap_end=0;
    __ctru_heap=__ctru_heap_size=0;
}
__attribute__((noreturn)) void ch_loader_abort(s32 status){
    RawDeviceStartupDiagnostic diagnostic;
    ch_loader_startup_status=status;
    raw_device_startup_diagnostic(&diagnostic,status);
    /* The standard Luma dump captures the current stack. Keep this complete
     * copied record in that frame; the old BSS-only status was not dumped. */
    __asm__ volatile(""::"r"(&diagnostic):"memory");
    /* svcBreak may be intercepted. Never return to Luma's game-start release
     * path even if an external handler returns from the panic notification. */
    svcBreak(USERBREAK_PANIC);
    for(;;)svcSleepThread(10000000);
}
/* Any unexpected newlib process-exit request has the same no-release rule.
 * In particular, never invoke libctru's application heap/apt exit lifecycle. */
__attribute__((noreturn)) void __ctru_exit(int rc){ch_loader_abort(rc?rc:-2);}
/* Controls use ordinary physical L/R/A/B only. Avoid APT probing and the
 * optional New3DS IRRST service during this startup-owned manual path. */
bool hidShouldUseIrrst(void){return false;}

void main(void){
    ChPluginHeader h;int status;
    ch_loader_startup_status=0;
    if(!heap_header(&h))return;
    memcpy(saved_threadvars,getThreadLocalStorage(),sizeof(saved_threadvars));
    __asm__ volatile("vmrs %0,fpscr":"=r"(saved_fpscr));
    /* As in CTRPF, reserve display storage before newlib or thread allocation.
       Luma owns the mapping; the general allocator never sees these pages. */
    if(!raw_paused_3ds_reserve(h.heapVA,2u*RAW_PAUSE_ALLOC_BYTES))return;
    __ctru_heap=h.heapVA+2u*RAW_PAUSE_ALLOC_BYTES;
    __ctru_heap_size=h.heapSize-2u*RAW_PAUSE_ALLOC_BYTES;
    fake_heap_start=(char *)(uintptr_t)__ctru_heap;
    fake_heap_end=(char *)(uintptr_t)(h.heapVA+h.heapSize);
    __system_initSyscalls();
    if(R_FAILED(__sync_init())){restore_thread();clear_heap();return;}
    mappableInit(OS_MAP_AREA_BEGIN,OS_MAP_AREA_END);
    if(R_FAILED(srvInit())){__sync_fini();restore_thread();clear_heap();return;}
    status=raw_device_startup(0);ch_loader_startup_status=status;
    if(status!=0&&status!=1)ch_loader_abort(status);
    if(status==0){
        /* Device guarantees no live worker/HID ownership on clean failure. */
        srvExit();__sync_fini();clear_heap();
    }
    /* Success retains heap/srv/arbiter/HID for the installed coordinator and
     * its worker. This is not an application exit or a unload-capable plugin. */
    restore_thread();
}
