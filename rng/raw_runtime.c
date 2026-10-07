#include "raw_runtime.h"
#include <string.h>
static void fault(RawRuntime *,uint32_t);
static int frontend_without_game_scan(const RawSample *s) {
    return s->frontend_valid&&s->sample_complete&&s->counter_read&&
        s->frontend_mode<13u&&(s->frontend_mode!=1u||s->frontend_scan_gate==2u);
}
static int ordinary(const RawSample *s) {
    return s->counter_read&&s->sample_complete&&s->ordinary_supported&&s->batch==1u&&
        s->host_phase==0u&&s->input_type==1u&&s->input_enable==0u&&
        (!s->frontend_valid||(s->frontend_mode==1u&&s->frontend_scan_gate!=2u));
}
static void invalidate_source(RawRuntime *r) {
    if(r->source_bound)r->refresh_needed=1;
    r->source_bound=0;r->waiting_valid=0;r->waiting_recheck=0;r->plan_active=0;r->plan_pending=0;
    ch_controller_request_query(&r->controls);
}
static MpSourceConditions plan_sample(const RawRuntime *r,const RawSample *s) {
    MpSourceConditions a;memset(&a,0,sizeof(a));a.abi=MP_ABI;a.counter=s->counter;
    a.engine_type=s->input_type;a.host_batch_count=s->batch;a.host_phase=s->host_phase;
    a.input_enable=s->input_enable;a.original_host_held=s->host_held;
    a.original_host_intersection=s->host_intersection;a.actual_applied_mask=s->guest_mask;
    a.source_epoch=s->scene_epoch;memcpy(a.source_identity.bytes,r->source.source_identity.bytes,32);
    return a;
}
static void plan_failed(RawRuntime *r) {
    r->plan_active=0;r->plan_pending=0;r->plan_failed=1;r->input_condition_matches=0;
    if(r->input_plan.error==MP_UNSUPPORTED_SOURCE||r->input_plan.error==MP_STALE_SOURCE||
       r->input_plan.error==MP_COUNTER_MISMATCH)invalidate_source(r);
    else ch_controller_request_query(&r->controls);
}
static int create_plan(RawRuntime *r,const RawSample *s,const ManualPrediction *p) {
    MpSourceConditions a=plan_sample(r,s);MpConditionalTarget t;
    memset(&t,0,sizeof(t));t.abi=MP_ABI;t.effective_a_counter=p->target_counter;
    t.expected_release_counter=p->expected_release_counter;t.source_epoch=p->source_epoch;
    memcpy(t.source_identity.bytes,p->source_identity.bytes,32);
    memcpy(t.required_context_identity.bytes,p->required_context_identity.bytes,32);
    memcpy(t.model_certificate_identity.bytes,manual_model_certificate()->profile_artifact.bytes,32);
    return mp_plan_create(&a,&t,mp_original_input_evidence(),&r->input_plan)==MP_OK;
}
static void cancel_pending_plan(RawRuntime *r) {
    r->plan_pending=0;r->plan_active=0;r->plan_failed=1;r->input_condition_matches=0;
    r->candidate.status=MANUAL_QUERY_INVALID;
    ch_controller_request_query(&r->controls);
}
void raw_runtime_sample_failed(RawRuntime *r) {
    r->counter_valid=0;r->unit_supported=0;fault(r,RAW_FAULT_READ);
    if(r->runtime==CH_RUNTIME_STEPPING)r->runtime=CH_RUNTIME_PAUSED;
}
static void fault(RawRuntime *r,uint32_t why) {
    if(!r->fault)r->fault=why;
    invalidate_source(r);
    if(r->controls.fault==CH_FAULT_NONE)r->controls.fault=CH_FAULT_RUNTIME_FAILED;
}
void raw_runtime_environment_failed(RawRuntime *r) {
    if(!r)return;
    fault(r,RAW_FAULT_ENVIRONMENT);r->plan_failed=1;r->input_condition_matches=0;
    r->encounter_candidate.status=MANUAL_QUERY_INVALID;
}
void raw_runtime_display_failed(RawRuntime *r) {
    if(!r)return;
    fault(r,RAW_FAULT_DISPLAY);r->display_pending=0;r->display_error=1;
    r->plan_failed=1;r->input_condition_matches=0;
    r->candidate.status=r->encounter_candidate.status=MANUAL_QUERY_INVALID;
}
static void log_event(RawRuntime *r,uint32_t kind,const RawSample *s) {
    RawObservation *o;
    if(r->observation_count==RAW_LOG_CAPACITY) {r->observation_overflow=1;return;}
    o=&r->observations[r->observation_count++];o->event=kind;o->scan_id=r->scan_id;
    o->counter=s->counter;o->previous_held=r->scan_before_held;o->held=s->host_held;
    o->intersection=s->host_intersection;o->mask=s->guest_mask;
    o->batch=s->batch;o->phase=s->host_phase;
}
void raw_runtime_init(RawRuntime *r,uint32_t held) {
    memset(r,0,sizeof(*r));ch_controller_init(&r->controls,held);r->runtime=CH_RUNTIME_RUNNING;r->source_bg=UINT32_MAX;
}
int raw_runtime_restart_checked(RawRuntime *r,uint64_t epoch,uint32_t held) {
    uint32_t visible,locked,next_request,next_query;
    if(!raw_runtime_restart_eligible(r,epoch))return 0;
    visible=r->controls.overlay_visible;locked=r->controls.interface_locked;
    next_request=r->controls.next_request_id;next_query=r->controls.next_query_id;
    raw_runtime_init(r,held);r->scene_epoch=epoch;r->restart_bootstrap_pending=1u;
    r->controls.overlay_visible=(uint8_t)visible;r->controls.interface_locked=(uint8_t)locked;
    r->controls.next_request_id=next_request;r->controls.next_query_id=next_query;
    return 1;
}
void raw_runtime_before_scan(RawRuntime *r,const RawSample *s) {
    uint32_t delta=s->counter-r->counter;
    if(frontend_without_game_scan(s)) {
        /* 1042F0 is also visited by the original VC menu. Its switch at
           1042FC legitimately omits the 104374 scan in these read-back modes.
           Retire only that unfinished scan, never a counter/reset fault. */
        invalidate_source(r);r->frontend_suspended=1;r->at_gate=0;
        r->counter_valid=0;r->unit_supported=0;r->scan_open=0;
        r->have_previous_scan=0;r->engine_entries=0;r->previous_unit_supported=0;
        r->latest=*s;r->observed=*s;
        if(r->runtime==CH_RUNTIME_STEPPING){r->runtime=CH_RUNTIME_PAUSED;fault(r,RAW_FAULT_UNIT);}
        return;
    }
    r->frontend_suspended=0;
    if(r->restart_bootstrap_pending&&!r->fault&&!r->controls.fault&&
       s->scene_epoch==r->scene_epoch&&!r->scan_open&&r->have_previous_scan&&
       r->previous_unit_supported&&r->restart_bootstrap_unit_supported&&ordinary(s)&&
       r->engine_entries==1u&&delta==1u){
        /* Reset has completed, but a loading visit is not yet a comparable
           game advance. Lock the baseline only after the first actual scan,
           its original engine entry and the next checked +1 game boundary. */
        r->restart_bootstrap_pending=0u;r->controls.counter_valid=0u;
    }
    if(r->plan_active&&r->have_previous_scan) {
        MpSourceConditions a=plan_sample(r,s);
        if(mp_plan_observe_counter(&r->input_plan,&a,r->scan_before_counter)!=MP_OK)
            plan_failed(r);
        else if(r->input_plan.state==MP_CONDITIONAL_INPUT_OBSERVED)r->plan_active=0;
    }
    r->at_gate=1;r->counter_valid=s->counter_read;r->unit_supported=(uint32_t)ordinary(s);
    if(r->source_bound&&(s->input_type!=r->source_input_type||s->host_phase!=r->source_phase||
        s->batch!=r->source_batch||!r->unit_supported))invalidate_source(r);
    if(r->source_bound&&!r->encounter_started&&!s->script_final_prompt)invalidate_source(r);
    if(r->scan_open)fault(r,RAW_FAULT_ORDER);
    if(!s->counter_read)fault(r,RAW_FAULT_READ);
    if(r->runtime==CH_RUNTIME_STEPPING) {
        /* Exactly the player's one requested unit. An unsupported trajectory
         * stops this owned step with a diagnostic, never retries another unit. */
        r->runtime=CH_RUNTIME_PAUSED;
        if(!r->fault&&r->previous_unit_supported&&r->unit_supported&&
            r->engine_entries==1u&&delta==1u&&s->counter==r->step_target&&
            s->scene_epoch==r->scene_epoch)
            r->completed_request=r->last_request;
        else fault(r,RAW_FAULT_UNIT);
    } else if(r->have_previous_scan&&
              (!r->previous_unit_supported||!r->unit_supported||r->engine_entries!=1u||delta!=1u)) {
        /* Ordinary running can leave the model domain. No pause or advance is
         * caused by this observation; only the conditional forecast expires. */
        invalidate_source(r);
    }
    if(s->scene_epoch!=r->scene_epoch)invalidate_source(r);
    if(s->counter_read)r->counter=s->counter;
    r->scene_epoch=s->scene_epoch;
    r->latest=*s;
    r->observed=*s;
    /* The A which reaches this text belongs to the original game. Arm only
       after the final wait is observed with actual input released; no extra
       search button, synthetic key or gameplay command is involved. */
    if(!r->restart_bootstrap_pending&&!r->search_started&&!r->encounter_started&&!r->fault&&r->counter_valid&&
       r->unit_supported&&s->script_final_prompt&&
       !((s->host_held|s->host_intersection)&3u)&&s->guest_mask==0xffffu){
        r->search_started=1;r->search_requested=1;
    }
    if(r->plan_pending) {
        uint32_t distance=r->raw_target-r->counter;
        /* A distant forecast needs no unbounded input log. Start the original
           observer only at an actual released boundary near its raw target;
           every subsequent scan, provider and counter receipt remains intact. */
        if(!r->source_bound||(!r->waiting_valid&&!r->waiting_recheck)||!r->controls.candidate_valid||
           r->fault||r->encounter_started||r->original_raw_a_seen||
           !distance||distance>=0x80000000u)
            cancel_pending_plan(r);
        else if(distance<=RAW_INPUT_PLAN_WINDOW) {
            if(create_plan(r,s,&r->candidate)) {
                r->plan_pending=0;r->plan_active=1;r->plan_failed=0;
            } else cancel_pending_plan(r);
        }
    }
    if(r->plan_active) {
        MpSourceConditions a=plan_sample(r,s);
        if(mp_plan_state(&r->input_plan,&a,0)!=MP_OK)plan_failed(r);
    }
}
int raw_runtime_command(RawRuntime *r,const ch_command *c) {
    if(!r||!c||!c->request_id||!r->at_gate||c->request_id==r->last_request||
        c->scene_epoch!=r->scene_epoch||c->expected_counter!=r->counter)return 0;
    if((r->fault||!r->counter_valid)&&c->kind!=CH_COMMAND_RESUME)return 0;
    switch(c->kind) {
    case CH_COMMAND_PAUSE_AT_BOUNDARY:
        if(r->runtime!=CH_RUNTIME_RUNNING)return 0;
        r->runtime=CH_RUNTIME_PAUSED;r->completed_request=c->request_id;break;
    case CH_COMMAND_STEP_ONE_BOUNDARY:
        if(r->runtime!=CH_RUNTIME_PAUSED||c->target_counter!=r->counter+1u)return 0;
        if(!r->unit_supported){fault(r,RAW_FAULT_UNIT);return 0;}
        r->step_by_physical_a=c->initiated_by_physical_a;
        r->step_target=c->target_counter;r->runtime=CH_RUNTIME_STEPPING;break;
    case CH_COMMAND_RESUME:
        if(r->runtime!=CH_RUNTIME_PAUSED)return 0;
        r->runtime=CH_RUNTIME_RUNNING;r->completed_request=c->request_id;break;
    default:return 0;
    }
    r->last_request=c->request_id;return 1;
}
void raw_runtime_poll_player(RawRuntime *r,uint32_t keys,const RawSample *s) {
    ch_input i;memset(&i,0,sizeof(i));
    if(r->frontend_suspended||r->restart_bootstrap_pending) {
        /* Menu keys are original front-end input, not a new game pause/step.
           Keep the last game counter for strict validation when it resumes.
           Checked Restart loading also waits for a complete new game unit. */
        r->controls.previous_keys=keys;r->controls.blocked_keys=keys;
        r->controls.pending_l_key=r->controls.pending_r_key=0;
        memset(&r->view,0,sizeof(r->view));r->view.overlay_visible=r->controls.overlay_visible;
        r->view.interface_locked=r->controls.interface_locked;r->view.fault=r->controls.fault;
        return;
    }
    i.physical_keys=keys;i.counter=r->counter;
    i.counter_valid=(uint8_t)r->counter_valid;i.at_native_boundary=(uint8_t)r->at_gate;
    i.runtime=r->runtime;i.scene_epoch=r->scene_epoch;i.source_generation=r->source_generation;
    i.completed_request_id=r->completed_request;
    i.scene_eligible=(uint8_t)(s->script_final_prompt&&r->unit_supported&&r->source_bound&&
        (r->waiting_valid||r->waiting_recheck)&&
        r->source.source_epoch==r->scene_epoch&&!r->encounter_started&&!r->fault&&
        !r->original_raw_a_seen);
    /* Rechecking keeps the already bound display target. New solver work
       still requires a current complete aligned state. */
    {uint32_t need=r->controls.need_query;
     if(r->waiting_recheck)r->controls.need_query=0;
     ch_controller_update(&r->controls,&i,&r->view);
     if(r->waiting_recheck&&need&&!r->controls.candidate_valid&&
        !r->controls.active_query.query_id)r->controls.need_query=need;}
    /* Controller-only failures also withdraw the bound source and expose the
       actual pause/counter reason. Never clear that fault to retry a query. */
    if(r->controls.fault&&!r->fault)fault(r,RAW_FAULT_ORDER);
    /* A is the player's request to resume original gameplay with real HID.
       Only an explicit L release requests a single unit. Do not rewrite A's
       RESUME command into a step or supply/clear any guest input. */
    if(r->view.command.kind&&!raw_runtime_command(r,&r->view.command))fault(r,RAW_FAULT_COMMAND);
    /* Worker queries run independently of the player's pause state. */
    if(r->query_status==7u&&r->source_bound&&!r->waiting_recheck&&!r->fault&&
       !r->encounter_started&&!r->controls.active_query.query_id)
        ch_controller_request_query(&r->controls);
}
void raw_runtime_begin_scan(RawRuntime *r,const RawSample *s) {
    if(r->frontend_suspended&&frontend_without_game_scan(s))return;
    if(!r->at_gate||r->runtime==CH_RUNTIME_PAUSED||r->scan_open){fault(r,RAW_FAULT_ORDER);return;}
    r->scan_id++;r->scan_before_counter=s->counter;r->scan_before_held=s->host_held;
    r->engine_entries=0;r->previous_unit_supported=r->unit_supported;r->have_previous_scan=1;
    if(r->restart_bootstrap_pending)r->restart_bootstrap_unit_supported=(uint32_t)ordinary(s);
    r->scan_open=1;r->at_gate=0;log_event(r,RAW_EVENT_BEFORE_SCAN,s);
}
void raw_runtime_after_scan(RawRuntime *r,const RawSample *s) {
    if(r->restart_bootstrap_pending&&!ordinary(s))r->restart_bootstrap_unit_supported=0u;
    if(!r->scan_open||!s->counter_read||s->counter!=r->scan_before_counter) {
        fault(r,RAW_FAULT_ORDER);return;
    }
    if((s->host_intersection&1u)!=((r->scan_before_held&s->host_held)&1u))
        fault(r,RAW_FAULT_FILTER);
    r->observed=*s;if(s->host_held&1u)r->original_raw_a_seen=1;
    if((s->host_held&1u)&&!(r->scan_before_held&1u)&&!r->encounter_started) {
        r->actual_raw_press_seen=1;r->actual_raw_press=s->counter;
        r->actual_raw_release_seen=0;
        /* Record the original press before a distant plan cancellation changes
           candidate.status. It must never relabel a later environment fault. */
        r->encounter_forecast_reason=RAW_ENCOUNTER_FORECAST_UNAVAILABLE;
        if(r->candidate.abi==MANUAL_PREDICTION_ABI&&r->candidate.status==MANUAL_QUERY_OK&&
           r->candidate.source_epoch==s->scene_epoch&&s->counter!=r->raw_target)
            r->encounter_forecast_reason=RAW_ENCOUNTER_FORECAST_OFF_TARGET;
    } else if(!(s->host_held&1u)&&(r->scan_before_held&1u)&&r->actual_raw_press_seen&&
              !r->actual_raw_release_seen) {
        r->actual_raw_release_seen=1;r->actual_raw_release=s->counter;
    }
    if(r->waiting_recheck&&(s->host_held&1u)) {
        /* Actual HID always reaches the game. A press made without a checked
           waiting state cannot later acquire the retained forecast. */
        r->plan_active=0;r->plan_pending=0;r->plan_failed=1;r->input_condition_matches=0;
        ch_controller_request_query(&r->controls);
    }
    if(!ordinary(s))invalidate_source(r);
    if(r->plan_pending&&((s->host_held|s->host_intersection)&1u))cancel_pending_plan(r);
    if(r->plan_active) {
        MpSourceConditions a=plan_sample(r,s);
        if(mp_plan_observe_scan(&r->input_plan,&a,r->scan_before_held,
            s->host_held,s->host_intersection)!=MP_OK)plan_failed(r);
    }
    log_event(r,RAW_EVENT_AFTER_SCAN,s);r->scan_open=0;
}
void raw_runtime_engine_entry(RawRuntime *r,const RawSample *s) {
    uint32_t held=(s->guest_mask&1u)?0u:1u;
    if(r->restart_bootstrap_pending&&(!ordinary(s)||s->counter!=r->scan_before_counter))
        r->restart_bootstrap_unit_supported=0u;
    if(r->scan_open){fault(r,RAW_FAULT_ORDER);return;}
    r->engine_entries++;log_event(r,RAW_EVENT_ENGINE_ENTRY,s);
    r->observed=*s;
    if(!ordinary(s))invalidate_source(r);
    if(r->plan_pending&&((s->guest_mask&255u)!=255u||s->guest_mask==255u))
        cancel_pending_plan(r);
    if(r->plan_active) {
        MpSourceConditions a=plan_sample(r,s);
        if(mp_plan_observe_applied_mask(&r->input_plan,&a,s->guest_mask)!=MP_OK)
            plan_failed(r);
    }
    if(!r->source_bound&&!r->encounter_started){
        /* Prior dialogue A is real input, but cannot create an encounter
           receipt before a released final source has actually been bound. */
        r->guest_a_previous=held;r->guest_a_initialized=0;return;
    }
    if(!r->guest_a_initialized) {
        if(!held&&s->script_final_prompt)r->guest_a_initialized=1;
        r->guest_a_previous=held;return;
    }
    if(held&&!r->guest_a_previous&&!r->encounter_started) {
        r->encounter_started=1;r->effective_press=s->counter;
        r->actual_press_mask=s->guest_mask;
        memset(&r->encounter_candidate,0,sizeof(r->encounter_candidate));
        r->encounter_candidate.abi=MANUAL_PREDICTION_ABI;
        r->encounter_candidate.status=MANUAL_QUERY_INVALID;
        /* A late UI update may already have marked the raw target passed. The
         * immutable forecast still binds this original effective sample only
         * when its saved raw-press observation and effective target both match.
         * The input-plan observer supplies that raw condition separately. */
        if(r->waiting_valid&&!r->waiting_recheck&&r->plan_active&&!r->plan_failed&&r->input_plan.effective_a_seen&&
            r->input_plan.observed_effective_a_counter==s->counter&&
            r->candidate.abi==MANUAL_PREDICTION_ABI&&r->candidate.status==MANUAL_QUERY_OK&&
            r->candidate.source_epoch==s->scene_epoch&&r->effective_target==s->counter)
            r->encounter_candidate=r->candidate;
        if(r->encounter_candidate.status==MANUAL_QUERY_OK)
            r->encounter_forecast_reason=RAW_ENCOUNTER_FORECAST_NONE;
        else {
            if(r->encounter_forecast_reason!=RAW_ENCOUNTER_FORECAST_OFF_TARGET)
                r->encounter_forecast_reason=RAW_ENCOUNTER_FORECAST_UNAVAILABLE;
            r->plan_active=0;r->plan_pending=0;r->plan_failed=1;
            r->input_condition_matches=0;
        }
        r->source_bound=0;
    } else if(!held&&r->guest_a_previous&&r->encounter_started&&!r->release_seen) {
        r->effective_release=s->counter;r->release_seen=1;
        r->actual_release_mask=s->guest_mask;
        r->input_condition_matches=(uint32_t)(r->plan_active&&!r->plan_failed&&
            r->input_plan.state==MP_CONDITIONAL_INPUT_OBSERVED&&
            r->encounter_candidate.status==MANUAL_QUERY_OK&&
            s->counter==r->encounter_candidate.expected_release_counter);
    }
    r->guest_a_previous=held;
}
int raw_runtime_bind_source(RawRuntime *r,const ManualSourceBinding *s,uint64_t generation) {
    if(!r||r->restart_bootstrap_pending||!s||s->abi!=MANUAL_PREDICTION_ABI||!s->source_epoch||!generation||
        s->rng_add>255||s->rng_sub>255||r->encounter_started||r->fault||r->controls.fault||
        !r->observed.script_final_prompt||
        (r->source_generation&&(r->original_raw_a_seen||r->actual_raw_press_seen))||
        !ordinary(&r->observed)||((r->observed.host_held|r->observed.host_intersection)&1u)||
        (r->observed.guest_mask&255u)!=255u||r->observed.guest_mask==255u||
        s->source_epoch!=r->observed.scene_epoch||s->origin_counter!=r->observed.counter||
        (r->source_generation&&generation<=r->source_generation))return 0;
    r->source=*s;r->source_generation=generation;r->source_bound=1;
    r->search_started=1;r->search_requested=0;
    r->source_input_type=r->observed.input_type;r->source_phase=r->observed.host_phase;
    r->source_batch=r->observed.batch;
    r->plan_active=0;r->plan_failed=0;r->plan_pending=0;
    r->original_raw_a_seen=0;r->query_status=MANUAL_QUERY_OK;r->refresh_needed=0;
    r->actual_raw_press_seen=0;r->actual_raw_release_seen=0;
    r->actual_raw_press=0;r->actual_raw_release=0;
    r->encounter_forecast_reason=RAW_ENCOUNTER_FORECAST_NONE;
    r->actual_press_mask=0;r->actual_release_mask=0;
    r->guest_a_initialized=1;r->guest_a_previous=0;
    memset(&r->candidate,0,sizeof(r->candidate));ch_controller_request_query(&r->controls);return 1;
}
int raw_runtime_query_failed(RawRuntime *r,const ch_query *q,uint32_t status){
    if(!r||!q||status==MANUAL_QUERY_OK||status>7u||
       !ch_controller_query_failed(&r->controls,q))return 0;
    r->query_status=status;
    r->refresh_needed=(uint32_t)(status==MANUAL_QUERY_OUTSIDE_DOMAIN||
        status==MANUAL_QUERY_NO_FUTURE_CANDIDATE);
    /* A failed future query never controls gameplay or discards an owned
       environment source before its checked new-window readback. */
    return 1;
}
int raw_runtime_candidate_bound(const RawRuntime *r,const ch_query *q,const ManualPrediction *p) {
    ManualCurrentBoundary queried;
    uint32_t raw_delta;
    if(!r||!q||!p||p->abi!=MANUAL_PREDICTION_ABI||p->status!=MANUAL_QUERY_OK||
        !r->source_bound||!r->unit_supported||r->fault||
        r->encounter_started||r->original_raw_a_seen||
        q->query_id==0||q->query_id!=r->controls.active_query.query_id||
        q->source_counter!=r->controls.active_query.source_counter||
        q->minimum_target_counter!=r->controls.active_query.minimum_target_counter||
        q->scene_epoch!=r->controls.active_query.scene_epoch||
        q->source_generation!=r->controls.active_query.source_generation||
        !r->controls.counter_valid||!r->controls.scene_eligible||r->controls.fault||
        p->source_epoch!=r->scene_epoch||p->source_epoch!=r->source.source_epoch||
        p->origin_counter!=r->source.origin_counter||p->queried_at_counter!=q->source_counter||
        p->input_contract!=manual_model_certificate()->input_contract||
        p->model_n>MANUAL_MODEL_MAX_N||p->div_x>255||p->branch>2||
        p->expected_wait_add>255||p->expected_wait_sub>255||
        (p->predicted_dv&0xfffu)!=0xaaau||!(p->predicted_dv&0x2000u)||
        p->release_relative!=p->press_relative+3u||
        p->expected_release_counter!=p->target_counter+3u||
        p->manual_input_validated||memcmp(p->source_identity.bytes,r->source.source_identity.bytes,32)||
        memcmp(p->required_context_identity.bytes,r->source.required_context_identity.bytes,32))return 0;
    memset(&queried,0,sizeof(queried));queried.counter=q->source_counter;
    queried.source_epoch=r->scene_epoch;queried.source_identity=r->source.source_identity;
    queried.required_context_identity=r->source.required_context_identity;
    raw_delta=p->target_counter-1u-q->source_counter;
    return manual_prediction_state(p,&queried,0)==MANUAL_QUERY_OK&&raw_delta&&raw_delta<0x80000000u&&
        p->remaining_advances==p->target_counter-q->source_counter&&
        p->target_counter-1u-q->minimum_target_counter<0x80000000u;
}
int raw_runtime_retry_near_candidate(RawRuntime *r,const ch_query *q,const ManualPrediction *p,uint32_t lead){
    uint32_t delta;
    if(!r||!lead||lead>MANUAL_MODEL_MAX_N-3u||r->runtime!=CH_RUNTIME_RUNNING||
       !raw_runtime_candidate_bound(r,q,p)||r->counter-r->source.origin_counter>=MANUAL_MODEL_MAX_N-3u)return 0;
    delta=p->target_counter-1u-r->counter;
    if(!delta||delta>=lead)return 0;
    ch_controller_request_query(&r->controls);return 1;
}
int raw_runtime_retry_missed_candidate(RawRuntime *r,const ch_query *q,const ManualPrediction *p) {
    uint32_t delta;
    if(!raw_runtime_candidate_bound(r,q,p)||r->counter-r->source.origin_counter>=MANUAL_MODEL_MAX_N-3u)return 0;
    delta=p->target_counter-1u-r->counter;
    if(delta&&delta<0x80000000u)return 0;
    /* A complete, correctly bound worker result can arrive after its physical
       press boundary. Request a future result without changing player state.
       Invalid metadata and expired query tokens never enter this retry path. */
    ch_controller_request_query(&r->controls);return 1;
}
int raw_runtime_accept_candidate(RawRuntime *r,const ch_query *q,const ManualPrediction *p) {
    uint32_t raw,delta;
    ManualCurrentBoundary current;
    if(!raw_runtime_candidate_bound(r,q,p)||!r->waiting_valid||r->waiting_recheck)return 0;
    memset(&current,0,sizeof(current));current.counter=r->counter;current.source_epoch=r->scene_epoch;
    current.source_identity=r->source.source_identity;current.required_context_identity=r->source.required_context_identity;
    if(manual_prediction_state(p,&current,0)!=MANUAL_QUERY_OK)return 0;
    raw=p->target_counter-1u;delta=raw-r->counter;
    if(!delta||delta>=0x80000000u)return 0;
    if(delta<=RAW_INPUT_PLAN_WINDOW&&!create_plan(r,&r->latest,p))return 0;
    if(!ch_controller_accept_candidate(&r->controls,q,raw,(uint16_t)p->predicted_dv))return 0;
    /* A UI result does not erase the completed original scan receipt. */
    r->plan_active=(uint32_t)(delta<=RAW_INPUT_PLAN_WINDOW);r->plan_pending=!r->plan_active;
    r->plan_failed=0;
    if(r->plan_pending)memset(&r->input_plan,0,sizeof(r->input_plan));
    r->query_status=MANUAL_QUERY_OK;r->refresh_needed=0;
    r->candidate=*p;r->raw_target=raw;r->effective_target=p->target_counter;
    r->release_target=p->expected_release_counter;return 1;
}
void raw_runtime_actual_dv(RawRuntime *r,uint16_t actual) {
    if(!r->encounter_started||r->actual_seen)return;
    r->actual_seen=1;r->actual_dv=actual;
}
uint32_t raw_original_target(uint32_t lr) {
    switch(lr) {
    case 0x10ed60u:return 0x18c670u;
    case 0x1858e8u:return 0x1928a8u;
    case 0x1042f4u:return 0x110fa0u;
    case 0x104378u:return 0x10a430u;
    case 0x1a82e0u:return 0x1aabf8u;
    case 0x1a8340u:return 0x1aae60u;
    case 0x145480u:return 0x14aa24u;
    default:return 0;
    }
}
