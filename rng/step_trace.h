#ifndef CH_STEP_TRACE_H
#define CH_STEP_TRACE_H
#include <stdint.h>
#include <string.h>
/* Last explicit L request only. Observations never authorize game execution.
 * seen, rather than tick!=0, distinguishes a valid zero tick from no receipt.
 * Fault bits survive later receipts and requests until explicit init.
 * The owner serializes helper calls and read-only copies; no synchronization
 * or SDK endpoint is hidden here. Mark only the explicit current L path. */
typedef struct {
    uint32_t request_id,origin_counter,target_counter,seen,fault;
    uint64_t epoch;
    uint64_t accepted_tick,present_begin_tick,present_end_tick;
    uint64_t actual_boundary_tick,clone_published_tick,clone_acknowledged_tick;
    uint32_t pause_update_count;
    uint64_t pause_update_ticks,pause_update_max_ticks;
} RawStepTrace;
enum {
    RAW_STEP_TRACE_ACCEPTED=1u,RAW_STEP_TRACE_PRESENT_BEGIN=2u,
    RAW_STEP_TRACE_PRESENT_END=4u,RAW_STEP_TRACE_ACTUAL_BOUNDARY=8u,
    RAW_STEP_TRACE_CLONE_PUBLISHED=16u,RAW_STEP_TRACE_CLONE_ACKNOWLEDGED=32u
};
enum {
    RAW_STEP_TRACE_BAD_REQUEST=1u,RAW_STEP_TRACE_BAD_ORDER=2u,
    RAW_STEP_TRACE_TICK_BACKWARD=4u,RAW_STEP_TRACE_BAD_BOUNDARY=8u
};
static inline void raw_step_trace_init(RawStepTrace *t){if(t)memset(t,0,sizeof(*t));}
static inline int raw_step_trace_active(const RawStepTrace *t){
    return t&&(t->seen&RAW_STEP_TRACE_ACCEPTED)&&!(t->seen&RAW_STEP_TRACE_CLONE_ACKNOWLEDGED);
}
static inline int raw_step_trace_accept(RawStepTrace *t,uint32_t request_id,
    uint64_t epoch,uint32_t origin,uint32_t target,uint64_t tick){uint32_t fault;
    if(!t)return 0;
    if(!request_id||!epoch||target!=origin+1u){t->fault|=RAW_STEP_TRACE_BAD_REQUEST;return 0;}
    if((t->seen&RAW_STEP_TRACE_ACCEPTED)&&t->request_id==request_id&&t->epoch==epoch)return 0;
    fault=t->fault;memset(t,0,sizeof(*t));t->fault=fault;
    t->request_id=request_id;t->epoch=epoch;t->origin_counter=origin;t->target_counter=target;
    t->accepted_tick=tick;t->seen=RAW_STEP_TRACE_ACCEPTED;return 1;
}
static inline int raw_step_trace_order(RawStepTrace *t,uint32_t required,uint64_t floor,uint64_t tick){
    if((t->seen&required)!=required){t->fault|=RAW_STEP_TRACE_BAD_ORDER;return 0;}
    if(tick<floor){t->fault|=RAW_STEP_TRACE_TICK_BACKWARD;return 0;}
    return 1;
}
static inline int raw_step_trace_present_begin(RawStepTrace *t,uint64_t tick){
    if(!raw_step_trace_active(t)||t->seen&RAW_STEP_TRACE_PRESENT_BEGIN)return 0;
    if(t->seen&RAW_STEP_TRACE_ACTUAL_BOUNDARY){t->fault|=RAW_STEP_TRACE_BAD_ORDER;return 0;}
    if(!raw_step_trace_order(t,RAW_STEP_TRACE_ACCEPTED,t->accepted_tick,tick))return 0;
    t->present_begin_tick=tick;t->seen|=RAW_STEP_TRACE_PRESENT_BEGIN;return 1;
}
static inline int raw_step_trace_present_end(RawStepTrace *t,uint64_t tick){
    if(!raw_step_trace_active(t)||t->seen&RAW_STEP_TRACE_PRESENT_END)return 0;
    if(!raw_step_trace_order(t,RAW_STEP_TRACE_PRESENT_BEGIN,t->present_begin_tick,tick))return 0;
    t->present_end_tick=tick;t->seen|=RAW_STEP_TRACE_PRESENT_END;return 1;
}
static inline int raw_step_trace_actual_boundary(RawStepTrace *t,uint64_t epoch,uint32_t counter,uint64_t tick){
    uint64_t floor;
    if(!raw_step_trace_active(t)||t->seen&RAW_STEP_TRACE_ACTUAL_BOUNDARY)return 0;
    if(epoch!=t->epoch||counter!=t->target_counter){t->fault|=RAW_STEP_TRACE_BAD_BOUNDARY;return 0;}
    if((t->seen&RAW_STEP_TRACE_PRESENT_BEGIN)&&!(t->seen&RAW_STEP_TRACE_PRESENT_END)){
        t->fault|=RAW_STEP_TRACE_BAD_ORDER;return 0;
    }
    floor=t->seen&RAW_STEP_TRACE_PRESENT_END?t->present_end_tick:t->accepted_tick;
    if(!raw_step_trace_order(t,RAW_STEP_TRACE_ACCEPTED,floor,tick))return 0;
    t->actual_boundary_tick=tick;t->seen|=RAW_STEP_TRACE_ACTUAL_BOUNDARY;return 1;
}
static inline int raw_step_trace_clone_published(RawStepTrace *t,uint64_t tick){
    if(!raw_step_trace_active(t)||t->seen&RAW_STEP_TRACE_CLONE_PUBLISHED)return 0;
    if(!raw_step_trace_order(t,RAW_STEP_TRACE_ACTUAL_BOUNDARY,t->actual_boundary_tick,tick))return 0;
    t->clone_published_tick=tick;t->seen|=RAW_STEP_TRACE_CLONE_PUBLISHED;return 1;
}
static inline int raw_step_trace_clone_acknowledged(RawStepTrace *t,uint64_t tick){
    if(!raw_step_trace_active(t))return 0;
    if(!raw_step_trace_order(t,RAW_STEP_TRACE_CLONE_PUBLISHED,t->clone_published_tick,tick))return 0;
    t->clone_acknowledged_tick=tick;t->seen|=RAW_STEP_TRACE_CLONE_ACKNOWLEDGED;return 1;
}
#endif
