/* Included by the actual-device fixture after SDK boundary definitions. */
#include "../../rng/query_completion.h"
#define NATIVE_BASE UINT32_C(0x22f5fc)
#define IO_BASE UINT32_C(0x8a3c07c)
static uint8_t native[0x458],io[256];
static uint32_t reads,fail_at,submitted,drawn;
static RawJob copied_job;
static const CQWaitingState canonical={6,234,2,{216,39,38,39,113}};
int ch_install_startup(ChInstall *s,const ChInstallOps *o,uint32_t p,uint32_t q,const ChInstallPoint *a,uint32_t n){
    (void)s;(void)o;(void)p;(void)q;(void)a;(void)n;CHECK(0);return 0;
}
int raw_hud_3ds_original_unpainted_present(RawHud3dsSink *s,uint32_t a,uint32_t b,uint8_t *c,uint8_t *d,uint32_t e,uint32_t f){
    (void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;CHECK(0);return 0;
}
static void set32(uint32_t a,uint32_t v){uint32_t i;for(i=0;i<4;i++)native[a-NATIVE_BASE+i]=(uint8_t)(v>>(8*i));}
static int read_fixture(void *u,uint32_t a,void *out,uint32_t n){const uint8_t *p;(void)u;reads++;if(reads==fail_at)return 0;
    if(a>=NATIVE_BASE&&a-NATIVE_BASE+n<=sizeof(native))p=native+a-NATIVE_BASE;
    else if(a>=IO_BASE&&a-IO_BASE+n<=sizeof(io))p=io+a-IO_BASE;else return 0;
    memcpy(out,p,n);return 1;
}
static int copied_submit(void *u,const RawJob *job){(void)u;submitted++;copied_job=*job;return 1;}
static int copied_receive(void *u,RawJob *job,ManualPrediction *p){(void)u;(void)job;(void)p;return 0;}
static void fixture_draw(void *u,const RawHud *h){(void)u;CHECK(h->count<=12);drawn++;}
static void forbidden_wait(void *u){(void)u;CHECK(0);}
static int forbidden_write(void *u,uint32_t a,uint8_t v){(void)u;(void)a;(void)v;CHECK(0);return 0;}
static int forbidden_rtc(void *u,uint32_t v){(void)u;(void)v;CHECK(0);return 0;}
static int forbidden_publish(void *u){(void)u;CHECK(0);return 0;}
static void state_at(uint32_t counter,const CQWaitingState *state,uint32_t vblank,uint32_t mask,uint32_t host){
    fixture_sample.counter=counter;fixture_sample.guest_mask=mask;fixture_sample.host_held=host;
    fixture_sample.host_intersection=host;ch_raw_service.runtime.counter=counter;
    set32(0x22f600,state->clock.budget);set32(0x22fa48,state->clock.timer_phase);
    set32(0x22fa4c,state->clock.timer_phase+1u);set32(0x22fa50,state->clock.div_countdown);
    io[4]=(uint8_t)state->clock.div;io[128+0x1b]=(uint8_t)vblank;io[128+0x55]=(uint8_t)state->bg;
    io[128+0x61]=(uint8_t)state->a;io[128+0x62]=(uint8_t)state->s;
    CHECK(raw_platform_sample(&device.binding,&ch_raw_service.runtime.observed));
    ch_raw_service.runtime.latest=ch_raw_service.runtime.observed;
}
static void reset(void){RawServiceOps ops;ManualSourceBinding binding;RawRuntime *r=&ch_raw_service.runtime;
    memset(&device,0,sizeof(device));memset(native,0,sizeof(native));memset(io,0,sizeof(io));
    memset(&fixture_sample,0,sizeof(fixture_sample));memset(&fixture_prompt,0,sizeof(fixture_prompt));
    entered=left=restore_calls=reads=fail_at=submitted=drawn=0;
    memset(&ops,0,sizeof(ops));ops.user=&device;ops.sample=sample;ops.physical_keys=keys;
    ops.draw=fixture_draw;ops.wait_poll=forbidden_wait;ops.submit_copy=copied_submit;ops.receive_copy=copied_receive;
    ops.first_scheduler=first;CHECK(raw_service_init(&ch_raw_service,&ops));
    fixture_sample.counter_read=fixture_sample.sample_complete=fixture_sample.ordinary_supported=1;
    fixture_sample.batch=fixture_sample.input_type=fixture_sample.script_final_prompt=1;fixture_sample.scene_epoch=77;
    fixture_prompt.complete=fixture_prompt.match=1;fixture_prompt.guest_pc=0x460;fixture_prompt.guest_sp=0xc0d5;
    fixture_prompt.rom_pointer=0x8800010;fixture_prompt.wram0_pointer=0x8a2ffac;fixture_prompt.wram1_pointer=0x8a30fac;
    fixture_prompt.io_pointer=IO_BASE;fixture_prompt.hram_pointer=IO_BASE+128;
    fixture_prompt.script_mode=1;fixture_prompt.script_running=255;fixture_prompt.script_bank=0x1b;
    fixture_prompt.script_next=0x6e54;fixture_prompt.map_group=3;fixture_prompt.map_number=52;
    device.binding.source_epoch=77;device.binding.read=(ChReadOps){0,read_fixture,0};
    device.environment.ops.read=device.binding.read;device.environment.ops.write_data_byte=forbidden_write;
    device.environment.ops.write_rtc_word=forbidden_rtc;device.environment.ops.publish_rtc=forbidden_publish;
    device.environment.state.initial_applied=device.environment.state.preparation_applied=device.environment.state.rtc_owned=1;
    device.environment.state.epoch=77;device.environment.state.origin_counter=900;device.environment.state.source_bg=2;
    device.source_available=1;set32(0x22f5fc,0xc0d50460);set32(0x22f608,1);set32(0x22f60c,1);
    set32(0x22f6d8,IO_BASE+128);set32(0x22f6dc,IO_BASE);
    set32(0x22f6f0,113);set32(0x22f6f4,51);set32(0x22f6f8,114);set32(0x22f6fc,20);set32(0x22f700,43);
    set32(0x22f738,1);set32(0x22f73c,0);set32(0x22f75c,1);
    io[7]=4;io[15]=1;io[255]=15;io[0x41]=9;io[0x44]=144;
    r->scene_epoch=77;r->source_generation=1;r->source.abi=MANUAL_PREDICTION_ABI;r->source.source_epoch=77;
    r->source.origin_counter=900;r->source.rng_add=6;r->source.rng_sub=234;
    memset(r->source.source_identity.bytes,0x61,32);r->source.required_context_identity=manual_model_certificate()->clock_derivation;
    r->source_bound=r->search_started=r->counter_valid=r->unit_supported=1;r->source_bg=2;
    r->source_input_type=1;r->source_batch=1;r->source_phase=0;r->guest_a_initialized=1;
    state_at(900,&canonical,64,0xffff,0);binding=r->source;
    CHECK(observe_waiting(&device,&binding));CHECK(r->waiting_valid&&device.waiting_observation_valid);reads=0;
}
static void candidate_token(void){RawRuntime *r=&ch_raw_service.runtime;
    r->controls.candidate_valid=1;r->controls.target_counter=2900;r->controls.prediction=CH_PREDICTION_FUTURE;
    r->controls.active_query.query_id=99;r->controls.active_query.source_counter=900;
    r->controls.active_query.scene_epoch=77;r->controls.active_query.source_generation=1;
    r->view.query=r->controls.active_query;r->raw_target=2900;r->plan_active=r->plan_pending=1;
}
static void route_first(void){ChNativeContext c;memset(&c,0,sizeof(c));c.lr=0x1a8340;
    ch_raw_service.pending_marker=1;CHECK(raw_service_route(&ch_raw_service,&c)==0x1aae60);
    CHECK(ch_raw_service.runtime.runtime==CH_RUNTIME_RUNNING);
}
static void safe_waiting(void){RawRuntime *r=&ch_raw_service.runtime;
    CHECK(!r->fault&&r->source_bound&&device.environment.state.rtc_owned);
    CHECK(!device.environment.cleanup_attempted&&!device.environment.terminal_attempted);
    CHECK(!entered&&!left&&!restore_calls&&r->runtime==CH_RUNTIME_RUNNING);
}
static void invalidated(void){RawRuntime *r=&ch_raw_service.runtime;safe_waiting();
    CHECK(!r->controls.candidate_valid&&!r->plan_active&&!r->plan_pending);
    CHECK(!r->controls.active_query.query_id&&!r->view.query.query_id&&r->controls.need_query);
}
static void upper_select_release_query(void){static const uint32_t masks[]={0xfdff,0xfeff,0xfcff};
    RawRuntime *r=&ch_raw_service.runtime;CQWaitingState next=canonical;uint32_t i;ch_query old;
    RawQueryCache cache;ManualPrediction prediction;ChNativeContext gate;
    reset();candidate_token();old=r->controls.active_query;
    for(i=0;i<3;i++){CHECK(cq_waiting_next(&next));state_at(901+i,&next,65+i,masks[i],(~masks[i])&0xff00);
        route_first();safe_waiting();CHECK(r->waiting_valid&&r->controls.candidate_valid&&!device.waiting_diagnostic.failures);
        CHECK(!memcmp(&r->waiting,&next,sizeof(next)));}
    CHECK(cq_waiting_next(&next));state_at(904,&next,68,0xfffb,4);route_first();invalidated();
    CHECK(!r->waiting_valid&&device.waiting_diagnostic.stage==WAIT_DIAG_ALIGNMENT);
    CHECK(!ch_controller_accept_candidate(&r->controls,&old,2900,0x6aaa));
    CHECK(cq_waiting_next(&next));next.a=99;next.s=101;next.clock.div=100;
    state_at(905,&next,69,0xffff,0);route_first();safe_waiting();
    CHECK(r->waiting_valid&&r->waiting_counter==905&&!memcmp(&r->waiting,&next,sizeof(next)));
    CHECK(!r->controls.candidate_valid&&r->controls.need_query&&!r->refresh_needed);
    memset(&gate,0,sizeof(gate));gate.lr=0x1042f4;CHECK(raw_service_route(&ch_raw_service,&gate)==0x110fa0);
    CHECK(submitted==1&&drawn==1&&copied_job.waiting_valid&&copied_job.waiting_counter==905);
    CHECK(!memcmp(&copied_job.waiting,&next,sizeof(next))&&copied_job.token.source_counter==905);
    memset(&cache,0,sizeof(cache));CHECK(raw_job_solve_cached(&copied_job,0,&cache,&prediction)==MANUAL_QUERY_OK);
    CHECK(raw_query_complete(r,&copied_job,&prediction)==RAW_QUERY_ACCEPTED);
    CHECK(r->controls.candidate_valid&&(r->plan_active||r->plan_pending)&&!r->fault);
    CHECK(!ch_controller_accept_candidate(&r->controls,&old,2900,0x6aaa));
}
static void mismatch_and_read(void){CQWaitingState next;RawRuntime *r=&ch_raw_service.runtime;uint32_t kind;
    for(kind=0;kind<2;kind++){reset();candidate_token();next=canonical;CHECK(cq_waiting_next(&next));
        if(kind==0)next.a^=1u;else next.clock.div^=1u;
        state_at(901,&next,65,0xffff,0);route_first();invalidated();
        CHECK(r->waiting_valid&&!memcmp(&r->waiting,&next,sizeof(next))&&!r->refresh_needed);
        CHECK(device.waiting_diagnostic.stage==WAIT_DIAG_STATE&&device.waiting_diagnostic.failures==1);}
    reset();candidate_token();next=canonical;CHECK(cq_waiting_next(&next));state_at(901,&next,65,0xffff,0);
    reads=0;fail_at=7;route_first();invalidated();CHECK(!r->waiting_valid);
    CHECK(device.waiting_read_diagnostic.stage==SOURCE_ALIGNED_READ&&device.waiting_read_diagnostic.read_ordinal==7);
    reset();candidate_token();state_at(901,&next,65,0xffff,0);set32(0x22f6f0,112);route_first();invalidated();
    CHECK(!r->waiting_valid&&device.waiting_read_diagnostic.domain_guard==7);
    reset();route_first();route_first();safe_waiting();CHECK(r->waiting_counter==900&&!memcmp(&r->waiting,&canonical,sizeof(canonical)));
    CHECK(!device.waiting_diagnostic.failures);
}
static void fresh_and_A_strict(void){ManualSourceBinding fresh;RawRuntime *r=&ch_raw_service.runtime;
    reset();fresh=r->source;device.binding.last_boundary.guest_active_low_mask=0xfffb;
    CHECK(!observe_waiting(&device,&fresh));CHECK(!device.waiting_diagnostic.failures);
    reset();r->encounter_started=1;r->plan_failed=1;r->source_bound=0;route_first();
    CHECK(r->fault==RAW_FAULT_ENVIRONMENT&&device.environment.terminal_attempted);
    CHECK(device.environment.last_status==RAW_ENV_REJECTED&&entered==1&&left==1&&reads==0);
    CHECK(!device.waiting_diagnostic.failures);
}
int main(void){upper_select_release_query();mismatch_and_read();fresh_and_A_strict();
    printf("passed: %u checks; actual device first/observer/service/runtime; L/R upper masks, Select invalidation and copied actual-seed query accepted, RNG/clock mismatch, read/domain rejection, duplicate counter, fresh strict and actual-A environment failure\n",checks);return 0;}
