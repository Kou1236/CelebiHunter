#include "raw_service.h"
#include "query_completion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static RawRuntime r;
static Sol61Workspace workspace;

static RawQueryCache phase_cache;
static CQWaitingState canonical_waiting(void) {
    CQWaitingState p={6u,234u,2u,{216u,39u,38u,39u,113u}};
    return p;
}
static int bind_test_source(RawRuntime *runtime,const ManualSourceBinding *b,uint64_t generation) {
    if(!raw_runtime_bind_source(runtime,b,generation))return 0;
    runtime->waiting=canonical_waiting();runtime->waiting_counter=b->origin_counter;
    runtime->waiting_valid=1u;return 1;
}
static int fixture_job(const ManualSourceBinding *b,const ManualCurrentBoundary *c,
    uint32_t lead,RawJob *j) {
    uint32_t n,elapsed=c->counter-b->origin_counter;
    if(elapsed>=0x80000000u)return 0;
    memset(j,0,sizeof(*j));j->source=*b;j->boundary=*c;
    j->token=r.controls.active_query;j->token.source_counter=c->counter;
    j->token.minimum_target_counter=c->counter+1u;
    j->token.query_id=1u;j->token.scene_epoch=b->source_epoch;j->token.source_generation=1u;
    j->source_bg=2u;j->minimum_player_lead=lead;j->cache_build_allowed=1u;
    j->waiting=canonical_waiting();j->waiting_valid=1u;
    for(n=0;n<elapsed;n++)if(!cq_waiting_next(&j->waiting))return 0;
    j->waiting_counter=c->counter;return 1;
}
static int phase_query(const ManualSourceBinding *b,const ManualCurrentBoundary *c,
    const ManualModelCertificate *certificate,const MpOriginalInputEvidence *code,Sol61Workspace *w,ManualPrediction *p){
    RawJob job;(void)certificate;(void)code;
    if(!fixture_job(b,c,1u,&job))return MANUAL_QUERY_INVALID;
    return raw_job_solve_cached(&job,w,&phase_cache,p);
}
static uint32_t checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"failed line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
static RawSample sample(uint32_t counter) {
    RawSample s;memset(&s,0,sizeof(s));s.counter=counter;s.counter_read=1;s.sample_complete=1;
    s.ordinary_supported=1;s.batch=1;s.input_type=1;s.guest_mask=0xffff;s.script_final_prompt=1;s.scene_epoch=77;return s;
}
static ManualSourceBinding source(uint32_t origin) {
    ManualSourceBinding b;memset(&b,0,sizeof(b));b.abi=MANUAL_PREDICTION_ABI;b.rng_add=6;b.rng_sub=234;
    b.origin_counter=origin;b.source_epoch=77;memset(b.source_identity.bytes,0x61,32);
    b.required_context_identity=manual_model_certificate()->clock_derivation;return b;
}
static void poll(RawSample *s,uint32_t k){raw_runtime_poll_player(&r,k,s);}
static void bind_candidate(RawSample *s) {
    ManualSourceBinding b=source(s->counter);ManualCurrentBoundary c;ManualPrediction p;ch_query q;
    r.source_bg=2u;CHECK(bind_test_source(&r,&b,1));poll(s,0);q=r.view.query;CHECK(q.query_id);
    memset(&c,0,sizeof(c));c.counter=s->counter;c.source_epoch=77;c.source_identity=b.source_identity;
    c.required_context_identity=b.required_context_identity;
    CHECK(phase_query(&b,&c,manual_model_certificate(),mp_original_input_evidence(),&workspace,&p)==0);
    CHECK(p.target_counter-s->counter>=2);CHECK(p.manual_input_validated==0);
    CHECK(raw_runtime_accept_candidate(&r,&q,&p));
}
static void unit(RawSample *s,uint32_t held) {
    uint32_t old=s->host_held;
    raw_runtime_begin_scan(&r,s);s->host_held=held;s->host_down=held&~old;s->host_up=old&~held;
    s->host_intersection=old&held;
    raw_runtime_after_scan(&r,s);
    s->guest_mask=(~s->host_intersection)&0xffff;raw_runtime_engine_entry(&r,s);
    if(r.waiting_valid&&r.source_bound&&r.waiting_counter==s->counter) {
        CHECK(cq_waiting_next(&r.waiting));r.waiting_counter=s->counter+1u;
    }
    s->counter++;raw_runtime_before_scan(&r,s);poll(s,held);
}
static void pause(RawSample *s){poll(s,CH_KEY_L);poll(s,CH_KEY_L|CH_KEY_R);CHECK(r.runtime==CH_RUNTIME_PAUSED);poll(s,0);}
static void resume(RawSample *s){poll(s,CH_KEY_R);poll(s,0);CHECK(r.runtime==CH_RUNTIME_RUNNING);poll(s,0);}
static int hud_has(const RawHud *h,const char *line) {
    uint32_t n;for(n=0;n<h->count;n++)if(!strcmp(h->line[n],line))return 1;
    return 0;
}
static void hud_dv_fields(const RawHud *h,int predicted_valid,uint32_t predicted,int actual_valid,uint32_t actual){
    char predicted_text[5],actual_text[5],expected[96];
    if(predicted_valid)snprintf(predicted_text,sizeof(predicted_text),"%04X",(unsigned)(predicted&0xffffu));
    else strcpy(predicted_text,"--");
    if(actual_valid)snprintf(actual_text,sizeof(actual_text),"%04X",(unsigned)(actual&0xffffu));
    else strcpy(actual_text,"--");
    snprintf(expected,sizeof(expected),"Predicted DV %s | Actual DV %s",predicted_text,actual_text);
    CHECK(hud_has(h,expected));
}
static void hud_excludes_old_input_failure(const RawHud *h) {
    uint32_t i;
    CHECK(h->count<=7);
    CHECK(!strcmp(h->line[0],"CelebiHunter v1.4.0"));
    CHECK(!strcmp(h->line[h->count-1],"Start+Up: show/hide HUD"));
    for(i=0;i<h->count;i++){
        CHECK(strlen(h->line[i])<=50u);
        CHECK(!strstr(h->line[i],"Left"));
        CHECK(!strstr(h->line[i],"Input:"));
        CHECK(!strstr(h->line[i],"DV match"));
        CHECK(!strstr(h->line[i],"DV mismatch"));
        CHECK(!strstr(h->line[i],"Research"));
    }
    CHECK(!hud_has(h,"Input condition missed"));
    CHECK(!hud_has(h,"Forecast condition/result differs"));
}
static void test_player_control(void) {
    RawSample s=sample(100);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
    pause(&s);CHECK(r.observation_count==0);CHECK(s.counter==100);
    poll(&s,CH_KEY_L);poll(&s,CH_KEY_L);CHECK(r.runtime==CH_RUNTIME_PAUSED);
    poll(&s,0);CHECK(r.runtime==CH_RUNTIME_STEPPING);unit(&s,0);
    CHECK(r.runtime==CH_RUNTIME_PAUSED);CHECK(s.counter==101);CHECK(r.completed_request==r.last_request);
    poll(&s,0);CHECK(r.runtime==CH_RUNTIME_PAUSED);resume(&s);CHECK(s.counter==101);
    s.script_final_prompt=0;pause(&s);poll(&s,CH_KEY_A);CHECK(r.runtime==CH_RUNTIME_RUNNING);
    CHECK(r.view.command.initiated_by_physical_a);CHECK(s.guest_mask==0xffff);CHECK(!r.encounter_started);
    unit(&s,CH_KEY_A);CHECK(r.runtime==CH_RUNTIME_RUNNING);CHECK(s.counter==102);
    poll(&s,CH_KEY_A);CHECK(r.runtime==CH_RUNTIME_RUNNING);CHECK(!r.controls.pending.kind);
    CHECK(!r.fault);poll(&s,0);CHECK(r.runtime==CH_RUNTIME_RUNNING);
}
static void test_conditional_original_receipts(void) {
    RawSample s=sample(100);RawHud h;uint32_t raw,effective,release,original_dv;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
    raw=r.raw_target;effective=r.effective_target;release=r.release_target;original_dv=r.candidate.predicted_dv;
    CHECK(raw+1==effective);CHECK(release-effective==3);
    while(s.counter<raw)unit(&s,0);
    CHECK(r.runtime==CH_RUNTIME_RUNNING);CHECK(!r.encounter_started);CHECK(r.controls.candidate_valid);
    unit(&s,1);CHECK(!r.encounter_started);CHECK(s.guest_mask==0xffff);
    CHECK(r.input_plan.raw_press_seen);CHECK(!r.view.query.query_id);
    unit(&s,1);CHECK(r.encounter_started);CHECK(r.effective_press==effective);
    CHECK(r.encounter_candidate.predicted_dv==original_dv);CHECK(r.encounter_candidate.status==0);
    while(s.counter<release)unit(&s,1);
    unit(&s,0);CHECK(r.release_seen);CHECK(r.effective_release==release);CHECK(r.input_condition_matches);
    CHECK(r.input_plan.state==MP_CONDITIONAL_INPUT_OBSERVED);CHECK(!r.input_plan.manual_hardware_verified);
    raw_runtime_actual_dv(&r,0x1234);raw_runtime_actual_dv(&r,0xffff);
    CHECK(r.actual_dv==0x1234);CHECK(r.encounter_candidate.predicted_dv==original_dv);
    raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
    hud_dv_fields(&h,1,original_dv,1,0x1234);
    CHECK(h.count==7u&&!h.line[3][0]);
}
static void test_missed_and_wrong_input(void) {
    RawSample s=sample(100);RawHud h;uint32_t old_raw;ch_query token;ManualPrediction old;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
    old_raw=r.raw_target;old=r.candidate;while(s.counter<=old_raw)unit(&s,0);
    CHECK(r.runtime==CH_RUNTIME_RUNNING);CHECK(!r.plan_active);CHECK(!r.encounter_started);
    CHECK(r.plan_failed);CHECK(r.view.query.query_id);token=r.view.query;
    CHECK(!raw_runtime_accept_candidate(&r,&token,&old));
    unit(&s,1);unit(&s,1);CHECK(r.encounter_started);CHECK(r.encounter_candidate.status==MANUAL_QUERY_INVALID);
    CHECK(!r.input_condition_matches);
    raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
    CHECK(h.count==7u&&!h.line[4][0]);
    CHECK(r.encounter_forecast_reason==RAW_ENCOUNTER_FORECAST_OFF_TARGET);
    CHECK(hud_has(&h,"A pressed outside target Advance [R11]"));
    CHECK(!hud_has(&h,"Source check failed [R10]"));
    hud_dv_fields(&h,0,0,0,0);
    unit(&s,0);CHECK(r.release_seen);
    raw_runtime_actual_dv(&r,(uint16_t)old.predicted_dv);
    raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
    hud_dv_fields(&h,0,0,1,old.predicted_dv);
    /* A real environment failure remains distinct from this ordinary
       off-target encounter, including when an old MP error is still stored. */
    CHECK(r.actual_raw_press_seen&&r.actual_raw_press!=r.raw_target);
    CHECK(r.plan_failed&&r.input_plan.error==MP_INPUT_MISMATCH);
    raw_runtime_environment_failed(&r);raw_runtime_hud(&r,&h);
    CHECK(r.fault==RAW_FAULT_ENVIRONMENT);
    CHECK(hud_has(&h,"Source check failed [R10]"));
    CHECK(!hud_has(&h,"A pressed outside target Advance [R11]"));
    CHECK(h.count==7u&&!h.line[4][0]);hud_excludes_old_input_failure(&h);
    /* A write/read failure or different recorded control fault cannot be
       relabelled as a player timing error merely because A was pressed. */
    r.input_plan.error=MP_COUNTER_MISMATCH;raw_runtime_hud(&r,&h);
    CHECK(hud_has(&h,"Source check failed [R10]"));
    CHECK(!hud_has(&h,"A pressed outside target Advance [R11]"));
    r.input_plan.error=MP_INPUT_MISMATCH;r.actual_raw_press=r.raw_target;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Source check failed [R10]"));
    CHECK(!hud_has(&h,"A pressed outside target Advance [R11]"));
    r.actual_raw_press++;r.fault=RAW_FAULT_DISPLAY;raw_runtime_hud(&r,&h);
    CHECK(hud_has(&h,"Display fault [R20]"));
}
static void test_noncanonical_release_hud_keeps_DV_and_certificate_separate(void) {
    RawSample s;RawHud h;uint32_t which,raw,effective,release,predicted;
    for(which=0;which<2;which++) {
        s=sample(100);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
        raw=r.raw_target;effective=r.effective_target;release=r.release_target;predicted=r.candidate.predicted_dv;
        while(s.counter<raw)unit(&s,0);
        unit(&s,1);unit(&s,1);
        CHECK(r.encounter_started&&r.encounter_candidate.status==MANUAL_QUERY_OK);
        CHECK(r.effective_press==effective&&!r.release_seen);
        raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
        CHECK(h.count==7u&&!h.line[3][0]);
        hud_dv_fields(&h,1,predicted,0,0);
        CHECK(!r.release_seen&&r.encounter_candidate.expected_release_counter==release);
        if(which)while(s.counter<release+1u)unit(&s,1);
        unit(&s,0);
        CHECK(r.release_seen&&r.plan_failed&&!r.plan_active&&!r.input_condition_matches);
        CHECK(r.effective_release==(which?release+1u:effective+1u));
        CHECK(r.encounter_candidate.status==MANUAL_QUERY_OK&&r.encounter_candidate.predicted_dv==predicted);
        CHECK(!r.input_plan.manual_hardware_verified);
        raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
        CHECK(h.count==7u&&!h.line[3][0]);
        hud_dv_fields(&h,1,predicted,0,0);
        CHECK(r.encounter_candidate.expected_release_counter==release);
        raw_runtime_actual_dv(&r,(uint16_t)predicted);
        raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
        hud_dv_fields(&h,1,predicted,1,predicted);
        CHECK(r.release_seen&&r.effective_release==(which?release+1u:effective+1u));
        CHECK(!r.input_condition_matches);
    }
}
static void test_one_L_release_never_repeats_and_plus_two_faults(void) {
    RawSample s=sample(300);uint32_t n,request,ack,counter;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);pause(&s);
    for(n=0;n<128;n++) {
        poll(&s,CH_KEY_L);CHECK(r.runtime==CH_RUNTIME_PAUSED);
        CHECK(!r.view.command.kind&&s.counter==300);
    }
    poll(&s,0);CHECK(r.runtime==CH_RUNTIME_STEPPING);
    request=r.last_request;CHECK(r.step_target==301);
    for(n=0;n<128;n++) {
        poll(&s,0);CHECK(r.runtime==CH_RUNTIME_STEPPING);
        CHECK(!r.view.command.kind&&r.last_request==request&&s.counter==300);
    }
    unit(&s,0);CHECK(r.runtime==CH_RUNTIME_PAUSED&&s.counter==301);
    CHECK(r.completed_request==request&&!r.fault);
    for(n=0;n<128;n++) {
        poll(&s,0);CHECK(r.runtime==CH_RUNTIME_PAUSED);
        CHECK(!r.view.command.kind&&r.last_request==request&&s.counter==301);
    }
    ack=r.completed_request;poll(&s,CH_KEY_L);poll(&s,0);
    CHECK(r.runtime==CH_RUNTIME_STEPPING&&r.step_target==302);
    request=r.last_request;CHECK(request!=ack);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    s.counter+=2;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.runtime==CH_RUNTIME_PAUSED&&r.fault==RAW_FAULT_UNIT);
    CHECK(r.completed_request==ack&&r.completed_request!=request);
    CHECK(!r.source_bound&&!r.controls.candidate_valid&&r.controls.fault!=CH_FAULT_NONE);
    counter=s.counter;poll(&s,CH_KEY_L);poll(&s,0);
    CHECK(r.runtime==CH_RUNTIME_PAUSED&&s.counter==counter);
    CHECK(r.last_request==request&&!r.view.command.kind);
}
static void test_guards_and_recovery(void) {
    RawSample s;uint32_t bad;
    for(bad=0;bad<3;bad++) {
        s=sample(10);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);pause(&s);
        poll(&s,CH_KEY_L);poll(&s,0);CHECK(r.runtime==CH_RUNTIME_STEPPING);
        raw_runtime_begin_scan(&r,&s);s.host_intersection=0;raw_runtime_after_scan(&r,&s);
        raw_runtime_engine_entry(&r,&s);s.counter+=(bad==0?0:bad==1?2:1);
        if(bad==2)s.host_phase=2;
        raw_runtime_before_scan(&r,&s);poll(&s,0);CHECK(r.runtime==CH_RUNTIME_PAUSED);
        CHECK(r.fault);poll(&s,CH_KEY_L);poll(&s,0);CHECK(r.runtime==CH_RUNTIME_PAUSED);
        resume(&s);CHECK(r.fault);CHECK(!r.source_bound);
    }
    s=sample(20);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);pause(&s);
    raw_runtime_sample_failed(&r);poll(&s,0);CHECK(!r.counter_valid);resume(&s);
    CHECK(r.runtime==CH_RUNTIME_RUNNING);CHECK(!r.counter_valid);
}
static void test_wrap_and_tokens(void) {
    RawSample s=sample(0xfffffffc);ManualPrediction p;ch_query q;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);CHECK(r.raw_target<1000u&&r.raw_target-s.counter==r.candidate.press_relative-1u);
    p=r.candidate;q=r.controls.active_query;q.query_id=999;CHECK(!raw_runtime_accept_candidate(&r,&q,&p));
    CHECK(r.plan_pending&&!r.plan_active);pause(&s);poll(&s,CH_KEY_L);poll(&s,0);unit(&s,0);
    CHECK(r.runtime==CH_RUNTIME_PAUSED);CHECK(!r.fault);
}
static void test_binding_regressions(void) {
    RawSample s=sample(100);ManualPrediction p;ch_query q;ManualSourceBinding b;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
    b=source(100);r.source_bg=2u;CHECK(bind_test_source(&r,&b,1));poll(&s,0);q=r.view.query;
    {ManualCurrentBoundary c={0};c.counter=100;c.source_epoch=77;c.source_identity=b.source_identity;
     c.required_context_identity=b.required_context_identity;
     CHECK(phase_query(&b,&c,manual_model_certificate(),mp_original_input_evidence(),&workspace,&p)==0);}
    p.origin_counter++;p.target_counter++;p.expected_release_counter++;
    CHECK(!raw_runtime_accept_candidate(&r,&q,&p));
    p.origin_counter--;p.target_counter--;p.expected_release_counter--;p.queried_at_counter++;
    CHECK(!raw_runtime_accept_candidate(&r,&q,&p));p.queried_at_counter--;
    CHECK(raw_runtime_accept_candidate(&r,&q,&p));
    while(s.counter<r.raw_target)unit(&s,0);
    unit(&s,1);CHECK(r.original_raw_a_seen);CHECK(!r.encounter_started);
    s.host_phase=1;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(!r.source_bound);CHECK(!r.view.query.query_id);CHECK(r.original_raw_a_seen);
    CHECK(!raw_runtime_bind_source(&r,&b,2));
    /* A fresh source cannot reuse a half-observed held raw A, even when the
     * old conditional plan has already failed. */
}
static void test_late_worker_future_retry(void) {
    uint32_t paused;
    for(paused=0;paused<2;paused++) {
        RawSample s=sample(100);ManualSourceBinding b=source(100);
        ManualCurrentBoundary current={0};ManualPrediction p,bad,next;ch_query q,newq;
        uint32_t before_count,before_counter,before_mask;
        raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
        r.source_bg=2u;CHECK(bind_test_source(&r,&b,1));poll(&s,0);q=r.view.query;
        current.counter=100;current.source_epoch=77;current.source_identity=b.source_identity;
        current.required_context_identity=b.required_context_identity;
        CHECK(phase_query(&b,&current,manual_model_certificate(),mp_original_input_evidence(),&workspace,&p)==0);
        while(s.counter<p.target_counter)unit(&s,0);
        if(paused)pause(&s);
        before_count=r.observation_count;before_counter=r.counter;before_mask=s.guest_mask;
        bad=p;bad.expected_release_counter++;
        CHECK(!raw_runtime_accept_candidate(&r,&q,&p));
        CHECK(!raw_runtime_retry_missed_candidate(&r,&q,&bad));
        CHECK(r.controls.active_query.query_id==q.query_id);
        CHECK(raw_runtime_retry_missed_candidate(&r,&q,&p));
        CHECK(r.counter==before_counter&&s.guest_mask==before_mask&&r.observation_count==before_count);
        CHECK(r.runtime==(paused?CH_RUNTIME_PAUSED:CH_RUNTIME_RUNNING));
        poll(&s,0);newq=r.view.query;CHECK(newq.query_id&&newq.query_id!=q.query_id);
        CHECK(newq.source_counter==r.counter&&r.source.origin_counter==100);
        current.counter=r.counter;
        CHECK(phase_query(&b,&current,manual_model_certificate(),mp_original_input_evidence(),&workspace,&next)==0);
        CHECK(raw_runtime_accept_candidate(&r,&newq,&next));
        CHECK(r.raw_target-r.counter>0&&r.raw_target-r.counter<0x80000000u);
        CHECK(!raw_runtime_retry_missed_candidate(&r,&q,&p));
        CHECK(r.runtime==(paused?CH_RUNTIME_PAUSED:CH_RUNTIME_RUNNING));
    }
}
static RawService svc;
static RawSample svc_sample;
static uint32_t svc_keys,wait_count,draw_count,read_count,post_count,first_count,present_count;
static uint32_t display_updates,display_restores;
static int service_sample(void *u,RawSample *s){(void)u;read_count++;*s=svc_sample;return 1;}
static int service_keys(void *u,uint32_t *k){(void)u;*k=svc_keys;return 1;}
static void service_draw(void *u,const RawHud *h){(void)u;CHECK(h->count<=12);draw_count++;}
static void service_wait(void *u){(void)u;wait_count++;CHECK(wait_count<10);svc_keys=wait_count==1?0:CH_KEY_A;}
static int submit(void *u,const RawJob *j){(void)u;(void)j;post_count++;return 1;}
static int receive(void *u,RawJob *j,ManualPrediction *p){(void)u;(void)j;(void)p;return 0;}
static int first_scheduler(void *u,const ChNativeContext *c,const RawRuntime *r){
    (void)u;CHECK(c->lr==0x1a8340);CHECK(r->engine_entries==1);first_count++;return 0;}
static void service_present(void *u,const ChNativeContext *c){(void)u;CHECK(c->lr==0x145480);present_count++;}
static int service_display(void *u,const ChNativeContext *c,const RawRuntime *runtime,uint32_t action){
    (void)u;CHECK(c->lr==0x1042f4&&runtime->at_gate&&!runtime->scan_open);
    CHECK(runtime->counter==200&&runtime->observation_count==0);
    if(action==1){display_updates++;CHECK(runtime->runtime==CH_RUNTIME_PAUSED);return 1;}
    CHECK(action==2);display_restores++;CHECK(runtime->runtime==CH_RUNTIME_RUNNING);
    return display_restores==1?-1:display_restores==2?0:1;
}
static void test_service_original_call_order(void) {
    RawServiceOps o;ChNativeContext c;uint32_t n;memset(&o,0,sizeof(o));memset(&c,0,sizeof(c));
    o.sample=service_sample;o.physical_keys=service_keys;o.draw=service_draw;o.wait_poll=service_wait;
    o.submit_copy=submit;o.receive_copy=receive;o.first_scheduler=first_scheduler;
    o.present=service_present;
    o.paused_display=service_display;
    svc_keys=0;CHECK(raw_service_init(&svc,&o));
    svc_sample=sample(200);svc_sample.script_final_prompt=0;
    svc_keys=CH_KEY_L|CH_KEY_R;c.lr=0x1042f4;
    CHECK(raw_service_route(&svc,&c)==0x110fa0);CHECK(wait_count==4);
    CHECK(display_updates==2&&display_restores==3&&!svc.display_owned);
    CHECK(!svc.runtime.display_pending&&!svc.runtime.display_error&&!svc.runtime.fault);
    CHECK(svc.runtime.runtime==CH_RUNTIME_RUNNING);CHECK(svc.runtime.scan_open);
    CHECK(svc.runtime.observation_count==1);CHECK(!svc.runtime.encounter_started);
    svc_sample.host_held=1;svc_sample.host_intersection=0;c.lr=0x104378;
    CHECK(raw_service_route(&svc,&c)==0x10a430);CHECK(!svc.runtime.scan_open);
    c.lr=0x1a82e0;CHECK(raw_service_route(&svc,&c)==0x1aabf8);
    c.lr=0x1a8340;CHECK(raw_service_route(&svc,&c)==0x1aae60);CHECK(svc.pending_marker==0);
    CHECK(raw_service_route(&svc,&c)==0x1aae60);CHECK(svc.runtime.engine_entries==1);
    CHECK(first_count==1);
    /* Repeated original scheduler iterations cannot multiply process reads,
       execute another first-scheduler callback, poll UI or change receipts. */
    for(n=0;n<2048u;n++)CHECK(raw_service_route(&svc,&c)==0x1aae60);
    CHECK(svc.pending_marker==0&&svc.runtime.engine_entries==1);
    CHECK(first_count==1&&read_count==4);
    CHECK(post_count==0);CHECK(draw_count>=3);CHECK(read_count==4);
    c.lr=0x145480;CHECK(raw_service_route(&svc,&c)==0x14aa24);
    CHECK(present_count==1&&first_count==1&&read_count==4);CHECK(post_count==0);
}
static void test_player_paused_ready_A_resumes_and_observes_release(void) {
    RawSample s=sample(100);RawHud h;uint32_t raw,release;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
    raw=r.raw_target;release=r.release_target;while(s.counter<raw)unit(&s,0);pause(&s);
    CHECK(r.controls.candidate_valid&&r.controls.prediction==CH_PREDICTION_READY);
    poll(&s,CH_KEY_A);CHECK(r.runtime==CH_RUNTIME_RUNNING);
    CHECK(r.view.command.kind==CH_COMMAND_RESUME&&r.view.command.initiated_by_physical_a);
    CHECK(r.counter==raw&&!r.original_raw_a_seen&&!r.encounter_started);
    unit(&s,CH_KEY_A);
    CHECK(r.runtime==CH_RUNTIME_RUNNING&&!r.encounter_started&&!r.fault);
    CHECK(!r.view.query.query_id&&!r.controls.active_query.query_id);
    while(s.counter<release) {
        unit(&s,CH_KEY_A);
        CHECK(r.runtime==CH_RUNTIME_RUNNING&&!r.fault);
        CHECK(!r.view.query.query_id&&!r.controls.active_query.query_id);
    }
    CHECK(r.encounter_started&&!r.release_seen&&r.counter==release);
    poll(&s,0);CHECK(r.runtime==CH_RUNTIME_RUNNING&&!r.release_seen);
    unit(&s,0);
    CHECK(r.runtime==CH_RUNTIME_RUNNING&&!r.fault);CHECK(r.release_seen&&r.input_condition_matches);
    CHECK(r.actual_raw_press_seen&&r.actual_raw_press==raw);
    CHECK(r.actual_raw_release_seen&&r.actual_raw_release==release);
    CHECK(r.actual_press_mask==0xfffe&&r.actual_release_mask==0xffff);
    CHECK(!r.input_plan.manual_hardware_verified);
    raw_runtime_actual_dv(&r,(uint16_t)r.encounter_candidate.predicted_dv);
    raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
    CHECK(hud_has(&h,"CelebiHunter v1.4.0"));
    hud_dv_fields(&h,1,r.encounter_candidate.predicted_dv,1,r.encounter_candidate.predicted_dv);
}
static void test_player_leaves_prompt(void) {
    RawSample s=sample(100);
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
    s.script_final_prompt=0;unit(&s,0);
    CHECK(!r.source_bound&&!r.controls.candidate_valid&&!r.plan_active);
    CHECK(!r.fault&&!r.encounter_started&&r.runtime==CH_RUNTIME_RUNNING);
    CHECK(!r.view.query.query_id&&s.guest_mask==0xffff);
}
static void test_source_bound_on_real_first_scheduler(void) {
    RawSample s=sample(200);ManualSourceBinding b=source(200);
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    CHECK(r.engine_entries==1&&r.have_previous_scan);
    r.source_bg=2u;CHECK(bind_test_source(&r,&b,1));
    CHECK(r.have_previous_scan&&r.scan_before_counter==200);
    s.counter++;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.source_bound&&!r.fault&&r.counter==201&&r.engine_entries==1);
    CHECK(r.view.query.query_id&&r.view.query.source_counter==201);
}
static uint32_t recovery_waits,recovery_restore,recovery_cancel,recovery_action,recovery_error,recovery_step;
static void recovery_wait(void *u){(void)u;recovery_waits++;CHECK(recovery_waits<RAW_DISPLAY_EXIT_POLLS+8);
    svc_keys=recovery_waits==1?0:recovery_waits==2?recovery_action:0;
}
static int recovery_display(void *u,const ChNativeContext *c,const RawRuntime *runtime,uint32_t action){
    (void)u;(void)c;CHECK(runtime->counter==200&&runtime->observation_count==0);
    if(action==1){CHECK(runtime->runtime==CH_RUNTIME_PAUSED);return 0;}
    CHECK(runtime->runtime!=CH_RUNTIME_PAUSED);
    if(action==4){CHECK(runtime->runtime==CH_RUNTIME_STEPPING);recovery_step++;return 1;}
    if(action==2){recovery_restore++;return recovery_error?-1:0;}
    CHECK(action==3);recovery_cancel++;return 1;
}
static void test_player_exit_display_wait_is_bounded(void){
    uint32_t mode;RawServiceOps o;ChNativeContext c;
    for(mode=0;mode<6;mode++){
        memset(&o,0,sizeof(o));memset(&c,0,sizeof(c));
        o.sample=service_sample;o.physical_keys=service_keys;o.draw=service_draw;
        o.wait_poll=recovery_wait;o.submit_copy=submit;o.receive_copy=receive;
        o.paused_display=recovery_display;
        recovery_action=mode%3==0?CH_KEY_R:mode%3==1?CH_KEY_A:CH_KEY_L;
        recovery_error=mode/3;recovery_waits=recovery_restore=recovery_cancel=recovery_step=0;
        svc_keys=0;svc_sample=sample(200);
        if(mode%3==1)svc_sample.script_final_prompt=0;
        CHECK(raw_service_init(&svc,&o));
        svc_keys=CH_KEY_L|CH_KEY_R;c.lr=0x1042f4;
        CHECK(raw_service_route(&svc,&c)==0x110fa0);
        if(mode%3==2){
            CHECK(recovery_step==1&&!recovery_restore&&!recovery_cancel);
            CHECK(svc.display_owned&&!svc.display_abandoned&&svc.runtime.scan_open);
            CHECK(svc.runtime.runtime==CH_RUNTIME_STEPPING&&!svc.runtime.fault);
            CHECK(svc.runtime.counter==200&&svc.runtime.observation_count==1);
            CHECK(!svc.runtime.original_raw_a_seen&&!svc.runtime.encounter_started);
            continue;
        }
        CHECK(recovery_restore==RAW_DISPLAY_EXIT_POLLS&&recovery_cancel==1);
        CHECK(svc.display_abandoned&&!svc.display_owned&&svc.runtime.scan_open);
        CHECK(svc.runtime.counter==200&&svc.runtime.observation_count==1);
        CHECK(svc.runtime.fault==RAW_FAULT_DISPLAY&&svc.runtime.display_error);
        CHECK(!svc.runtime.display_pending&&!svc.runtime.source_bound&&!svc.runtime.plan_active);
        CHECK(!svc.runtime.controls.candidate_valid&&svc_sample.guest_mask==0xffff);
        CHECK(svc.runtime.runtime==(mode%3==2?CH_RUNTIME_STEPPING:CH_RUNTIME_RUNNING));
        CHECK(!svc.runtime.original_raw_a_seen&&!svc.runtime.encounter_started);
    }
}
static void test_released_GS_arrival_starts_running_search(void){
    RawSample s=sample(100);uint32_t raw,release,press_before_bind;
    /* Ordinary A advances the preceding text. Both original scan words and
       the applied guest A survive; the plugin cannot call this an encounter
       before an admitted final-prompt source exists. */
    s.script_final_prompt=0;raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);
    poll(&s,CH_KEY_A);CHECK(!r.search_started&&r.runtime==CH_RUNTIME_RUNNING);
    unit(&s,CH_KEY_A);CHECK(s.host_held==CH_KEY_A&&s.host_intersection==0u);
    unit(&s,CH_KEY_A);CHECK(s.host_intersection==CH_KEY_A&&s.guest_mask==0xfffeu);
    CHECK(r.original_raw_a_seen&&r.actual_raw_press_seen&&!r.encounter_started);
    CHECK(!r.guest_a_initialized&&!r.search_requested&&!r.view.command.kind);
    CHECK(!r.last_request&&!r.fault);
    press_before_bind=r.actual_raw_press;
    /* A held arrival is not eligible. The real scene matcher supplies this
       false predicate until the original GB input mirrors are released. */
    poll(&s,CH_KEY_A);CHECK(!r.search_started&&!r.search_requested);
    unit(&s,0);CHECK(s.host_held==0&&s.host_intersection==0&&s.guest_mask==0xffffu);
    CHECK(!r.search_started&&r.actual_raw_release_seen&&!r.encounter_started);
    s.script_final_prompt=1;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.search_started&&r.search_requested);
    CHECK(r.runtime==CH_RUNTIME_RUNNING&&s.counter==103&&!r.view.command.kind);
    CHECK(!r.last_request&&!r.controls.pending.kind&&!r.encounter_started);
    poll(&s,0);CHECK(r.search_requested&&!r.view.command.kind);
    /* Binding reads a genuinely released sample and starts a new receipt
       origin; the preceding text A must not contaminate its input proof. */
    bind_candidate(&s);
    CHECK(!r.search_requested&&r.search_started&&r.controls.candidate_valid);
    CHECK(r.have_previous_scan&&r.guest_a_initialized&&r.guest_a_previous==0);
    CHECK(!r.original_raw_a_seen&&!r.actual_raw_press_seen&&!r.actual_raw_release_seen);
    CHECK(r.actual_raw_press==0&&r.actual_raw_release==0);
    CHECK(press_before_bind==100&&!r.encounter_started&&!r.fault);
    raw=r.raw_target;release=r.release_target;
    while(s.counter<raw)unit(&s,0);
    CHECK(!r.search_requested&&r.runtime==CH_RUNTIME_RUNNING&&!r.last_request);
    /* The next A is original encounter input, never a search command. */
    unit(&s,CH_KEY_A);CHECK(s.host_held==CH_KEY_A&&s.host_intersection==0u);
    CHECK(r.original_raw_a_seen&&r.actual_raw_press_seen&&!r.encounter_started);
    CHECK(r.actual_raw_press==raw&&!r.search_requested);
    unit(&s,CH_KEY_A);CHECK(s.host_intersection==CH_KEY_A&&s.guest_mask==0xfffeu);
    CHECK(r.encounter_started&&r.effective_press==raw+1u);
    CHECK(r.encounter_candidate.status==MANUAL_QUERY_OK);
    while(s.counter<release)unit(&s,CH_KEY_A);
    unit(&s,0);CHECK(r.release_seen&&r.input_condition_matches);
    CHECK(!r.last_request&&!r.view.command.kind&&!r.fault);
}
static void test_compact_hud_layout(void){
    RawHud h;uint32_t state,flags;
    raw_runtime_init(&r,0);
    r.controls.overlay_visible=1;r.counter_valid=1;r.counter=2353;
    r.controls.candidate_valid=1;r.raw_target=2500;r.candidate.predicted_dv=0xfaaa;r.waiting_valid=1;
    for(state=CH_RUNTIME_RUNNING;state<=CH_RUNTIME_STEPPING;state++){
        r.runtime=(ch_runtime_state)state;raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
        CHECK(h.visible&&h.count==7u&&!h.line[3][0]);
        CHECK(hud_has(&h,"Advance 2353 | Target 2500"));
        hud_dv_fields(&h,1,0xfaaa,0,0);
    }
    r.runtime=CH_RUNTIME_PAUSED;r.counter=r.raw_target;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"A starts | L step | R run"));
    r.waiting_valid=0;r.waiting_recheck=1;raw_runtime_hud(&r,&h);
    CHECK(hud_has(&h,"Advance 2500 | Target 2500")&&!hud_has(&h,"A starts | L step | R run"));
    CHECK(!hud_has(&h,"Live state recheck; target held")&&!h.line[3][0]&&h.count==7u);
    r.waiting_valid=1;r.waiting_recheck=0;
    r.counter=0xfffffffeu;r.raw_target=1u;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Advance 4294967294 | Target 1"));
    hud_dv_fields(&h,1,0xfaaa,0,0);
    r.counter=2u;r.raw_target=1u;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Advance 2 | Target --"));
    CHECK(!hud_has(&h,"A starts | L step | R run"));hud_dv_fields(&h,0,0,0,0);
    r.encounter_started=1;r.encounter_candidate.status=MANUAL_QUERY_OK;
    r.encounter_candidate.predicted_dv=0xfaaa;r.raw_target=1u;r.actual_dv=0x2aaa;
    /* Input proof stays internal. Equal or different actual DVs remain their
       observed values in every release state, without outcome prose. */
    for(flags=0;flags<8;flags++){
        r.actual_seen=flags&1u;r.release_seen=(flags>>1)&1u;r.input_condition_matches=(flags>>2)&1u;
        raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
        CHECK(h.count==7u&&!h.line[3][0]);
        hud_dv_fields(&h,1,0xfaaa,r.actual_seen,0x2aaa);
    }
    r.fault=RAW_FAULT_DISPLAY;raw_runtime_hud(&r,&h);hud_excludes_old_input_failure(&h);
    CHECK(h.count==7u&&!h.line[4][0]&&hud_has(&h,"Display fault [R20]"));
    r.fault=0;r.counter_valid=0;r.encounter_started=0;r.controls.candidate_valid=0;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Advance -- | Target --"));hud_dv_fields(&h,0,0,1,0x2aaa);
}

static void test_hud_failure_codes(void){
    RawHud h;uint32_t i,j;int found;const struct {uint32_t status,detail;const char *expected;} cases[]={
        {MANUAL_QUERY_INVALID,RAW_QUERY_DETAIL_JOB_BINDING,"Copied search input failed validation [Q01]"},
        {MANUAL_QUERY_INVALID,RAW_QUERY_DETAIL_RESULT_BINDING,"Search result no longer matches live state [Q02]"},
        {MANUAL_QUERY_INVALID,RAW_QUERY_DETAIL_PLAN_ADMISSION,"Player input plan could not be tracked [Q03]"},
        {MANUAL_QUERY_INVALID,RAW_QUERY_DETAIL_SUBMISSION,"Search job could not be queued [Q04]"},
        {MANUAL_QUERY_INVALID,RAW_QUERY_DETAIL_WORKER_STATUS,"Search worker rejected its input [Q05]"},
        {MANUAL_QUERY_STALE_SOURCE,RAW_QUERY_DETAIL_NONE,"Live source changed [Q06]"},
        {MANUAL_QUERY_OUTSIDE_DOMAIN,RAW_QUERY_DETAIL_WINDOW_EXPIRED,"Source window expired; refreshing [Q07]"},
        {MANUAL_QUERY_SOLVER_BOUND,RAW_QUERY_DETAIL_NONE,"Search limit reached [Q08]"},
        {MANUAL_QUERY_UNSUPPORTED_CERTIFICATE,RAW_QUERY_DETAIL_NONE,"Timing profile unsupported [Q09]"},
        {MANUAL_QUERY_OUTSIDE_DOMAIN,RAW_QUERY_DETAIL_NONE,"Search range outside model [Q10]"},
        {MANUAL_QUERY_NO_FUTURE_CANDIDATE,RAW_QUERY_DETAIL_NONE,"No shiny in search window [Q11]"}
    };
    raw_runtime_init(&r,0);r.controls.overlay_visible=1;r.counter_valid=1;r.counter=100;
    for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        r.query_status=cases[i].status;r.query_failure_detail=cases[i].detail;
        raw_runtime_hud(&r,&h);CHECK(hud_has(&h,cases[i].expected));
    }
    r.query_status=MANUAL_QUERY_INVALID;r.query_failure_detail=RAW_QUERY_DETAIL_NONE;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Search input invalid [Q00]"));
    r.query_status=MANUAL_QUERY_OK;raw_runtime_hud(&r,&h);found=0;
    for(j=0;j<h.count;j++)if(strstr(h.line[j],"[Q"))found=1;
    CHECK(!found);
}

static void expect_runtime_fault(uint32_t fault,uint32_t control_fault,uint32_t plan_error,
    uint32_t plan_failed,uint32_t display_pending,uint32_t display_error,const char *expected){
    RawHud h;raw_runtime_init(&r,0);r.controls.overlay_visible=1;r.counter_valid=1;r.counter=100;
    r.fault=fault;r.controls.fault=control_fault;r.input_plan.error=plan_error;
    r.plan_failed=plan_failed;r.display_pending=display_pending;r.display_error=display_error;
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,expected));
}
static void test_runtime_fault_codes(void){
    expect_runtime_fault(RAW_FAULT_READ,0,0,0,0,0,"Game state read failed [R01]");
    expect_runtime_fault(RAW_FAULT_UNIT,0,0,0,0,0,"Unsupported scheduler state [R02]");
    expect_runtime_fault(RAW_FAULT_FILTER,0,0,0,0,0,"Input scan did not match [R03]");
    expect_runtime_fault(RAW_FAULT_ORDER,0,0,0,0,0,"Unexpected game update [R04]");
    expect_runtime_fault(RAW_FAULT_ORDER,CH_FAULT_BAD_ACK,0,0,0,0,"Pause/step acknowledgement failed [R05]");
    expect_runtime_fault(RAW_FAULT_ORDER,CH_FAULT_STEP_OVERSHOT,0,0,0,0,"Step passed target Advance [R06]");
    expect_runtime_fault(RAW_FAULT_ORDER,CH_FAULT_SCENE_CHANGED_DURING_COMMAND,0,0,0,0,"Scene changed during control [R07]");
    expect_runtime_fault(RAW_FAULT_ORDER,CH_FAULT_COUNTER_DISCONTINUITY,0,0,0,0,"Advance counter discontinuity [R08]");
    expect_runtime_fault(RAW_FAULT_COMMAND,0,0,0,0,0,"Pause/step request rejected [R09]");
    expect_runtime_fault(RAW_FAULT_ENVIRONMENT,0,MP_BAD_ARGUMENT,0,0,0,"Source check failed [R10]");
    expect_runtime_fault(RAW_FAULT_ENVIRONMENT,0,MP_INPUT_MISMATCH,1,0,0,"Source check failed [R10]");
    expect_runtime_fault(RAW_FAULT_ENVIRONMENT,0,MP_COUNTER_MISMATCH,1,0,0,"Source check failed [R10]");
    expect_runtime_fault(RAW_FAULT_DISPLAY,0,0,0,0,0,"Display fault [R20]");
    expect_runtime_fault(RAW_FAULT_DISPLAY,0,0,0,1,0,"Display restore pending [R21]");
    expect_runtime_fault(99,0,0,0,0,0,"Runtime check failed [R00]");
    expect_runtime_fault(99,0,0,0,0,1,"Display not ready [R22]");
}
static void test_controller_counter_regression_reports_runtime_fault(void){
    RawSample s=sample(200);ManualSourceBinding b=source(200),fresh;RawHud h;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
    r.source_bg=2u;CHECK(bind_test_source(&r,&b,1u));poll(&s,0);
    CHECK(!r.fault&&r.controls.fault==CH_FAULT_NONE&&r.controls.active_query.query_id);
    /* Supply an actual backwards sample through the normal runtime entry and
       UI poll. Neither fault field is fabricated by this test. */
    s.counter=199u;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
    CHECK(r.fault==RAW_FAULT_ORDER&&r.runtime==CH_RUNTIME_RUNNING);
    CHECK(!raw_runtime_source_recovery_pending(&r));
    CHECK(!r.source_bound&&!r.waiting_valid&&!r.controls.candidate_valid);
    CHECK(!r.view.query.query_id&&!r.controls.active_query.query_id);
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Advance counter discontinuity [R08]"));
    CHECK(!hud_has(&h,"Unexpected game update [R04]"));
    /* A later good read cannot silently clear this control fault or revive
       source acquisition after a genuine counter discontinuity. */
    s.counter=200u;raw_runtime_before_scan(&r,&s);poll(&s,0);fresh=source(s.counter);
    CHECK(!raw_runtime_bind_source(&r,&fresh,2u));
    CHECK(r.fault==RAW_FAULT_ORDER&&r.controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
    raw_runtime_hud(&r,&h);CHECK(hud_has(&h,"Advance counter discontinuity [R08]"));
}
static void expire_test_source(RawSample *s){ManualSourceBinding b=source(s->counter);
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,s);poll(s,0);
    r.source_bg=2u;CHECK(bind_test_source(&r,&b,1u));poll(s,0);
    CHECK(r.source_bound&&r.waiting_valid&&!r.refresh_needed&&!r.fault);
    /* A coherent scheduler sample can temporarily leave the model without
       failing its memory read or requesting a player command. */
    s->batch=2u;s->ordinary_supported=0u;raw_runtime_before_scan(&r,s);poll(s,0);
    CHECK(!r.source_bound&&!r.waiting_valid&&!r.controls.candidate_valid);
    CHECK(r.refresh_needed&&!r.fault&&r.runtime==CH_RUNTIME_RUNNING);
    CHECK(raw_runtime_source_recovery_pending(&r));
    CHECK(!r.view.command.kind&&!r.view.query.query_id);
    *s=sample(s->counter+1u);raw_runtime_before_scan(&r,s);poll(s,0);
    CHECK(r.refresh_needed&&!r.source_bound&&!r.waiting_valid);
    CHECK(raw_runtime_source_recovery_pending(&r));
    CHECK(!r.view.query.query_id&&!r.view.command.kind&&!r.fault);
}
static void test_transient_source_expiry_requires_new_released_binding(void){
    RawSample s=sample(200);ManualSourceBinding fresh;uint32_t counter;
    expire_test_source(&s);counter=s.counter;fresh=source(counter);
    CHECK(!raw_runtime_bind_source(&r,&fresh,1u));
    CHECK(bind_test_source(&r,&fresh,2u));poll(&s,0);
    CHECK(r.source_bound&&r.waiting_valid&&!r.refresh_needed&&!r.fault);
    CHECK(!raw_runtime_source_recovery_pending(&r));
    CHECK(r.source_generation==2u&&r.source.origin_counter==counter);
    CHECK(r.controls.active_query.query_id&&r.view.query.query_id);
    CHECK(!r.view.command.kind&&s.counter==counter&&s.guest_mask==0xffffu);
}
static void test_refresh_binding_rejects_observed_A_and_departed_prompt(void){
    RawSample s=sample(300);ManualSourceBinding fresh;
    expire_test_source(&s);
    /* Observe original scan input through the real runtime entry points,
       then release it. Released input alone cannot erase the recorded A. */
    unit(&s,CH_KEY_A);unit(&s,0);fresh=source(s.counter);
    CHECK(r.original_raw_a_seen&&r.actual_raw_press_seen&&!r.encounter_started);
    CHECK(!raw_runtime_source_recovery_pending(&r));
    CHECK(!s.host_held&&!s.host_intersection&&s.guest_mask==0xffffu);
    CHECK(!raw_runtime_bind_source(&r,&fresh,2u));
    CHECK(!r.source_bound&&!r.waiting_valid&&!r.controls.candidate_valid);
    CHECK(!r.view.query.query_id&&!r.view.command.kind&&!r.fault);
    s=sample(400);expire_test_source(&s);s.script_final_prompt=0u;
    raw_runtime_before_scan(&r,&s);poll(&s,0);fresh=source(s.counter);
    CHECK(!raw_runtime_source_recovery_pending(&r));
    CHECK(!raw_runtime_bind_source(&r,&fresh,2u));
    CHECK(!r.source_bound&&!r.waiting_valid&&!r.controls.candidate_valid);
    CHECK(!r.view.query.query_id&&!r.view.command.kind&&!r.fault);
    /* A genuine read fault retains its existing prohibition on rebinding. */
    s=sample(500);expire_test_source(&s);raw_runtime_sample_failed(&r);
    s=sample(s.counter+1u);raw_runtime_before_scan(&r,&s);poll(&s,0);fresh=source(s.counter);
    CHECK(r.fault==RAW_FAULT_READ&&!raw_runtime_bind_source(&r,&fresh,2u));
    CHECK(!raw_runtime_source_recovery_pending(&r));
    CHECK(!r.source_bound&&!r.waiting_valid&&!r.controls.candidate_valid&&!r.view.query.query_id);
}

static void test_original_frontend_menu_is_not_an_unfinished_game_scan(void) {
    RawSample s;RawHud h;uint32_t mode,gate,n;
    for(mode=0u;mode<13u;mode++)for(gate=0u;gate<3u;gate++){
        if(mode==1u&&gate!=2u)continue;
        s=sample(800);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
        raw_runtime_begin_scan(&r,&s);CHECK(r.scan_open);
        /* The native front-end can leave mode 1 after 110FA0. Its switch
           skips 10A430, so the next UI visit owns no after-scan receipt. */
        s.frontend_valid=1u;s.frontend_mode=mode;s.frontend_scan_gate=gate;
        s.script_final_prompt=0u;
        for(n=0;n<5u;n++){
            raw_runtime_before_scan(&r,&s);poll(&s,CH_KEY_L|CH_KEY_R);
            raw_runtime_begin_scan(&r,&s);
            CHECK(r.frontend_suspended&&!r.fault&&!r.scan_open&&!r.have_previous_scan);
            CHECK(r.runtime==CH_RUNTIME_RUNNING&&!r.source_bound&&!r.waiting_valid);
            CHECK(!r.controls.candidate_valid&&!r.controls.active_query.query_id);
            CHECK(!r.view.command.kind&&!r.view.query.query_id);
            CHECK(r.controls.last_counter==800u);
            raw_runtime_hud(&r,&h);CHECK(!hud_has(&h,"Unexpected game update [R04]"));
        }
        /* A normal return from the VC menu keeps its original game counter.
           It must not synthesize a held L+R chord into a new game pause. */
        s.frontend_mode=1u;s.frontend_scan_gate=0u;s.script_final_prompt=1u;
        raw_runtime_before_scan(&r,&s);poll(&s,CH_KEY_L|CH_KEY_R);
        CHECK(!r.frontend_suspended&&!r.fault&&r.runtime==CH_RUNTIME_RUNNING);
        CHECK(!r.view.command.kind);poll(&s,0);CHECK(!r.fault);
        raw_runtime_begin_scan(&r,&s);CHECK(r.scan_open);
        raw_runtime_after_scan(&r,&s);CHECK(!r.scan_open&&!r.fault);
    }
    /* A missing/incoherent menu receipt, unknown mode, or an ordinary game
       scan never grants permission to discard an unfinished scan. */
    for(n=0u;n<4u;n++){
        s=sample(900);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
        raw_runtime_begin_scan(&r,&s);s.frontend_valid=n!=0u;
        s.frontend_mode=n==2u?13u:n==3u?1u:8u;
        if(n==1u)s.sample_complete=0u;
        raw_runtime_before_scan(&r,&s);CHECK(r.fault==RAW_FAULT_ORDER);
    }
    /* Menu entry alone does not authenticate a restart: an unexplained
       counter reset still reports the original discontinuity on return. */
    s=sample(950);raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);
    s.frontend_valid=1u;s.frontend_mode=8u;s.script_final_prompt=0u;
    raw_runtime_before_scan(&r,&s);poll(&s,0);s.frontend_mode=1u;s.counter=0u;
    raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.fault==RAW_FAULT_ORDER&&r.controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
}

static void test_checked_restart_retires_only_the_verified_session(void) {
    RawSample s=sample(600);ManualPrediction stale;ch_query old_query;ManualSourceBinding b;
    uint32_t request_id,query_id;RawHud h;
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);poll(&s,0);bind_candidate(&s);
    stale=r.candidate;old_query=r.controls.active_query;
    r.encounter_started=1u;r.actual_seen=1u;r.actual_dv=0xaaaau;
    r.actual_raw_press_seen=1u;r.actual_raw_press=600u;
    r.fault=RAW_FAULT_ORDER;r.controls.fault=CH_FAULT_RUNTIME_FAILED;
    r.controls.overlay_visible=0u;r.controls.interface_locked=1u;
    request_id=r.controls.next_request_id;query_id=r.controls.next_query_id;
    CHECK(!raw_runtime_restart_checked(&r,0u,0u));
    CHECK(!raw_runtime_restart_checked(&r,77u,0u));
    CHECK(!raw_runtime_restart_checked(&r,76u,0u));
    CHECK(r.encounter_started&&r.actual_seen&&r.fault==RAW_FAULT_ORDER);
    /* Device supplies epoch 78 only after its native reset receipt and
       checked release of environment/result/mailbox ownership. */
    CHECK(raw_runtime_restart_checked(&r,78u,CH_KEY_L|CH_KEY_R));
    CHECK(r.scene_epoch==78u&&!r.fault&&!r.controls.fault&&!r.scan_open&&r.restart_bootstrap_pending);
    CHECK(!r.source_generation&&!r.source_bound&&!r.waiting_valid&&!r.search_started);
    CHECK(!r.encounter_started&&!r.actual_seen&&!r.actual_raw_press_seen);
    CHECK(!r.plan_active&&!r.plan_pending&&!r.controls.candidate_valid);
    CHECK(!r.controls.overlay_visible&&r.controls.interface_locked);
    CHECK(r.controls.next_request_id==request_id&&r.controls.next_query_id==query_id);
    CHECK(!raw_runtime_accept_candidate(&r,&old_query,&stale));
    s=sample(0);s.scene_epoch=78u;s.script_final_prompt=0u;
    raw_runtime_before_scan(&r,&s);poll(&s,CH_KEY_L|CH_KEY_R);
    CHECK(!r.fault&&!r.view.command.kind&&r.runtime==CH_RUNTIME_RUNNING);
    poll(&s,0);CHECK(!r.fault&&!r.search_requested);
    /* A checked reset retires the old session, but even its zero readback
       is not a control baseline before a complete ordinary game unit. */
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    s.counter=1u;raw_runtime_before_scan(&r,&s);poll(&s,0);CHECK(!r.restart_bootstrap_pending&&!r.fault);
    s.script_final_prompt=1u;raw_runtime_before_scan(&r,&s);poll(&s,0);
    CHECK(r.search_started&&r.search_requested&&!r.fault);
    b=source(1);b.source_epoch=78u;CHECK(bind_test_source(&r,&b,1u));poll(&s,0);
    CHECK(r.controls.active_query.query_id&&r.controls.active_query.scene_epoch==78u);
    raw_runtime_hud(&r,&h);CHECK(!hud_has(&h,"Unexpected game update [R04]"));
    /* Ownership errors and a player-owned pause cannot be cleared using a
       reset-epoch receipt: their external resources still need resolution. */
    r.runtime=CH_RUNTIME_PAUSED;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
    r.runtime=CH_RUNTIME_STEPPING;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
    r.runtime=CH_RUNTIME_RUNNING;r.display_pending=1u;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
    r.display_pending=0u;r.display_error=1u;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
    r.display_error=0u;r.fault=RAW_FAULT_DISPLAY;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
    r.fault=RAW_FAULT_ENVIRONMENT;CHECK(!raw_runtime_restart_checked(&r,79u,0u));
}

static void test_actual_waiting_job_guards(void) {
    ManualSourceBinding b=source(100);ManualCurrentBoundary c={0};
    RawJob job,bad;RawQueryCache cache={0};ManualPrediction p;
    c.counter=100;c.source_epoch=b.source_epoch;c.source_identity=b.source_identity;
    c.required_context_identity=b.required_context_identity;
    CHECK(fixture_job(&b,&c,120u,&job));
    CHECK(raw_job_solve_cached(&job,0,&cache,&p)==MANUAL_QUERY_OK);
    CHECK(p.target_counter==350u&&p.div_x==186u&&p.predicted_dv==0x6aaau);
    CHECK(p.expected_wait_add==35u&&p.expected_wait_sub==37u&&p.branch==0u);
    CHECK(!p.manual_input_validated&&p.model_n==p.press_relative+2u);
    bad=job;bad.waiting_valid=0;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)==MANUAL_QUERY_INVALID);
    bad=job;bad.waiting.bg=(bad.waiting.bg+1u)%3u;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)==MANUAL_QUERY_INVALID);
    bad=job;bad.boundary.counter=102u;bad.token.source_counter=102u;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)==MANUAL_QUERY_INVALID);
    bad=job;bad.boundary.counter=101u;bad.token.source_counter=101u;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)==MANUAL_QUERY_OK);
    CHECK(p.target_counter==350u&&p.queried_at_counter==101u);
    bad=job;bad.boundary.source_identity.bytes[0]^=1u;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)==MANUAL_QUERY_INVALID);
    bad=job;bad.waiting.clock.lcd_countdown=112u;
    CHECK(raw_job_solve_cached(&bad,0,&cache,&p)!=MANUAL_QUERY_OK);
}

int main(void) {
    test_actual_waiting_job_guards();
    test_player_control();test_conditional_original_receipts();test_missed_and_wrong_input();
    test_guards_and_recovery();test_wrap_and_tokens();test_binding_regressions();test_service_original_call_order();
    test_late_worker_future_retry();
    test_player_paused_ready_A_resumes_and_observes_release();
    test_player_leaves_prompt();
    test_source_bound_on_real_first_scheduler();
    test_noncanonical_release_hud_keeps_DV_and_certificate_separate();
    test_player_exit_display_wait_is_bounded();
    test_one_L_release_never_repeats_and_plus_two_faults();
    test_released_GS_arrival_starts_running_search();
    test_compact_hud_layout();
    test_hud_failure_codes();
    test_runtime_fault_codes();
    test_controller_counter_regression_reports_runtime_fault();
    test_transient_source_expiry_requires_new_released_binding();
    test_refresh_binding_rejects_observed_A_and_departed_prompt();
    test_original_frontend_menu_is_not_an_unfinished_game_scan();
    test_checked_restart_retires_only_the_verified_session();
    printf("passed: %u assertions, runtime/control scenarios + actual waiting job guards\n",checks);return 0;
}
