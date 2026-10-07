#ifndef CH_PAUSED_3DS_H
#define CH_PAUSED_3DS_H
#include <3ds.h>
#include "paused_display.h"
#include "present_context.h"
enum {RAW_PAUSE_3DS_RESERVE=1,RAW_PAUSE_3DS_OBSERVE=2,RAW_PAUSE_3DS_ALLOCATE=3,
      RAW_PAUSE_3DS_QUERY=4,RAW_PAUSE_3DS_PHYSICAL=5,RAW_PAUSE_3DS_FLUSH=6,
      RAW_PAUSE_3DS_PUBLISH=7,RAW_PAUSE_3DS_RELEASE=8,RAW_PAUSE_3DS_BACKGROUND=9};
typedef struct {
    uint32_t stage,error_stage,error_code,last_result,last_address;
    uint32_t memory_state,memory_permission,physical,expected;
    uint32_t allocations,releases,flushes,publishes,observes;
    uint32_t error_result,error_address,error_memory_state,error_memory_permission;
    uint32_t error_physical,error_expected;
} RawPause3dsDiagnostic;
typedef struct {
    const RawPresentBinding *binding;
    uint32_t initialized,owned[2];
    RawPauseBuffer lease[2];
    RawPause3dsDiagnostic diagnostic;
    void *clean_user;
    int (*clean_overlay)(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint8_t *,uint32_t);
    /* Optional positive mapping proofs for one observe's read-only contract
       and paired data reads. Never span allocation, flush or native publish.
       A successful begin is always paired with end, including failed reads. */
    void *read_scope_user;
    int (*read_scope_begin)(void *);
    void (*read_scope_end)(void *);
} RawPause3ds;
/* Startup-only exclusive reservation, carved out BEFORE malloc is enabled.
 * Both slots remain Luma-owned Shared mappings for the process lifetime.
 * This function never allocates, frees, maps, paints or publishes memory.
 * Exactly two 0x47000-byte aligned slots are required. 1=accepted, 0=rejected.
 * It cannot replace a reservation while either slot is leased. */
int raw_paused_3ds_reserve(uint32_t base,uint32_t size);
/* Original GSP is reused without initialization or event processing. Mapping
 * validation uses actual Shared/RW loader pages and every-page VA-to-PA
 * conversion at lease creation. The fixed reserved mapping is never remapped;
 * flush/release require the exact retained certificate and exclusive owner.
 * Only slot leases are released after restoration acknowledgement;
 * the Luma mapping itself is never freed. Last failure diagnostics survive
 * successful rollback and subsequent retries. */
int raw_paused_3ds_init(RawPause3ds *,const RawPresentBinding *,RawPauseOps *);
#endif
