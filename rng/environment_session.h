#ifndef CH_MANUAL_ENVIRONMENT_SESSION_H
#define CH_MANUAL_ENVIRONMENT_SESSION_H
#include "environment.h"
typedef struct {
    RawEnvironmentOps ops;
    RawEnvironmentState state;
    /* Permanent storage: never put a complete captured image on game stack. */
    RawEnvironmentPlan plan;
    uint64_t generation;
    uint32_t terminal_attempted,cleanup_attempted,last_status;
} RawEnvironmentSession;
/* Pure scheduling predicate. Idle original units require no environment
   ownership/SVC work; the existing mutation and cleanup guards still decide
   every operation in raw_environment_session_first. */
static inline int raw_environment_session_first_needed(const RawEnvironmentSession *s,const RawRuntime *r){
    if(!s||!r)return 0;
    if(s->state.poisoned)return 1;
    if(!s->state.initial_applied)return s->state.preparation_applied&&s->state.rtc_owned&&
        !s->cleanup_attempted&&(r->fault||!r->observed.script_final_prompt);
    if(r->encounter_started&&!s->terminal_attempted)return 1;
    return s->state.rtc_owned&&!s->cleanup_attempted&&
        (r->fault||(!r->encounter_started&&!r->source_bound&&!raw_runtime_source_recovery_pending(r))||
         (r->encounter_started&&((r->actual_seen&&r->release_seen)||r->plan_failed)));
}
int raw_environment_session_init(RawEnvironmentSession *,const RawEnvironmentOps *);
/* Preparation establishes typed settings only. It never binds a prediction
   origin. A subsequent player-released unit must pass full CPU admission. */
int raw_environment_session_prepare(RawEnvironmentSession *,const ChNativeContext *,uint32_t tls,uint64_t epoch);
/* Initial source is captured and canonical environment read back at the same
   first scheduler. Returned binding is conditional, never hardware-promoted. */
int raw_environment_session_source(RawEnvironmentSession *,const ChNativeContext *,uint32_t tls,
    uint64_t epoch,RawSourceCapture *,ManualSourceBinding *,uint64_t *generation);
/* A released, player-requested new origin preserves existing RTC ownership.
   Generation is provisional until coordinator result readback commits it. */
int raw_environment_session_rebase(RawEnvironmentSession *,const ChNativeContext *,uint32_t tls,
    uint64_t epoch,RawSourceCapture *,ManualSourceBinding *,uint64_t *generation);
/* First scheduler after real original marker; terminal only on exact original
   effective A receipt. Never calls provider or any controller function. */
int raw_environment_session_first(RawEnvironmentSession *,const ChNativeContext *,uint32_t tls,
    uint64_t epoch,const RawRuntime *);
#endif
