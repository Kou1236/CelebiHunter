/* Included by the actual-device fixture after SDK boundary definitions. */
#include "../../rng/query_completion.h"
#define NATIVE_BASE UINT32_C(0x22f5fc)
#define IO_BASE UINT32_C(0x8a3c07c)
static uint8_t native[0x458],io[256];
static uint32_t reads,fail_at,paired_drift_at,submitted,drawn,received;
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
    memcpy(out,p,n);if(reads==paired_drift_at)((uint8_t *)out)[0]^=1u;return 1;
}
static int copied_submit(void *u,const RawJob *job){(void)u;submitted++;copied_job=*job;return 1;}
static int copied_receive(void *u,RawJob *job,ManualPrediction *p){(void)u;(void)job;(void)p;received++;return 0;}
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
    entered=left=restore_calls=reads=fail_at=paired_drift_at=submitted=drawn=received=0;
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
    r->view.query=r->controls.active_query;r->raw_target=2900;r->plan_active=0;r->plan_pending=1;
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
    CHECK(!r->waiting_valid&&device.waiting_diagnostic.stage==RAW_WAIT_DIAG_ALIGNMENT);
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
        CHECK(device.waiting_diagnostic.stage==RAW_WAIT_DIAG_STATE&&device.waiting_diagnostic.failures==1);}
    reset();candidate_token();next=canonical;CHECK(cq_waiting_next(&next));state_at(901,&next,65,0xffff,0);
    reads=0;fail_at=7;route_first();safe_waiting();CHECK(!r->waiting_valid&&r->waiting_recheck);
    CHECK(r->controls.candidate_valid&&r->raw_target==2900&&r->plan_pending);
    CHECK(device.waiting_read_diagnostic.stage==SOURCE_ALIGNED_READ&&device.waiting_read_diagnostic.read_ordinal==7);
    reset();candidate_token();state_at(901,&next,65,0xffff,0);set32(0x22f6f0,112);route_first();safe_waiting();
    CHECK(!r->waiting_valid&&r->waiting_recheck&&r->controls.candidate_valid&&device.waiting_read_diagnostic.domain_guard==7);
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
static uint8_t compat_cpu[34];
static void store_compat_word(uint8_t *bytes,uint32_t offset,uint32_t value){uint32_t i;
    for(i=0;i<4;i++)bytes[offset+i]=(uint8_t)(value>>(8u*i));}
static int compat_cpu_read(void *u,uint32_t address,void *out,uint32_t size){
    uint32_t offset;(void)u;
    if(address==0x22f5fcu&&size==4u)offset=0;
    else if(address==0x22f604u&&size==20u)offset=4;
    else if(address==0x22f62cu&&size==8u)offset=24;
    else if(address==0x22f764u&&size==1u)offset=32;
    else if(address==0x230f7du&&size==1u)offset=33;
    else return 0;
    memcpy(out,compat_cpu+offset,size);return 1;
}
static void compatibility_save_inputs(void){ChReadOps ops={&device,compat_cpu_read,0};
    uint8_t start[4],rtc[5];uint32_t day,hour,minute,second,carry_second,carry_minute,carry_hour;
    memset(compat_cpu,0xa5,sizeof(compat_cpu));
    compat_cpu[0]=0x60;compat_cpu[1]=0x04;compat_cpu[2]=0xd5;compat_cpu[3]=0xc0;
    store_compat_word(compat_cpu,4,37);store_compat_word(compat_cpu,8,1);store_compat_word(compat_cpu,12,1);
    compat_cpu[32]=0;compat_cpu[33]=0;
    CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MATCH);
    /* The live CPU word at 0x22f604 is not DIV. It may vary freely; only the
       modeled call site and documented structural invariants stay fixed. */
    store_compat_word(compat_cpu,4,0xffffffffu);compat_cpu[16]=0x12;compat_cpu[24]=0x67;
    CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MATCH);
    store_compat_word(compat_cpu,4,0);CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MATCH);
    store_compat_word(compat_cpu,4,65);CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MATCH);
    store_compat_word(compat_cpu,4,37);compat_cpu[33]=1;
    CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MISMATCH);compat_cpu[33]=0;
    compat_cpu[0]=0x61;CHECK(raw_environment_initial_cpu_check(&ops)==RAW_ENV_CPU_MISMATCH);compat_cpu[0]=0x60;
    /* Exhaust the API's field domain. The original FixTime day uses ADC
       without a final modulo; offsets that cannot yield day 72 must reject.
       All ordinary initialization weekdays 0..6 remain representable. */
    for(day=0;day<140u;day++)for(hour=0;hour<24u;hour++)
        for(minute=0;minute<60u;minute++)for(second=0;second<60u;second++){
            start[0]=(uint8_t)day;start[1]=(uint8_t)hour;start[2]=(uint8_t)minute;start[3]=(uint8_t)second;
            carry_second=second>7u;
            carry_minute=minute+carry_second>26u;
            carry_hour=hour+carry_minute>20u;
            if(day+carry_hour>72u){
                if(raw_environment_derive_preparation_rtc(start,rtc)){
                    fprintf(stderr,"unrepresentable StartTime admitted: %u/%u/%u/%u\n",day,hour,minute,second);exit(1);}
                checks++;continue;
            }
            if(!raw_environment_derive_preparation_rtc(start,rtc)){
                fprintf(stderr,"invalid accepted StartTime: %u/%u/%u/%u\n",day,hour,minute,second);exit(1);}
            carry_second=(second+rtc[0])>=60u;
            carry_minute=(minute+rtc[1]+carry_second)>=60u;
            carry_hour=(hour+rtc[2]+carry_minute)>=24u;
            if((second+rtc[0])%60u!=7u||(minute+rtc[1]+carry_second)%60u!=26u||
               (hour+rtc[2]+carry_minute)%24u!=20u||((day+rtc[3]+carry_hour)&255u)!=72u||rtc[3]>=140u||rtc[4]!=0u){
                fprintf(stderr,"bad RTC derivation: %u/%u/%u/%u\n",day,hour,minute,second);exit(1);}
            checks++;
        }
    start[0]=140;start[1]=start[2]=start[3]=0;CHECK(!raw_environment_derive_preparation_rtc(start,rtc));
    start[0]=0;start[1]=24;CHECK(!raw_environment_derive_preparation_rtc(start,rtc));
    start[1]=0;start[2]=60;CHECK(!raw_environment_derive_preparation_rtc(start,rtc));
    start[2]=0;start[3]=60;CHECK(!raw_environment_derive_preparation_rtc(start,rtc));
}
static void compatibility_environment_layout(void){uint32_t p0=0x8a4418cu,p3=0x8a2ffacu,p6=IO_BASE;
    set32(0x22f640,p0);set32(0x22f644,p0+8u);set32(0x22f698,0x27be7cu);
    set32(0x22f6c8,p3);set32(0x22f6d4,0x8a31000u);set32(0x22f6d8,p6+128u);
    set32(0x22f6dc,p6);set32(0x22f768,p3+4096u);
    CHECK(raw_environment_data_address(&device.binding.read,p0));
    CHECK(raw_environment_data_address(&device.binding.read,p3+0x14c6u));
    CHECK(raw_environment_data_address(&device.binding.read,p6+5u));
    CHECK(raw_environment_data_range(&device.binding.read,p0,5u));
    CHECK(raw_environment_data_range(&device.binding.read,p3+0x14c6u,3u));
    CHECK(!raw_environment_data_range(&device.binding.read,p3+0x14c6u,4u));
    CHECK(!raw_environment_data_range(&device.binding.read,p6+4u,2u));
    CHECK(!raw_environment_data_address(&device.binding.read,p6+4u));
    /* A different process mapping with the same validated relationships must
       admit the same typed rows, while stale addresses cease to be writable. */
    p0+=0x1000u;p3+=0x10000u;p6+=0x2000u;
    set32(0x22f640,p0);set32(0x22f644,p0+8u);set32(0x22f6c8,p3);
    set32(0x22f6d8,p6+128u);set32(0x22f6dc,p6);set32(0x22f768,p3+4096u);
    CHECK(raw_environment_data_address(&device.binding.read,p0));
    CHECK(raw_environment_data_address(&device.binding.read,p3+0x14c6u));
    CHECK(raw_environment_data_address(&device.binding.read,p6+5u));
    CHECK(raw_environment_data_range(&device.binding.read,p0,5u));
    CHECK(raw_environment_data_range(&device.binding.read,p3+0x14c6u,3u));
    CHECK(!raw_environment_data_range(&device.binding.read,p6+4u,2u));
    CHECK(!raw_environment_data_address(&device.binding.read,p6+4u));
    CHECK(!raw_environment_data_address(&device.binding.read,0x8a4418cu));
    CHECK(!raw_environment_data_address(&device.binding.read,0x8a31472u));
    set32(0x22f644,p0+12u);
    CHECK(!raw_environment_data_address(&device.binding.read,p0));
}
static int diagnostic_contains(const RawHud *h,const char *text){uint32_t i;
    for(i=0;i<h->count;i++)if(strstr(h->line[i],text))return 1;
    return 0;
}
static void diagnostic_rows_fit(const RawHud *h){uint32_t i;
    CHECK(h->count<=12u);
    for(i=0;i<h->count;i++){
        CHECK(memchr(h->line[i],0,sizeof(h->line[i]))!=0);
        CHECK(strlen(h->line[i])<=50u);
    }
}
static void source_diagnostic_hud(void){RawHud h;RawRuntime *r=&ch_raw_service.runtime;
    RawDeviceSourceDiagnostic *s=&device.source_diagnostic;uint32_t base,kind;
    static const uint32_t reasons[]={5u,6u,7u,8u};
    static const char *codes[]={"Check S01/U05:","Check S01/U06:","Check S01/U07:","Check S01/U08:"};
    /* A successful admission closes the one-shot write attempt. That closure
       must not be presented as an acquisition failure beside a valid target. */
    reset();candidate_token();s->closed=1u;s->phase=RAW_SOURCE_ADMITTED;
    raw_runtime_hud(r,&h);base=h.count;diagnose_hud(&device,r,&h);
    CHECK(h.count==base&&!diagnostic_contains(&h,"source acquisition stopped"));
    /* The actual sampler returns UNSUPPORTED for these concrete mismatches;
       use its status and verify that the formatter preserves the subcause. */
    for(kind=0;kind<4u;kind++){
        reset();device.source_available=0;device.source_requested=1;r->source_bound=0;
        memset(s,0,sizeof(*s));s->phase=RAW_SOURCE_WAIT_UNIT;
        s->sample_status=(uint32_t)CH_SAMPLE_UNSUPPORTED;s->counter_valid=1u;
        s->engine_type=s->batch_count=1u;
        if(kind==0)s->batch_count=2u;else if(kind==1)s->host_phase=1u;
        else if(kind==2)s->engine_type=2u;else s->input_enable=1u;
        CHECK(unit_reason(s)==reasons[kind]);
        memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
        CHECK(diagnostic_contains(&h,codes[kind]));
        CHECK(diagnostic_contains(&h,"t=")&&diagnostic_contains(&h," b=")&&
            diagnostic_contains(&h," p=")&&diagnostic_contains(&h," en="));
        diagnostic_rows_fit(&h);
    }
    /* Read/coherence faults retain precedence over partly populated fields. */
    s->sample_status=(uint32_t)CH_SAMPLE_READ_FAILED;CHECK(unit_reason(s)==1u);
    s->sample_status=(uint32_t)CH_SAMPLE_INCOHERENT;CHECK(unit_reason(s)==2u);
    reset();device.source_available=0;device.source_requested=1;r->source_bound=0;
    s->phase=RAW_SOURCE_WAIT_INPUT;s->host_held=s->host_intersection=1u;s->guest_mask=0xffffu;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S02/I01:"));
    CHECK(diagnostic_contains(&h,"held=0001 intersection=0001 guest=FFFF"));diagnostic_rows_fit(&h);
    s->phase=RAW_SOURCE_WAIT_CPU;s->guest_pc=0x461u;s->guest_sp=0xc0d5u;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S03:"));
    CHECK(diagnostic_contains(&h,"pc=0461 sp=C0D5"));diagnostic_rows_fit(&h);
    /* The row budget remains bounded when the base HUD already uses seven
       rows, as it does after a runtime failure. */
    memset(&h,0,sizeof(h));h.count=7u;add_source_diag(&device,r,&h);diagnostic_rows_fit(&h);
    memset(&h,0,sizeof(h));h.count=12u;add_source_diag(&device,r,&h);CHECK(h.count==12u);
}
static void waiting_diagnostic_codes(void){RawHud h;RawRuntime *r=&ch_raw_service.runtime;
    RawWaitingDiagnostic *w=&device.waiting_diagnostic;uint32_t guard;char code[24];
    reset();r->waiting_valid=0;w->failures=1u;w->stage=RAW_WAIT_DIAG_ALIGNMENT;
    w->aligned.stage=SOURCE_ALIGNED_BOUNDARY;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check W01/B:")&&!diagnostic_contains(&h,"Check T01:"));
    w->aligned.stage=SOURCE_ALIGNED_DRIFT;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check W01/D:")&&!diagnostic_contains(&h,"Check T03:"));
    for(guard=1u;guard<=16u;guard++){
        w->aligned.stage=SOURCE_ALIGNED_DOMAIN;w->aligned.domain_guard=guard;
        memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
        snprintf(code,sizeof(code),"Check T%02u:",guard);
        CHECK(diagnostic_contains(&h,code));
    }
    w->aligned.stage=SOURCE_ALIGNED_READ;w->aligned.read_ordinal=26u;
    w->aligned.read_pass=2u;w->aligned.read_address=0xffffffffu;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check M26/P2@FFFFFFFF:"));
}
static void fresh_diagnostic_retention(void){RawHud h;ManualSourceBinding fresh;
    RawDeviceSourceDiagnostic retained;RawRuntime *r=&ch_raw_service.runtime;
    reset();fresh=r->source;reads=0;fail_at=7u;
    CHECK(!observe_waiting(&device,&fresh));
    CHECK(!device.waiting_diagnostic.failures&&r->waiting_valid&&!r->fault);
    device.worker=(Thread)(uintptr_t)1u;
    CHECK(raw_device_source_diagnostic_copy(&retained));
    CHECK(retained.waiting_stage==RAW_WAIT_DIAG_ALIGNMENT);
    CHECK(retained.waiting_alignment_stage==SOURCE_ALIGNED_READ&&retained.waiting_alignment.raw_valid);
    CHECK(retained.waiting_read_ordinal==7u&&retained.waiting_read_pass==1u);
    CHECK(retained.waiting_read_address==IO_BASE+4u&&retained.waiting_read_size==4u);
    CHECK(!memcmp(&retained.waiting_alignment,&device.waiting_read_diagnostic,sizeof(retained.waiting_alignment)));
    CHECK(stop_source(&device,RAW_SOURCE_STOP_VERIFY)<0);r->source_bound=0;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S14/M7/P1@08A3C080:"));diagnostic_rows_fit(&h);
    reset();fresh=r->source;set32(0x22f6f0,112u);
    CHECK(!observe_waiting(&device,&fresh));device.worker=(Thread)(uintptr_t)1u;
    CHECK(raw_device_source_diagnostic_copy(&retained));
    CHECK(retained.waiting_alignment_stage==SOURCE_ALIGNED_DOMAIN&&retained.waiting_guard==7u);
    CHECK(retained.waiting_alignment.raw_valid&&retained.waiting_alignment.first.lcd[0]==112u);
    CHECK(stop_source(&device,RAW_SOURCE_STOP_VERIFY)<0);r->source_bound=0;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S14/T07:"));diagnostic_rows_fit(&h);
    reset();fresh=r->source;fresh.rng_add^=1u;
    CHECK(!observe_waiting(&device,&fresh));device.worker=(Thread)(uintptr_t)1u;
    CHECK(raw_device_source_diagnostic_copy(&retained));
    CHECK(retained.waiting_stage==RAW_WAIT_DIAG_BINDING);
    CHECK(retained.waiting_expected.a==fresh.rng_add&&retained.waiting_actual.a==r->source.rng_add);
    CHECK(stop_source(&device,RAW_SOURCE_STOP_VERIFY)<0);r->source_bound=0;
    memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S14/W02:"));diagnostic_rows_fit(&h);
}
static int recovery_read(void *u,uint32_t a,void *out,uint32_t n){
    if(a==0x230f7du&&n==1u){*(uint8_t *)out=0;return 1;}
    return read_fixture(u,a,out,n);
}
static void recovery_diagnostics(void){RawRuntime *r=&ch_raw_service.runtime;
    RawHud h;ManualSourceBinding fresh;uint64_t generation;ChNativeContext c;uint32_t kind;
    memset(&c,0,sizeof(c));c.lr=0x1a8340u;
    for(kind=0;kind<2u;kind++){
        reset();r->source_bound=r->waiting_valid=0;r->refresh_needed=1;
        device.source_diagnostic.closed=1;device.source_diagnostic.phase=RAW_SOURCE_ADMITTED;
        CHECK(raw_runtime_source_recovery_pending(r));
        memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);
        CHECK(diagnostic_contains(&h,"Check S18:"));diagnostic_rows_fit(&h);
        device.binding.read.read_bytes=recovery_read;
        /* Run the actual coordinator's negative refresh path. Native
           ownership and snapshot transport remain SDK fixture endpoints. */
        backend_ok=kind?1:0;read_ok=0;post_read_ok=1;restore_status=RAW_ENV_OK;
        CHECK(source(&device,&c,&fresh,&generation)<0);
        CHECK(device.source_diagnostic.closed);
        CHECK(device.source_diagnostic.phase==(kind?RAW_SOURCE_STOP_VERIFY:RAW_SOURCE_STOP_OWNERSHIP));
        CHECK(!r->source_bound&&!r->controls.candidate_valid&&!r->fault);
        memset(&h,0,sizeof(h));add_source_diag(&device,r,&h);diagnostic_rows_fit(&h);
        CHECK(diagnostic_contains(&h,kind?"Check S14/L:":"Check S09:"));
        if(kind)CHECK(device.environment.cleanup_attempted&&!device.environment.state.rtc_owned&&restore_calls==1);
        else CHECK(!device.environment.cleanup_attempted&&device.environment.state.rtc_owned&&!restore_calls);
    }
    backend_ok=read_ok=post_read_ok=1;
}
static uint32_t hud_red_rows(const RawHud *h){uint32_t i,n=0;
    for(i=0;i<h->count;i++)if(h->color[i]==0xff9090u)n++;
    return n;
}
static void waiting_recheck_severity(void){RawRuntime *r=&ch_raw_service.runtime;
    CQWaitingState next=canonical;RawHud h;uint32_t i;
    reset();candidate_token();
    for(i=0;i<RAW_WAIT_UNAVAILABLE_ADVANCES;i++){
        CHECK(cq_waiting_next(&next));state_at(901+i,&next,65+i,0xffff,0);
        set32(0x22f6f0,112);route_first();
        CHECK(!r->fault&&r->source_bound&&!r->waiting_valid);
        CHECK(r->controls.candidate_valid==(i+1u<RAW_WAIT_UNAVAILABLE_ADVANCES));
        CHECK(r->waiting_recheck==(i+1u<RAW_WAIT_UNAVAILABLE_ADVANCES));
        raw_runtime_hud(r,&h);diagnose_hud(&device,r,&h);
        if(i+1u<RAW_WAIT_UNAVAILABLE_ADVANCES){
            CHECK(!hud_red_rows(&h));
        }else CHECK(!hud_red_rows(&h)&&!diagnostic_contains(&h,"Check T07:"));
        CHECK(device.source_diagnostic.waiting_alignment.domain_guard==7u);
        CHECK(device.source_diagnostic.waiting_failures==i+1u);
    }
    set32(0x22f6f0,113);CHECK(cq_waiting_next(&next));state_at(905,&next,69,0xffff,0);route_first();
    CHECK(r->waiting_valid&&!device.source_diagnostic.waiting_unavailable_failures);
    raw_runtime_hud(r,&h);diagnose_hud(&device,r,&h);CHECK(!hud_red_rows(&h));
    CHECK(device.source_diagnostic.waiting_failures==RAW_WAIT_UNAVAILABLE_ADVANCES);
}
static void initial_retry_result(void){
    memset(&fixture_result,0,sizeof(fixture_result));fixture_result.abi=CH_RESULT_ABI;
    fixture_result.rom=0x10000000u;fixture_result.wram=0x11000000u;
    fixture_result.io=IO_BASE;fixture_result.engine=0x16000000u;
    fixture_result.map[0]=3;fixture_result.map[1]=52;
    fixture_result.script[0]=1;fixture_result.script[1]=255;fixture_result.script[2]=0x1b;
    fixture_result.script[3]=0x54;fixture_result.script[4]=0x6e;fixture_result.applied_mask=0xffff;
    memcpy(fixture_result.path,ch_result_expected_path,CH_RESULT_PATH_BYTES);
}
static void readonly_retry_budgets(void){RawRuntime *r=&ch_raw_service.runtime;
    ManualSourceBinding fresh;uint64_t generation;ChNativeContext c;uint32_t i,reads_before,kind;
    memset(&c,0,sizeof(c));c.lr=0x1a8340u;
    reset();initial_retry_result();backend_ok=read_ok=post_read_ok=1;
    device.source_available=0;device.source_requested=1;
    device.environment.state.initial_applied=0;device.environment.state.preparation_counter=899;
    device.environment.state.origin_counter=0;device.environment.generation=0;
    device.binding.read.read_bytes=recovery_read;device.environment.ops.read=device.binding.read;
    r->source_bound=r->waiting_valid=0;r->source_generation=0;r->refresh_needed=0;
    for(i=0;i<RAW_SOURCE_READONLY_RETRIES;i++){
        fixture_sample.counter=900+i*RAW_SOURCE_RETRY_ADVANCES;
        CHECK(raw_platform_sample(&device.binding,&r->observed));
        CHECK(raw_environment_initial_cpu_check(&device.binding.read)==RAW_ENV_CPU_MATCH);
        /* The full native capture fails before any write: the fixture has no
           engine-memory mapping, while the result/quick CPU gates are valid. */
        CHECK(source(&device,&c,&fresh,&generation)==(i+1u<RAW_SOURCE_READONLY_RETRIES?0:-1));
        CHECK(!device.environment.state.initial_applied&&!device.environment.state.poisoned);
        CHECK(device.source_diagnostic.initial_readonly_retries==i+1u);
        if(i+1u<RAW_SOURCE_READONLY_RETRIES){
            CHECK(!device.source_diagnostic.closed&&!device.environment.cleanup_attempted&&device.environment.state.rtc_owned);
            reads_before=snapshot_reads;fixture_sample.counter++;
            CHECK(raw_platform_sample(&device.binding,&r->observed));
            CHECK(source(&device,&c,&fresh,&generation)==0&&snapshot_reads==reads_before);
        }
    }
    CHECK(device.source_diagnostic.closed&&device.environment.cleanup_attempted&&restore_calls==1);
    for(kind=0;kind<2u;kind++){
        reset();device.source_available=0;device.source_requested=1;
        device.environment.state.initial_applied=0;
        device.environment.last_status=kind?RAW_ENV_POISONED:RAW_ENV_STORE_FAILED;
        device.environment.state.poisoned=kind;
        r->source_bound=r->waiting_valid=0;r->refresh_needed=0;
        CHECK(source(&device,&c,&fresh,&generation)<0);
        CHECK(!device.source_diagnostic.initial_readonly_retries&&device.source_diagnostic.closed);
        CHECK(device.source_diagnostic.phase==(kind?RAW_SOURCE_STOP_POISON:RAW_SOURCE_STOP_STORE));
        CHECK(device.environment.cleanup_attempted&&restore_calls==1);
    }
    for(kind=0;kind<2u;kind++){
        reset();device.binding.read.read_bytes=recovery_read;
        r->source_bound=r->waiting_valid=0;r->refresh_needed=1;
        device.source_diagnostic.closed=1;device.source_diagnostic.phase=RAW_SOURCE_ADMITTED;
        result_trace_failure=kind==0;result_trace_drift=kind==1;
        for(i=0;i<RAW_SOURCE_READONLY_RETRIES;i++){
            fixture_sample.counter=900+i*RAW_SOURCE_RETRY_ADVANCES;
            CHECK(raw_platform_sample(&device.binding,&r->observed));
            CHECK(source(&device,&c,&fresh,&generation)==(i+1u<RAW_SOURCE_READONLY_RETRIES?0:-1));
            CHECK(device.source_diagnostic.refresh_readonly_retries==i+1u);
            if(i+1u<RAW_SOURCE_READONLY_RETRIES){
                CHECK(device.environment.state.rtc_owned&&!device.environment.cleanup_attempted&&!restore_calls);
                CHECK(!r->source_bound&&!r->waiting_valid&&raw_runtime_source_recovery_pending(r));
                reads_before=snapshot_reads;fixture_sample.counter++;
                CHECK(raw_platform_sample(&device.binding,&r->observed));
                CHECK(source(&device,&c,&fresh,&generation)==0&&snapshot_reads==reads_before);
            }
        }
        CHECK(device.source_diagnostic.phase==RAW_SOURCE_STOP_VERIFY&&device.environment.cleanup_attempted&&restore_calls==1);
        CHECK(!device.environment.state.rtc_owned&&!device.environment.state.poisoned);
        result_trace_failure=result_trace_drift=0;
    }
    backend_ok=read_ok=post_read_ok=1;
}
static void real_waiting_job(RawJob *job,ManualPrediction *prediction){
    RawRuntime *r=&ch_raw_service.runtime;RawQueryCache cache;
    raw_runtime_poll_player(r,0,&r->observed);CHECK(r->view.query.query_id);
    memset(job,0,sizeof(*job));job->token=r->view.query;job->source=r->source;
    job->boundary.counter=r->counter;job->boundary.source_epoch=r->source.source_epoch;
    job->boundary.source_identity=r->source.source_identity;
    job->boundary.required_context_identity=r->source.required_context_identity;
    job->waiting=r->waiting;job->waiting_counter=r->waiting_counter;job->waiting_valid=1;
    job->minimum_player_lead=RAW_MINIMUM_PLAYER_LEAD;job->source_bg=r->source_bg;
    memset(&cache,0,sizeof(cache));CHECK(raw_job_solve_cached(job,0,&cache,prediction)==MANUAL_QUERY_OK);
    memset(&r->view.query,0,sizeof(r->view.query));
}
static void near_real_target(CQWaitingState *state){
    RawRuntime *r=&ch_raw_service.runtime;RawJob job;ManualPrediction prediction;uint32_t counter;
    reset();real_waiting_job(&job,&prediction);CHECK(raw_query_complete(r,&job,&prediction)==RAW_QUERY_ACCEPTED);
    *state=canonical;CHECK(r->plan_pending&&!r->plan_active);
    while(r->raw_target-r->counter>RAW_INPUT_PLAN_WINDOW){
        counter=r->counter+1u;CHECK(cq_waiting_next(state));
        state_at(counter,state,(64u+counter-900u)&255u,0xffff,0);route_first();
        raw_runtime_poll_player(r,0,&r->observed);CHECK(r->controls.candidate_valid&&!r->fault);
    }
    raw_runtime_before_scan(r,&r->observed);CHECK(r->plan_active&&!r->plan_pending&&!r->fault);
    raw_runtime_begin_scan(r,&r->observed);raw_runtime_after_scan(r,&r->observed);
    raw_runtime_engine_entry(r,&r->observed);route_first();
}
static void actual_released_unit(CQWaitingState *state,uint32_t fail){
    RawRuntime *r=&ch_raw_service.runtime;uint32_t before=r->counter;
    CHECK(cq_waiting_next(state));state_at(before+1u,state,(65u+before-900u)&255u,0xffff,0);
    r->counter=before;raw_runtime_before_scan(r,&r->observed);raw_runtime_poll_player(r,0,&r->observed);
    CHECK(!r->fault&&r->controls.candidate_valid);
    raw_runtime_begin_scan(r,&r->observed);raw_runtime_after_scan(r,&r->observed);
    raw_runtime_engine_entry(r,&r->observed);reads=0;
    fail_at=fail==UINT32_MAX?0:fail;paired_drift_at=fail==UINT32_MAX?26u:0;
    route_first();fail_at=paired_drift_at=0;
}
static void waiting_target_recovery(void){
    RawRuntime *r=&ch_raw_service.runtime;CQWaitingState state;ManualPrediction immutable;
    uint32_t target,anchor,log_count,i;RawHud h;
    near_real_target(&state);target=r->raw_target;anchor=r->waiting_counter;
    immutable=r->candidate;log_count=r->input_plan.log_count;
    for(i=0;i<3u;i++){
        actual_released_unit(&state,i==1u?UINT32_MAX:7u);CHECK(r->waiting_recheck&&!r->waiting_valid);
        CHECK(device.waiting_read_diagnostic.stage==(i==1u?SOURCE_ALIGNED_DRIFT:SOURCE_ALIGNED_READ));
        CHECK(r->waiting_counter==anchor&&r->raw_target==target&&r->controls.candidate_valid);
        CHECK(!memcmp(&r->candidate,&immutable,sizeof(immutable))&&r->plan_active&&!r->plan_failed);
        CHECK(r->input_plan.current_counter==r->counter&&r->input_plan.log_count>log_count);
        raw_runtime_poll_player(r,0,&r->observed);CHECK(r->controls.candidate_valid&&!r->view.query.query_id);
        raw_runtime_hud(r,&h);CHECK(!diagnostic_contains(&h,"Live state recheck; target held")&&!h.line[3][0]);
        log_count=r->input_plan.log_count;
        /* Same actual boundary cannot consume the remaining recovery budget. */
        reads=0;fail_at=7;route_first();fail_at=0;
        CHECK(device.waiting_diagnostic.failures==i+1u&&r->controls.candidate_valid);
    }
    actual_released_unit(&state,0);CHECK(r->waiting_valid&&!r->waiting_recheck);
    CHECK(r->raw_target==target&&r->raw_target-r->counter<RAW_MINIMUM_PLAYER_LEAD);
    CHECK(r->controls.candidate_valid&&r->plan_active&&!memcmp(&r->candidate,&immutable,sizeof(immutable)));
    CHECK(!device.source_diagnostic.waiting_unavailable_failures&&!r->view.query.query_id);
    safe_waiting();
}
static void waiting_recovery_drift(void){
    RawRuntime *r=&ch_raw_service.runtime;CQWaitingState state;uint32_t kind,counter;
    for(kind=0;kind<7u;kind++){
        reset();candidate_token();state=canonical;CHECK(cq_waiting_next(&state));state_at(901,&state,65,0xffff,0);
        reads=0;fail_at=7;route_first();fail_at=0;CHECK(r->waiting_recheck);
        CHECK(cq_waiting_next(&state));counter=902;
        if(kind==0)state.a^=1u;else if(kind==1)state.s^=1u;else if(kind==2)state.clock.div^=1u;
        else if(kind==3)state.bg=(state.bg+1u)%3u;
        else if(kind==4){state.clock.timer_phase++;state.clock.budget=state.clock.div_countdown<state.clock.timer_phase+1u?
            state.clock.div_countdown:state.clock.timer_phase+1u;}
        else if(kind==5){state.clock.div_countdown++;state.clock.budget=state.clock.div_countdown<state.clock.timer_phase+1u?
            state.clock.div_countdown:state.clock.timer_phase+1u;}
        state_at(counter,&state,kind==6?67u:66u,0xffff,0);route_first();invalidated();
        CHECK(!r->waiting_recheck);
    }
    reset();candidate_token();state=canonical;CHECK(cq_waiting_next(&state));state_at(901,&state,65,0xffff,0);
    reads=0;fail_at=7;route_first();fail_at=0;
    CHECK(cq_waiting_next(&state));state_at(902,&state,66,0xffff,0);fixture_prompt.map_number=51;
    CHECK(raw_platform_sample(&device.binding,&r->observed));route_first();invalidated();
    CHECK(device.waiting_diagnostic.stage==RAW_WAIT_DIAG_INFER);
}
static void waiting_result_deferral(void){
    RawRuntime *r=&ch_raw_service.runtime;RawJob job,out;ManualPrediction prediction,returned;CQWaitingState state=canonical;
    ChNativeContext gate;
    reset();real_waiting_job(&job,&prediction);CHECK(raw_mailbox_submit(&device.mailbox,&job));
    CHECK(raw_mailbox_take(&device.mailbox,&out));CHECK(raw_mailbox_finish(&device.mailbox,&out,&prediction));
    CHECK(cq_waiting_next(&state));state_at(901,&state,65,0xffff,0);reads=0;fail_at=7;route_first();fail_at=0;
    CHECK(r->waiting_recheck&&raw_query_complete(r,&job,&prediction)==RAW_QUERY_DEFERRED);
    CHECK(!receive(&device,&out,&returned)&&device.mailbox.ready_valid);
    CHECK(r->controls.active_query.query_id==job.token.query_id&&r->query_status==MANUAL_QUERY_OK);
    memset(&gate,0,sizeof(gate));gate.lr=0x1042f4u;
    CHECK(raw_service_route(&ch_raw_service,&gate)==0x110fa0u);
    CHECK(!received&&device.mailbox.ready_valid&&r->controls.active_query.query_id==job.token.query_id);
    raw_runtime_after_scan(r,&r->observed);raw_runtime_engine_entry(r,&r->observed);
    CHECK(cq_waiting_next(&state));state_at(902,&state,66,0xffff,0);route_first();
    CHECK(r->waiting_valid&&!r->waiting_recheck&&receive(&device,&out,&returned));
    CHECK(!memcmp(&job,&out,sizeof(job))&&!memcmp(&prediction,&returned,sizeof(prediction)));
    raw_runtime_poll_player(r,0,&r->observed);CHECK(raw_query_complete(r,&out,&returned)==RAW_QUERY_ACCEPTED);
}
static void waiting_original_a_rejected(void){
    RawRuntime *r=&ch_raw_service.runtime;CQWaitingState state;uint32_t target,counter;
    near_real_target(&state);target=r->raw_target;
    while(target-r->counter>2u)actual_released_unit(&state,0);
    actual_released_unit(&state,7);CHECK(r->waiting_recheck&&r->counter+1u==target);
    /* Still pass an actual physical A to the original scan while isolated. */
    counter=r->counter+1u;CHECK(cq_waiting_next(&state));state_at(counter,&state,(64u+counter-900u)&255u,0xffff,0);
    r->counter=counter-1u;raw_runtime_before_scan(r,&r->observed);raw_runtime_poll_player(r,0,&r->observed);
    raw_runtime_begin_scan(r,&r->observed);r->observed.host_held=1;raw_runtime_after_scan(r,&r->observed);
    CHECK(r->actual_raw_press_seen&&r->original_raw_a_seen&&!r->controls.candidate_valid&&!r->plan_active);
    raw_runtime_engine_entry(r,&r->observed);
    CHECK(!r->encounter_started&&r->observed.guest_mask==0xffffu);
    route_first();CHECK(r->waiting_valid&&!r->waiting_recheck&&!r->controls.candidate_valid&&r->plan_failed);
    CHECK(cq_waiting_next(&state));state_at(target+1u,&state,(65u+target-900u)&255u,0xffff,1);
    r->counter=target;raw_runtime_before_scan(r,&r->observed);
    raw_runtime_begin_scan(r,&r->observed);raw_runtime_after_scan(r,&r->observed);
    r->observed.guest_mask=0xfffe;
    raw_runtime_engine_entry(r,&r->observed);
    CHECK(r->encounter_started&&r->encounter_candidate.status!=MANUAL_QUERY_OK&&!r->input_condition_matches);
    CHECK(r->runtime==CH_RUNTIME_RUNNING&&!r->fault);
}
static void transient_diagnostic_publication(void){RawRuntime *r=&ch_raw_service.runtime;
    RawHud h;uint32_t i,base;RawDeviceSourceDiagnostic retained;
    reset();r->controls.overlay_visible=1;r->waiting_valid=0;
    device.waiting_diagnostic.failures=1;device.waiting_diagnostic.stage=RAW_WAIT_DIAG_ALIGNMENT;
    device.waiting_diagnostic.aligned.stage=SOURCE_ALIGNED_DOMAIN;
    device.waiting_diagnostic.aligned.domain_guard=7;
    device.source_diagnostic.waiting_unavailable_failures=4;
    /* The formerly red fourth failure is not a runtime fault. Full code
       formatting remains testable above; live publication waits for a stable
       cause instead of displaying it for the sole rejected game frame. */
    for(i=0;i<59u;i++){
        raw_runtime_hud(r,&h);base=h.count;diagnose_hud(&device,r,&h);
        CHECK(h.count==base&&!hud_red_rows(&h)&&!diagnostic_contains(&h,"Check T07:"));
        CHECK(!h.line[4][0]&&!strcmp(h.line[5],"L+R: pause"));
    }
    raw_runtime_hud(r,&h);diagnose_hud(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check T07:")&&!hud_red_rows(&h));
    /* Successful readback immediately retires the delayed warning. No HUD
       debounce can grant source/forecast validity. */
    r->waiting_valid=1;raw_runtime_hud(r,&h);base=h.count;diagnose_hud(&device,r,&h);
    CHECK(h.count==base&&!device.diagnostic_ticks);
    r->waiting_valid=0;raw_runtime_hud(r,&h);diagnose_hud(&device,r,&h);
    CHECK(!diagnostic_contains(&h,"Check T07:")&&device.diagnostic_ticks==1u);
    device.worker=(Thread)(uintptr_t)1;
    device.source_diagnostic.waiting_guard=7u;
    CHECK(raw_device_source_diagnostic_copy(&retained)&&retained.waiting_guard==7u);
    /* Stopped acquisition failures and runtime errors bypass presentation
       smoothing, retaining immediate red report codes. */
    r->source_bound=0;device.source_diagnostic.closed=1;
    device.source_diagnostic.phase=RAW_SOURCE_STOP_STORE;
    raw_runtime_hud(r,&h);diagnose_hud(&device,r,&h);
    CHECK(diagnostic_contains(&h,"Check S11/")&&hud_red_rows(&h));
    CHECK(!h.line[4][0]&&!strcmp(h.line[5],"L+R: pause"));
}
int main(void){upper_select_release_query();mismatch_and_read();fresh_and_A_strict();compatibility_save_inputs();compatibility_environment_layout();
    source_diagnostic_hud();waiting_diagnostic_codes();fresh_diagnostic_retention();
    recovery_diagnostics();
    waiting_recheck_severity();transient_diagnostic_publication();readonly_retry_budgets();
    waiting_target_recovery();waiting_recovery_drift();waiting_result_deferral();waiting_original_a_rejected();
    printf("passed: %u checks; runtime paths, dynamic mappings, live CPU fields, all valid StartTime tuples, and invalid save-time guards\n",checks);return 0;}
