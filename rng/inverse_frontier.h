#ifndef CH_INVERSE_FRONTIER_H
#define CH_INVERSE_FRONTIER_H
#include "data/solver.h"
typedef struct {
    uint16_t head[65536],next[SOL61_FRONTIER_SLOTS];
    uint32_t waiting_pairs,intervals,lookups,solutions;
    uint32_t source_bg,rng_add,rng_sub,frontier_checksum,binding_seal,bg_bound;
    uint32_t prefix_sums[2][256];
} RawInverseWorkspace;
/* Index closed waiting RNG pairs, then solve only inverse shiny constraints.
   No native encounter replay, mutable profile, or N/x encounter enumeration. */
int raw_inverse_frontier(uint32_t,uint32_t,RawInverseWorkspace *,Sol61Frontier *);
#define RAW_BG_FRONTIER_ABI 2u
uint32_t raw_inverse_target_bg(uint32_t source_bg,uint32_t model_n);
int raw_inverse_frontier_bg(uint32_t,uint32_t,uint32_t,RawInverseWorkspace *,Sol61Frontier *);
int raw_inverse_frontier_select_bg(uint32_t,uint32_t,uint32_t,uint32_t,
    const RawInverseWorkspace *,const Sol61Frontier *,Sol61Result *);
#endif
