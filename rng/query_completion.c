#include "query_completion.h"
#include <string.h>
static int same_token(const ch_query *a,const ch_query *b){
    return a->query_id&&a->query_id==b->query_id&&a->source_counter==b->source_counter&&
        a->minimum_target_counter==b->minimum_target_counter&&a->scene_epoch==b->scene_epoch&&
        a->source_generation==b->source_generation;
}
static int job_bound(const RawRuntime *r,const RawJob *j){
    const ManualSourceBinding *a=&j->source,*b=&r->source;
    return r->waiting_valid&&j->waiting_valid&&j->boundary.counter-j->waiting_counter<=1u&&
        j->waiting_counter-a->origin_counter<=MANUAL_MODEL_MAX_PRESS&&
        j->waiting.bg==(r->source_bg+(j->waiting_counter-a->origin_counter)%3u)%3u&&
        r->source_bg<3u&&j->source_bg==r->source_bg&&a->abi==b->abi&&a->rng_add==b->rng_add&&a->rng_sub==b->rng_sub&&
        a->origin_counter==b->origin_counter&&a->source_epoch==b->source_epoch&&
        !memcmp(a->source_identity.bytes,b->source_identity.bytes,32)&&
        !memcmp(a->required_context_identity.bytes,b->required_context_identity.bytes,32)&&
        j->boundary.counter==j->token.source_counter&&j->boundary.source_epoch==a->source_epoch&&
        !memcmp(j->boundary.source_identity.bytes,a->source_identity.bytes,32)&&
        !memcmp(j->boundary.required_context_identity.bytes,a->required_context_identity.bytes,32);
}
int raw_query_complete(RawRuntime *r,const RawJob *j,const ManualPrediction *p){
    uint32_t status;
    if(!r||!j||!p||!same_token(&j->token,&r->controls.active_query))return RAW_QUERY_IGNORED;
    r->query_returned_counter=r->counter;r->query_result_target=p->target_counter;
    if(!job_bound(r,j)){
        r->query_failure_detail=RAW_QUERY_DETAIL_JOB_BINDING;
        raw_runtime_query_failed(r,&j->token,MANUAL_QUERY_STALE_SOURCE);return RAW_QUERY_REJECTED;
    }
    if(p->status!=MANUAL_QUERY_OK){
        status=p->abi==MANUAL_PREDICTION_ABI&&p->status<=7u?p->status:MANUAL_QUERY_INVALID;
        r->query_failure_detail=RAW_QUERY_DETAIL_WORKER_STATUS;
        raw_runtime_query_failed(r,&j->token,status);
        return r->refresh_needed?RAW_QUERY_REFRESH:RAW_QUERY_REJECTED;
    }
    /* A late return is timing, not corruption, only after the entire original
       candidate proof and current active-token binding have been checked. */
    if(!raw_runtime_candidate_bound(r,&j->token,p)){
        r->query_failure_detail=RAW_QUERY_DETAIL_RESULT_BINDING;
        raw_runtime_query_failed(r,&j->token,MANUAL_QUERY_INVALID);return RAW_QUERY_REJECTED;
    }
    if(r->counter-r->source.origin_counter>=MANUAL_MODEL_MAX_N-3u){
        r->query_failure_detail=RAW_QUERY_DETAIL_WINDOW_EXPIRED;
        raw_runtime_query_failed(r,&j->token,MANUAL_QUERY_OUTSIDE_DOMAIN);return RAW_QUERY_REFRESH;
    }
    if(raw_runtime_retry_near_candidate(r,&j->token,p,j->minimum_player_lead)){
        r->query_failure_detail=RAW_QUERY_DETAIL_NONE;return RAW_QUERY_RETRY;
    }
    if(raw_runtime_accept_candidate(r,&j->token,p)){
        r->query_failure_detail=RAW_QUERY_DETAIL_NONE;return RAW_QUERY_ACCEPTED;
    }
    if(raw_runtime_retry_missed_candidate(r,&j->token,p)){
        r->query_failure_detail=RAW_QUERY_DETAIL_NONE;return RAW_QUERY_RETRY;
    }
    r->query_failure_detail=RAW_QUERY_DETAIL_PLAN_ADMISSION;
    raw_runtime_query_failed(r,&j->token,MANUAL_QUERY_INVALID);return RAW_QUERY_REJECTED;
}
