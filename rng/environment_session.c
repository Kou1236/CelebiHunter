#include "environment_session.h"
#include <string.h>
int raw_environment_session_init(RawEnvironmentSession *s,const RawEnvironmentOps *o){
    if(!s||!o||!o->read.read_bytes||!o->write_data_byte||!o->write_rtc_word||!o->publish_rtc)return 0;
    memset(s,0,sizeof(*s));s->ops=*o;return 1;
}
int raw_environment_session_prepare(RawEnvironmentSession *s,const ChNativeContext *c,uint32_t tls,uint64_t epoch){
    int status;
    if(!s||s->state.preparation_applied||s->state.initial_applied||s->state.poisoned)return 0;
    status=raw_environment_prepare(&s->ops,c,tls,epoch,1,RAW_ENV_PREPARATION,183,&s->state,0,0,&s->plan);
    s->last_status=(uint32_t)status;if(status!=RAW_ENV_OK)return 0;
    status=raw_environment_apply(&s->ops,&s->state,&s->plan);s->last_status=(uint32_t)status;
    return status==RAW_ENV_OK;
}
int raw_environment_session_source(RawEnvironmentSession *s,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,RawSourceCapture *capture,ManualSourceBinding *binding,uint64_t *generation){
    int status;
    if(!s||!capture||!binding||!generation||s->state.initial_applied||s->state.poisoned)return 0;
    status=raw_environment_prepare(&s->ops,c,tls,epoch,1,RAW_ENV_INITIAL,183,&s->state,0,0,&s->plan);
    s->last_status=(uint32_t)status;if(status!=RAW_ENV_OK)return 0;
    status=raw_environment_apply(&s->ops,&s->state,&s->plan);s->last_status=(uint32_t)status;
    if(status!=RAW_ENV_OK)return 0;
    *capture=s->plan.before;
    /* A structural snapshot stays structural; do not rewrite its digest or
       flags to imply the before image contained the new environment. State's
       separate readback receipt establishes the required fixed context. */
    memset(binding,0,sizeof(*binding));binding->abi=MANUAL_PREDICTION_ABI;
    binding->rng_add=capture->rng_add;binding->rng_sub=capture->rng_sub;
    binding->origin_counter=s->state.origin_counter;binding->source_epoch=epoch;
    memcpy(binding->source_identity.bytes,s->state.source_identity,32);
    binding->required_context_identity=manual_model_certificate()->clock_derivation;
    *generation=++s->generation;return 1;
}
int raw_environment_session_first(RawEnvironmentSession *s,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,const RawRuntime *r){
    int status;
    if(!raw_environment_session_first_needed(s,r))return 0;
    if(s->state.poisoned){
        if(s->state.rtc_owned&&!s->cleanup_attempted){
            s->cleanup_attempted=1;s->last_status=(uint32_t)raw_environment_restore_rtc(&s->ops,&s->state);
        }
        return -1;
    }
    if(!s->state.initial_applied){
        /* A prepared environment can wait while the player holds the pause
           gate. It is never a prediction source until readback admission. */
        if(s->state.preparation_applied&&s->state.rtc_owned&&!s->cleanup_attempted&&
           (r->fault||!r->observed.script_final_prompt)){
            s->cleanup_attempted=1;s->last_status=(uint32_t)raw_environment_restore_rtc(&s->ops,&s->state);
            return s->last_status==RAW_ENV_OK?1:-1;
        }
        return 0;
    }
    if(r->encounter_started&&!s->terminal_attempted){
        s->terminal_attempted=1;
        if(r->fault||r->scan_open||r->engine_entries!=1||
           r->effective_press!=r->observed.counter){s->last_status=RAW_ENV_REJECTED;return -1;}
        if(r->plan_failed||!r->plan_active||r->encounter_candidate.status!=MANUAL_QUERY_OK){
            /* The player may start an ordinary encounter without a qualified
               forecast. Skip the terminal transaction and release only our
               RTC instruction; a missing forecast is not a failed source
               capture or write. Actual restoration failures still stop. */
            if(!r->actual_raw_press_seen||r->effective_press-r->actual_raw_press!=1u||
               (r->observed.guest_mask&1u)){
                s->last_status=RAW_ENV_REJECTED;return -1;
            }
            if(s->state.rtc_owned&&!s->cleanup_attempted){
                s->cleanup_attempted=1;
                status=raw_environment_restore_rtc(&s->ops,&s->state);
                s->last_status=(uint32_t)status;return status==RAW_ENV_OK?1:-1;
            }
            s->last_status=RAW_ENV_OK;return 0;
        }
        status=raw_environment_prepare(&s->ops,c,tls,epoch,1,RAW_ENV_TERMINAL,
            r->encounter_candidate.div_x,&s->state,&r->encounter_candidate,&r->input_plan,&s->plan);
        if(status==RAW_ENV_OK)status=raw_environment_apply(&s->ops,&s->state,&s->plan);
        s->last_status=(uint32_t)status;return status==RAW_ENV_OK?1:-1;
    }
    /* Keep the RTC condition through complete generation, as in the proven
       encounter. Release alone does not establish that DVs were generated.
       Actual result comes from a separate admitted original-result observer.
       Failed conditions withdraw that forecast before any early cleanup. */
    if(s->state.rtc_owned&&!s->cleanup_attempted&&
       (r->fault||(!r->encounter_started&&!r->source_bound&&!raw_runtime_source_recovery_pending(r))||
        (r->encounter_started&&((r->actual_seen&&r->release_seen)||r->plan_failed)))){
        s->cleanup_attempted=1;status=raw_environment_restore_rtc(&s->ops,&s->state);
        s->last_status=(uint32_t)status;return status==RAW_ENV_OK?1:-1;
    }
    return 0;
}
int raw_environment_session_rebase(RawEnvironmentSession *s,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,RawSourceCapture *capture,ManualSourceBinding *binding,uint64_t *generation){
    int status;
    if(!s||!capture||!binding||!generation||!s->state.initial_applied||
       s->state.terminal_applied||!s->state.preparation_applied||!s->state.rtc_owned||
       s->state.poisoned||s->terminal_attempted||s->cleanup_attempted||s->generation==UINT64_MAX)return 0;
    status=raw_environment_prepare(&s->ops,c,tls,epoch,1,RAW_ENV_REBASE,183,&s->state,0,0,&s->plan);
    s->last_status=(uint32_t)status;if(status!=RAW_ENV_OK)return 0;
    status=raw_environment_apply(&s->ops,&s->state,&s->plan);s->last_status=(uint32_t)status;
    if(status!=RAW_ENV_OK)return 0;
    *capture=s->plan.before;
    memset(binding,0,sizeof(*binding));binding->abi=MANUAL_PREDICTION_ABI;
    binding->rng_add=capture->rng_add;binding->rng_sub=capture->rng_sub;
    binding->origin_counter=s->state.origin_counter;binding->source_epoch=epoch;
    memcpy(binding->source_identity.bytes,s->state.source_identity,32);
    binding->required_context_identity=manual_model_certificate()->clock_derivation;
    *generation=s->generation+1u;return 1;
}
