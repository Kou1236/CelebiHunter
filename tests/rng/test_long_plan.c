#include "raw_service.h"
#include "environment_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static RawService service;
static RawEnvironmentSession environment;
static RawSample actual;
static RawQueryCache cache;
static uint32_t physical,checks,terminal_receipts,environment_calls,submissions;
static uint32_t case_arm,case_terminal_log,case_complete_log;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"failed line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)

/* Exact recent-five-event predicate retained by production environment.c.
 * Memory admission/apply is mocked here; the original service/session gate,
 * query, physical filter and input-plan observers are all linked unchanged. */
static int receipt_log(const MpInputPlan *p,uint32_t counter){
    const MpObservation *raw,*raw_mask,*tail,*scan,*mask;uint32_t n=p->log_count;
    if(n<5||n>MP_LOG_CAPACITY)return 0;
    raw=&p->log[n-5];raw_mask=&p->log[n-4];tail=&p->log[n-3];scan=&p->log[n-2];mask=&p->log[n-1];
    return raw->kind==MP_LOG_SCAN&&raw->counter==counter-1&&!(raw->previous_held&1)&&
        (raw->current_held&1)&&!(raw->intersection&1)&&raw->intersection==(raw->previous_held&raw->current_held)&&
        raw_mask->kind==MP_LOG_PROVIDER&&raw_mask->counter==counter-1&&raw_mask->provider_mask==0xffff&&
        raw_mask->observation_point==MP_POINT_APPLIED_MASK_MARKER_1A82DC&&
        tail->kind==MP_LOG_COUNTER&&tail->counter==counter-1&&tail->next_counter==counter&&
        scan->kind==MP_LOG_SCAN&&scan->counter==counter&&scan->previous_held==raw->current_held&&
        scan->current_held==p->last_held&&scan->intersection==p->last_intersection&&
        scan->intersection==(scan->previous_held&scan->current_held)&&
        mask->kind==MP_LOG_PROVIDER&&mask->counter==counter&&mask->provider_mask==0xfffe&&
        mask->observation_point==MP_POINT_APPLIED_MASK_MARKER_1A82DC;
}
int raw_environment_prepare(const RawEnvironmentOps *o,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,uint32_t first,uint32_t stage,uint32_t x,const RawEnvironmentState *s,
    const ManualPrediction *p,const MpInputPlan *i,RawEnvironmentPlan *out){
    MpInputPlan altered;(void)o;(void)c;(void)tls;(void)s;environment_calls++;
    CHECK(epoch==77u&&first==1u&&stage==RAW_ENV_TERMINAL);
    CHECK(p&&p->status==MANUAL_QUERY_OK&&i&&i->effective_a_seen&&!i->error);
    CHECK(x==p->div_x&&receipt_log(i,p->target_counter));
    CHECK(i->origin_counter!=p->origin_counter);
    altered=*i;altered.log[altered.log_count-3u].next_counter++;
    CHECK(!receipt_log(&altered,p->target_counter));
    altered=*i;altered.log[altered.log_count-4u].provider_mask=0x7fffu;
    CHECK(!receipt_log(&altered,p->target_counter));
    case_terminal_log=i->log_count;terminal_receipts++;
    memset(out,0,sizeof(*out));out->stage=stage;return RAW_ENV_OK;
}
int raw_environment_apply(const RawEnvironmentOps *o,RawEnvironmentState *s,const RawEnvironmentPlan *p){
    (void)o;CHECK(p->stage==RAW_ENV_TERMINAL);environment_calls++;s->terminal_applied=1u;return RAW_ENV_OK;
}
int raw_environment_restore_rtc(const RawEnvironmentOps *o,RawEnvironmentState *s){
    (void)o;environment_calls++;s->rtc_owned=0u;return RAW_ENV_OK;
}
static int sample(void *u,RawSample *s){(void)u;*s=actual;return 1;}
static int keys(void *u,uint32_t *k){(void)u;*k=physical;return 1;}
static void draw(void *u,const RawHud *h){(void)u;CHECK(h->count<=7u);}
static void wait_poll(void *u){(void)u;CHECK(0);}
static int submit(void *u,const RawJob *j){(void)u;(void)j;submissions++;return 1;}
static int receive(void *u,RawJob *j,ManualPrediction *p){(void)u;(void)j;(void)p;return 0;}
static int first_scheduler(void *u,const ChNativeContext *c,const RawRuntime *r){
    ReleasedDivClock21 checked;(void)u;
    CHECK(released_div_boundary21(&r->waiting.clock,0u,&checked));
    return raw_environment_session_first(&environment,c,0u,77u,r);
}
static void route(uint32_t lr){
    ChNativeContext c={0};c.lr=lr;
    CHECK(raw_service_route(&service,&c)==raw_original_target(lr));
}
static void begin_unit(uint32_t held){
    RawRuntime *r=&service.runtime;physical=held;route(0x1042f4u);
    CHECK(r->runtime==CH_RUNTIME_RUNNING&&!r->view.command.kind);
    if(r->plan_active&&!case_arm)case_arm=r->input_plan.origin_counter;
}
static void finish_unit(uint32_t held){
    RawRuntime *r=&service.runtime;uint32_t old=actual.host_held;
    actual.host_held=held;actual.host_down=held&~old;actual.host_up=old&~held;
    actual.host_intersection=old&held;route(0x104378u);
    actual.guest_mask=(~actual.host_intersection)&0xffffu;route(0x1a82e0u);route(0x1a8340u);
    if(r->source_bound&&!r->encounter_started){
        CHECK(r->waiting_counter==actual.counter);
        CHECK(cq_waiting_next(&r->waiting));r->waiting_counter=actual.counter+1u;
    }
    actual.counter++;
}
static void unit(uint32_t held){begin_unit(held);finish_unit(held);}
static void setup(uint32_t origin,int far,uint32_t upper){
    RawServiceOps ops={0};ManualSourceBinding source={0};RawRuntime *r=&service.runtime;
    physical=upper;case_arm=case_terminal_log=case_complete_log=0u;
    ops.sample=sample;ops.physical_keys=keys;ops.draw=draw;ops.wait_poll=wait_poll;
    ops.submit_copy=submit;ops.receive_copy=receive;ops.first_scheduler=first_scheduler;
    CHECK(raw_service_init(&service,&ops));memset(&environment,0,sizeof(environment));
    memset(&actual,0,sizeof(actual));actual.counter=origin;
    actual.counter_read=actual.sample_complete=actual.ordinary_supported=1u;
    actual.batch=actual.input_type=actual.script_final_prompt=1u;
    actual.host_held=actual.host_intersection=upper;actual.guest_mask=(~upper)&0xffffu;actual.scene_epoch=77u;
    raw_runtime_before_scan(r,&actual);raw_runtime_poll_player(r,upper,&actual);
    CHECK(!r->controls.active_query.query_id); /* Waiting seed is not admitted yet. */
    source.abi=MANUAL_PREDICTION_ABI;source.rng_add=far?255u:6u;source.rng_sub=far?0u:234u;
    source.origin_counter=origin;source.source_epoch=77u;memset(source.source_identity.bytes,0x61,32);
    source.required_context_identity=manual_model_certificate()->clock_derivation;
    r->source_bg=far?0u:2u;CHECK(raw_runtime_bind_source(r,&source,1u));
    r->waiting=(CQWaitingState){source.rng_add,source.rng_sub,r->source_bg,{216u,39u,38u,39u,113u}};
    r->waiting_counter=origin;r->waiting_valid=1u;
    environment.state.initial_applied=environment.state.preparation_applied=environment.state.rtc_owned=1u;
    environment.state.epoch=77u;environment.state.origin_counter=origin;environment.state.source_bg=r->source_bg;
    memcpy(environment.state.source_identity,source.source_identity.bytes,32);
}
static ManualPrediction find_candidate(uint32_t lead){
    RawRuntime *r=&service.runtime;RawJob job={0};ManualPrediction prediction;
    ch_controller_request_query(&r->controls);raw_runtime_poll_player(r,physical,&actual);
    job.token=r->view.query;CHECK(job.token.query_id);
    job.source=r->source;job.source_bg=r->source_bg;job.boundary.counter=r->counter;
    job.boundary.source_epoch=77u;job.boundary.source_identity=r->source.source_identity;
    job.boundary.required_context_identity=r->source.required_context_identity;
    job.waiting=r->waiting;job.waiting_counter=r->waiting_counter;job.waiting_valid=r->waiting_valid;
    job.minimum_player_lead=lead;CHECK(raw_job_solve_cached(&job,0,&cache,&prediction)==MANUAL_QUERY_OK);
    CHECK(raw_runtime_accept_candidate(r,&job.token,&prediction));return prediction;
}
static void hold_and_release(void){
    RawRuntime *r=&service.runtime;uint32_t effective=r->effective_target;
    unit(CH_KEY_A);CHECK(!r->encounter_started&&!r->fault&&r->input_plan.raw_press_seen);
    CHECK(r->waiting.a==r->candidate.expected_wait_add&&r->waiting.s==r->candidate.expected_wait_sub&&
        r->waiting.clock.div==r->candidate.div_x);
    unit(CH_KEY_A);CHECK(r->encounter_started&&r->effective_press==effective&&!r->fault);
    CHECK(environment.terminal_attempted&&environment.state.terminal_applied&&environment.last_status==RAW_ENV_OK);
    CHECK(r->encounter_candidate.status==MANUAL_QUERY_OK&&case_terminal_log<MP_LOG_CAPACITY);
    unit(CH_KEY_A);unit(CH_KEY_A);unit(0u);
    CHECK(r->release_seen&&r->effective_release==effective+3u&&r->input_condition_matches&&!r->fault);
    raw_runtime_actual_dv(r,(uint16_t)r->encounter_candidate.predicted_dv);
    unit(0u);CHECK(!r->plan_active&&!r->plan_pending&&!r->plan_failed&&!r->fault);
    CHECK(r->input_plan.state==MP_CONDITIONAL_INPUT_OBSERVED);
    case_complete_log=r->input_plan.log_count;CHECK(case_complete_log<=207u);
}
static void long_wait(uint32_t origin,uint32_t upper){
    RawRuntime *r=&service.runtime;ManualPrediction p;uint32_t raw,elapsed=0u,prior_receipts=terminal_receipts;
    setup(origin,1,upper);p=find_candidate(120u);raw=r->raw_target;
    CHECK(p.press_relative==6684u&&p.div_x==249u&&p.predicted_dv==0xaaaau);
    CHECK(r->plan_pending&&!r->plan_active&&!r->input_plan.log_count&&r->controls.candidate_valid);
    while(actual.counter!=raw){
        uint32_t d=raw-actual.counter;
        unit(upper&&d>2u?upper:0u);elapsed++;
        CHECK(!r->fault&&!r->plan_failed&&!r->encounter_started);
        if(d>64u)CHECK(r->plan_pending&&!r->plan_active&&!r->input_plan.log_count);
        else CHECK(r->plan_active&&!r->plan_pending&&r->input_plan.log_count<MP_LOG_CAPACITY);
        if(elapsed==512u)CHECK(r->plan_pending&&!r->input_plan.log_count&&r->source_bound);
    }
    CHECK(elapsed==6683u&&case_arm==raw-64u);
    hold_and_release();CHECK(terminal_receipts==prior_receipts+1u);
    CHECK(case_terminal_log==197u&&case_complete_log==207u);
    printf("long_wait origin=%u relative=6684 raw=%u effective=%u arm=%u terminal_log=%u completed_log=%u upper=%u passed\n",
        origin,raw,p.target_counter,case_arm,case_terminal_log,case_complete_log,upper);
}
static void short_and_pending_guards(void){
    RawRuntime *r=&service.runtime;ManualPrediction p;uint32_t n;
    /* Query an actually projected current seed near the canonical target. */
    setup(100u,0,0u);for(n=0;n<230u;n++)unit(0u);begin_unit(0u);p=find_candidate(1u);
    CHECK(p.target_counter==350u&&r->plan_active&&!r->plan_pending&&r->input_plan.origin_counter==330u);
    finish_unit(0u);while(actual.counter!=r->raw_target)unit(0u);hold_and_release();
    CHECK(case_terminal_log==62u&&case_complete_log==72u);
    /* Forecast domain loss cancels a pending plan and never starts an observer. */
    setup(100u,1,0u);find_candidate(120u);r->waiting_valid=0u;
    unit(0u);CHECK(!r->plan_pending&&!r->plan_active&&r->plan_failed&&!r->fault);
    CHECK(!r->controls.candidate_valid&&!r->controls.active_query.query_id);
    setup(100u,1,0u);find_candidate(120u);actual.script_final_prompt=0u;unit(0u);
    CHECK(!r->source_bound&&!r->waiting_valid&&!r->plan_pending&&!r->plan_active&&!r->fault);
    setup(100u,1,0u);find_candidate(120u);actual.host_phase=1u;unit(0u);
    CHECK(!r->source_bound&&!r->plan_pending&&!r->plan_active&&!r->fault);
    /* A disappeared clock seed cannot turn an old token back into a plan. */
    setup(100u,1,0u);p=find_candidate(120u);r->waiting_valid=0u;
    raw_runtime_poll_player(r,0u,&actual);CHECK(!r->controls.active_query.query_id&&!r->controls.candidate_valid);
    unit(0u);CHECK(!r->plan_pending&&!r->plan_active&&!r->fault);
    /* Missing the complete arm window cancels, without advancing the game. */
    setup(100u,1,0u);find_candidate(120u);actual.counter=r->raw_target;
    begin_unit(0u);CHECK(!r->plan_pending&&!r->plan_active&&r->plan_failed&&!r->fault);
    CHECK(r->controls.active_query.query_id&&r->counter==actual.counter);
    /* At a real near boundary, use the actual released provider guard. */
    setup(100u,0,0u);find_candidate(1u);
    while(r->raw_target-actual.counter>64u)unit(0u);
    actual.guest_mask=0x00ffu;begin_unit(0u);
    CHECK(!r->plan_pending&&!r->plan_active&&r->plan_failed&&!r->fault);
    CHECK(r->input_plan.error==MP_UNSUPPORTED_SOURCE&&!r->controls.candidate_valid);
    /* Early physical A cancels a distant forecast; original effective A
       still reaches the unchanged strict terminal guard on its next scan. */
    setup(100u,1,0u);find_candidate(120u);unit(CH_KEY_A);
    CHECK(!r->plan_pending&&!r->plan_active&&r->plan_failed&&!r->fault&&r->actual_raw_press_seen);
    unit(CH_KEY_A);CHECK(r->encounter_started&&r->fault==RAW_FAULT_ENVIRONMENT);
    CHECK(r->encounter_candidate.status==MANUAL_QUERY_INVALID);
    /* Non-A GB input expires the pending forecast through actual masks. */
    setup(100u,1,0u);find_candidate(120u);unit(4u);unit(4u);
    CHECK(!r->plan_pending&&!r->plan_active&&r->plan_failed&&!r->fault&&!r->encounter_started);
}
int main(void){
    long_wait(100u,0u);long_wait(0xffffff00u,0u);long_wait(100u,0x8000u);
    short_and_pending_guards();
    printf("passed: %u long-plan assertions, 3 service long waits, current-seed short target, pending guards; memory endpoints mocked\n",checks);
    return 0;
}
