#ifndef CH_OWNED_READ_SCOPE_H
#define CH_OWNED_READ_SCOPE_H
#include <stdint.h>
#include <string.h>
/* Positive mapping proofs last only for one exclusively owned read operation.
   Never keep this scope across native calls, waits, writes or callbacks. */
#define CH_READ_SCOPE_CAPACITY 16u
typedef struct { uint32_t base,size; } ChReadRegion;
typedef struct {
    uint32_t active,count,queries,hits,failures;
    ChReadRegion regions[CH_READ_SCOPE_CAPACITY];
} ChReadScope;
typedef int (*ChReadRegionQuery)(void *,uint32_t,ChReadRegion *);
static inline int ch_read_scope_begin(ChReadScope *s) {
    if(!s||s->active)return 0;
    memset(s,0,sizeof(*s));s->active=1u;return 1;
}
static inline void ch_read_scope_end(ChReadScope *s) {
    if(s)memset(s,0,sizeof(*s));
}
static inline int ch_read_scope_readable(ChReadScope *s,ChReadRegionQuery query,
                                       void *user,uint32_t address,uint32_t size) {
    uint64_t end=(uint64_t)address+size,cursor=address;
    if(!s||!query||!address||!size||end>UINT64_C(0x100000000))return 0;
    while(cursor<end) {
        ChReadRegion region={0u,0u};uint32_t i,found=0u;uint64_t region_end;
        if(s->active)for(i=0u;i<s->count;i++) {
            ChReadRegion *known=&s->regions[i];
            if(known->base<=cursor&&(uint64_t)known->base+known->size>cursor) {
                region=*known;found=1u;s->hits++;break;
            }
        }
        if(!found) {
            if(s->active)s->queries++;
            if(!query(user,(uint32_t)cursor,&region)||!region.size||
               region.base>cursor||(uint64_t)region.base+region.size<=cursor||
               (uint64_t)region.base+region.size>UINT64_C(0x100000000)) {
                if(s->active)s->failures++;
                return 0;
            }
            if(s->active&&s->count<CH_READ_SCOPE_CAPACITY)
                s->regions[s->count++]=region;
        }
        region_end=(uint64_t)region.base+region.size;
        if(region_end>=end)return 1;
        cursor=region_end;
    }
    return 0;
}
#endif
