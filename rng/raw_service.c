#include "raw_service.h"
#include "query_completion.h"
#include "restart_lifecycle.h"
#include <string.h>
int raw_service_init(RawService *s,const RawServiceOps *o) {
    uint32_t keys;
    if(!s||!o||!o->sample||!o->physical_keys||!o->draw||!o->wait_poll||
        !o->submit_copy||!o->receive_copy||!o->physical_keys(o->user,&keys))return 0;
    memset(s,0,sizeof(*s));s->ops=*o;raw_runtime_init(&s->runtime,keys);s->initialized=1;return 1;
}
int raw_job_solve(const RawJob *j,Sol61Workspace *w,ManualPrediction *p) {
    RawQueryCache cache;memset(&cache,0,sizeof(cache));
    return raw_job_solve_cached(j,w,&cache,p);
}
int raw_job_solve_cached(const RawJob *j,Sol61Workspace *w,RawQueryCache *cache,ManualPrediction *p){
    CQQueryResult result;uint32_t elapsed,seed_elapsed;int status;(void)w;
    if(!p)return MANUAL_QUERY_INVALID;
    memset(p,0,sizeof(*p));p->abi=MANUAL_PREDICTION_ABI;p->status=MANUAL_QUERY_INVALID;
    if(!j||!cache||!j->waiting_valid||j->source_bg>2u||
       j->source.abi!=MANUAL_PREDICTION_ABI||!j->source.source_epoch||
       j->boundary.counter!=j->token.source_counter||
       j->boundary.source_epoch!=j->source.source_epoch||
       memcmp(j->boundary.source_identity.bytes,j->source.source_identity.bytes,32)||
       memcmp(j->boundary.required_context_identity.bytes,j->source.required_context_identity.bytes,32)||
       memcmp(j->source.required_context_identity.bytes,manual_model_certificate()->clock_derivation.bytes,32))return p->status;
    elapsed=j->boundary.counter-j->waiting_counter;
    seed_elapsed=j->waiting_counter-j->source.origin_counter;
    if(elapsed>1u||seed_elapsed>MANUAL_MODEL_MAX_PRESS||
       j->waiting.bg!=(j->source_bg+seed_elapsed%3u)%3u)return p->status;
    if(!cache->prepared){
        if(!cq_terminal_prepare(&cache->terminal))return p->status;
        cache->prepared=1u;
    }
    status=cq_find_prepared(&j->waiting,elapsed,j->minimum_player_lead,&cache->terminal,&result);
    if(status!=CQ_QUERY_OK)return p->status=status==CQ_QUERY_NO_FUTURE_SHINY?
        MANUAL_QUERY_NO_FUTURE_CANDIDATE:MANUAL_QUERY_UNSUPPORTED_CERTIFICATE;
    p->target_counter=j->waiting_counter+result.effective_n;
    p->press_relative=p->target_counter-j->source.origin_counter;
    if(!result.found||p->press_relative>MANUAL_MODEL_MAX_PRESS||
       p->target_counter-1u-j->token.minimum_target_counter>=0x80000000u)return p->status;
    p->status=MANUAL_QUERY_OK;p->evidence=MANUAL_EVIDENCE_CONDITIONAL;
    p->input_contract=MANUAL_INPUT_ORIGINAL_LEAF_HOLD_THREE_CALLS;
    p->source_epoch=j->source.source_epoch;p->source_identity=j->source.source_identity;
    p->required_context_identity=j->source.required_context_identity;
    p->origin_counter=j->source.origin_counter;p->queried_at_counter=j->boundary.counter;
    p->expected_release_counter=p->target_counter+3u;p->release_relative=p->press_relative+3u;
    p->remaining_advances=p->target_counter-j->boundary.counter;p->model_n=p->press_relative+2u;
    p->div_x=result.div_x;p->branch=result.branch;p->predicted_dv=result.predicted_dv;
    p->expected_wait_add=result.expected_wait_add;p->expected_wait_sub=result.expected_wait_sub;
    p->target_pair_count=result.checked_candidates;
    return p->status;
}
static void ui(RawService *s,const RawSample *sample) {
    RawRuntime *r=&s->runtime;RawJob job;ManualPrediction p;RawHud hud;uint32_t keys;
    if(s->ops.physical_keys(s->ops.user,&keys))raw_runtime_poll_player(r,keys,sample);
    if(!r->waiting_recheck&&s->ops.receive_copy(s->ops.user,&job,&p)) {
        (void)raw_query_complete(r,&job,&p);
    }
    if(r->view.query.query_id) {
        memset(&job,0,sizeof(job));job.token=r->view.query;job.source=r->source;
        job.boundary.counter=job.token.source_counter;job.boundary.source_epoch=r->scene_epoch;
        job.boundary.source_identity=r->source.source_identity;
        job.boundary.required_context_identity=r->source.required_context_identity;
        job.cache_build_allowed=1u;
        job.minimum_player_lead=RAW_MINIMUM_PLAYER_LEAD;job.source_bg=r->source_bg;
        job.waiting=r->waiting;job.waiting_counter=r->waiting_counter;job.waiting_valid=r->waiting_valid;
        r->query_submitted_counter=r->counter;
        if(!s->ops.submit_copy(s->ops.user,&job)){
            r->query_failure_detail=RAW_QUERY_DETAIL_SUBMISSION;
            raw_runtime_query_failed(r,&job.token,MANUAL_QUERY_INVALID);
        }
        /* Edge request belongs to this copied submission, not every UI tick. */
        memset(&r->view.query,0,sizeof(r->view.query));
    }
    raw_runtime_hud(r,&hud);s->ops.draw(s->ops.user,&hud);
}
uint32_t raw_service_route(RawService *s,const ChNativeContext *c) {
    RawSample a;RawRuntime *r;uint32_t chain,good;
    if(!c)return 0;
    chain=raw_original_target(c->lr);
    if(!s||!s->initialized||!chain)return chain;
    if(c->lr==RAW_RESTART_RECEIPT_LR||c->lr==RAW_MENU_RESET_RECEIPT_LR){
        if(s->ops.restart_session&&s->ops.restart_session(s->ops.user,c,&s->runtime)>0)
            s->pending_marker=0u;
        return chain;
    }
    if(s->restart_pending){
        int restarted;
        /* The native worker never edits runtime. Other hook routes preserve
           their original calls while its receipt awaits the main boundary. */
        if(c->lr!=0x1042f4u)return chain;
        restarted=s->ops.restart_session?
            s->ops.restart_session(s->ops.user,c,&s->runtime):0;
        if(restarted>0)s->pending_marker=0u;
        else raw_runtime_environment_failed(&s->runtime);
        if(restarted<=0)return chain;
    }
    if(c->lr==0x145480u){if(s->ops.present)s->ops.present(s->ops.user,c);return chain;}
    /* This original call sits inside the interpreter's repeated scheduler
       loop. Only the first call following the applied-input marker has work.
       Later calls must neither sample the process nor change observations. */
    if(c->lr==0x1a8340u&&!s->pending_marker)return chain;
    r=&s->runtime;
    memset(&a,0,sizeof(a));good=(uint32_t)s->ops.sample(s->ops.user,&a);
    if(c->lr==0x1042f4u) {
        if(good)raw_runtime_before_scan(r,&a);
        else {r->at_gate=1;raw_runtime_sample_failed(r);}
        if(r->frontend_suspended)s->pending_marker=0u;
        else if(s->ops.completed_unit)s->ops.completed_unit(s->ops.user,c,r,good);
        ui(s,&a);
        /* Only a player command may enter this loop. Jobs and display keep
         * running; original physical refresh remains on the caller's stack. */
        while(r->runtime==CH_RUNTIME_PAUSED||s->display_owned) {
            if(s->ops.paused_display&&!s->display_abandoned) {
                int display;
                if(r->runtime==CH_RUNTIME_STEPPING&&!r->step_by_physical_a){
                    /* L owns one original unit. Keep leases and the currently
                       shown image until its actual original presentation. */
                    display=s->ops.paused_display(s->ops.user,c,r,4);
                    r->display_pending=(uint32_t)(display!=1);
                    r->display_error=(uint32_t)(display<0);
                    break;
                }
                if(r->runtime==CH_RUNTIME_PAUSED) {
                    s->display_exit_polls=0;
                    s->display_owned=1;
                    display=s->ops.paused_display(s->ops.user,c,r,1);
                } else {
                    display=s->ops.paused_display(s->ops.user,c,r,2);
                    if(display==1)s->display_owned=0;
                    else if(++s->display_exit_polls>=RAW_DISPLAY_EXIT_POLLS){
                        /* Runtime already changed only by a player's command.
                           Stop a failed display from vetoing that command.
                           Keep every possibly scanned buffer permanently. */
                        (void)s->ops.paused_display(s->ops.user,c,r,3);
                        s->display_owned=0;s->display_abandoned=1;
                        raw_runtime_display_failed(r);break;
                    }
                }
                r->display_pending=(uint32_t)(display!=1);r->display_error=(uint32_t)(display<0);
                if(r->runtime!=CH_RUNTIME_PAUSED&&!s->display_owned)break;
            }
            s->ops.wait_poll(s->ops.user);
            ui(s,&a);
        }
        if(good)raw_runtime_begin_scan(r,&a);
    } else if(c->lr==0x104378u) {
        if(good)raw_runtime_after_scan(r,&a);
        else raw_runtime_sample_failed(r);
    } else if(c->lr==0x1a82e0u) {
        s->pending_marker=1;
        if(good)raw_runtime_engine_entry(r,&a);
        else raw_runtime_sample_failed(r);
    } else if(c->lr==0x1a8340u&&s->pending_marker) {
        ManualSourceBinding source;uint64_t generation;
        s->pending_marker=0;
        if(good&&s->ops.first_scheduler&&s->ops.first_scheduler(s->ops.user,c,r)<0)
            raw_runtime_environment_failed(r);
        if(good&&!r->restart_bootstrap_pending&&(!r->source_bound||(r->refresh_needed&&(r->search_started||r->runtime==CH_RUNTIME_STEPPING)&&
           !r->step_by_physical_a&&!r->controls.candidate_valid&&!r->plan_active&&
           !r->original_raw_a_seen&&!r->actual_raw_press_seen))&&!r->encounter_started&&!r->fault&&
            a.script_final_prompt&&s->ops.read_new_source) {
            int admitted=s->ops.read_new_source(s->ops.user,c,&source,&generation);
            if(admitted<0)raw_runtime_environment_failed(r);
            else if(admitted==1&&!raw_runtime_bind_source(r,&source,generation))
                raw_runtime_environment_failed(r);
        }
    }
    return chain;
}
RawService ch_raw_service;
/* The native bridge can bypass C on subsequent scheduler iterations. This
   is an actual pointer object, not a linker numeric/address substitution. */
uint32_t * const ch_raw_scheduler_pending=&ch_raw_service.pending_marker;
static uint32_t callback(const ChNativeContext *c) {return raw_service_route(&ch_raw_service,c);}
ChNativeCallback ch_raw_callback=callback;
int ch_raw_bind(const RawServiceOps *o) {return raw_service_init(&ch_raw_service,o);}
