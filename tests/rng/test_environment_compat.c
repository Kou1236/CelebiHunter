/* Host memory fixture: real capture, preparation, source binding, clock query,
 * input receipts and terminal guards. It does not execute the game or SDK. */
#include "environment_session.h"
#include "environment_data.h"
#include "source-observer/aligned_waiting.h"
#include "result/result_gate.h"
#include "result/pinned_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint32_t base,size;uint8_t *bytes;} Mapping;
typedef struct {
    uint8_t rom[0xa0000],wram[32768],io[256],audio[160],rtc[16],engine[0x154];
    uint8_t native[0x504],sound[0x900],host[0x74],extra[0x210],previous[4];
    uint8_t config_pointer[4],config[4],instruction[4],provider[sizeof(raw_environment_provider)],tail[16];
    Mapping maps[16];uint32_t count,rom_base,wram_base,io_base,rtc_base,engine_base;
    uint32_t drift_address,drift_reads,stores,rtc_stores,publishes;
} Fixture;
static Fixture f;
static RawEnvironmentSession session;
static RawEnvironmentPlan plan;
static RawSourceCapture captured;
static MpInputPlan input;
static uint32_t checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"failed line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
static void put16(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
static void native_word(uint32_t a,uint32_t v){put32(f.native+a-0x22f5fcu,v);}
static void map(uint32_t a,uint8_t *p,uint32_t n){CHECK(f.count<16u);f.maps[f.count++]=(Mapping){a,n,p};}
static uint8_t *mapped(uint32_t a,uint32_t n){uint32_t i;
    for(i=0;i<f.count;i++){Mapping *m=&f.maps[i];
        if(a>=m->base&&a-m->base<=m->size&&n<=m->size-(a-m->base))return m->bytes+a-m->base;}
    return 0;
}
static int read_memory(void *u,uint32_t a,void *out,uint32_t n){uint8_t *p=mapped(a,n);(void)u;
    if(!p||!n)return 0;
    memcpy(out,p,n);
    if(f.drift_address>=a&&f.drift_address-a<n)
        ((uint8_t *)out)[f.drift_address-a]^=(uint8_t)(++f.drift_reads&1u);
    return 1;
}
static int expected_data(uint32_t a){
    return (a>=f.rtc_base&&a-f.rtc_base<5u)||(a>=0x22f684u&&a-0x22f684u<5u)||
        (a>=f.wram_base+0x14c6u&&a-(f.wram_base+0x14c6u)<3u)||a==f.io_base+5u||
        (a>=0x22f600u&&a-0x22f600u<4u)||(a>=0x22fa48u&&a-0x22fa48u<12u)||a==0x22f6e4u;
}
static int store_byte(void *u,uint32_t a,uint8_t v){uint8_t *p=mapped(a,1);ChReadOps read={u,read_memory,0};
    CHECK(expected_data(a)&&raw_environment_data_address(&read,a)&&p);
    CHECK(a!=f.io_base+4u&&a!=f.io_base+128u+0x61u&&a!=f.io_base+128u+0x62u);
    CHECK(a!=f.wram_base+0x121bu&&a!=f.wram_base+0x121cu);
    *p=v;f.stores++;return 1;
}
static int store_span(void *u,uint32_t a,const uint8_t *v,uint32_t n){uint32_t i;ChReadOps read={u,read_memory,0};
    CHECK(raw_environment_data_range(&read,a,n));
    for(i=0;i<n;i++)CHECK(store_byte(u,a+i,v[i]));
    return 1;
}
static int store_rtc(void *u,uint32_t v){(void)u;CHECK(v==0x0bfef301u||v==0xe1a00000u);
    put32(f.instruction,v);f.rtc_stores++;return 1;}
static int publish(void *u){(void)u;f.publishes++;return 1;}
static ChNativeContext caller(void){ChNativeContext c;memset(&c,0,sizeof(c));c.lr=0x1a8340u;return c;}
static void setup(uint32_t variant,const uint8_t start[4]){
    static const uint8_t joywait[]={0xcd,0x5a,0x04,0xcd,0x84,0x09,0xf0,0xa7,0xe6,0x03,0xc0,0xcd,0x6f,0x04,0x18,0xf0};
    static const uint8_t waitbutton[]={0xf0,0xd8,0xf5,0x3e,0x01,0xe0,0xd8,0xcd,0xf6,0x31,0xcd,0x36,0x0a,0xf1,0xe0,0xd8,0xc9};
    uint32_t delta=variant*0x10000000u,i,offset=0;RawEnvironmentOps ops;
    memset(&f,0,sizeof(f));f.drift_address=UINT32_MAX;
    f.rom_base=0x10000000u+delta;f.wram_base=0x11000000u+delta;f.io_base=0x12000000u+delta;
    f.rtc_base=0x15000000u+delta;f.engine_base=0x16000000u+delta;
    map(f.rom_base,f.rom,sizeof(f.rom));map(f.wram_base,f.wram,sizeof(f.wram));
    map(f.io_base,f.io,sizeof(f.io));map(0x13000000u+delta,f.audio,sizeof(f.audio));
    map(f.rtc_base,f.rtc,sizeof(f.rtc));map(f.engine_base,f.engine,sizeof(f.engine));
    map(0x22f5fcu,f.native,sizeof(f.native));map(0x232d00u,f.sound,sizeof(f.sound));
    map(0x27bff0u,f.host,sizeof(f.host));map(0x230f00u,f.extra,sizeof(f.extra));
    map(0x23278cu,f.previous,sizeof(f.previous));map(0x5e1928u,f.config_pointer,4);
    map(0x14000000u+delta,f.config,4);map(0x1aa528u,f.instruction,4);
    map(0x195df4u,f.provider,sizeof(f.provider));map(0x1a84f4u,f.tail,sizeof(f.tail));
    put32(f.config_pointer,0x14000000u+delta);put32(f.host+0x70,1);
    put32(f.engine,1);put32(f.engine+0x140,100);
    native_word(0x22f640u,f.rtc_base);native_word(0x22f644u,f.rtc_base+8);
    native_word(0x22f698u,f.engine_base);native_word(0x22f6c4u,f.rom_base);
    native_word(0x22f6c8u,f.wram_base);native_word(0x22f768u,f.wram_base+4096);
    native_word(0x22f6d4u,0x13000000u+delta);native_word(0x22f6d8u,f.io_base+128);
    native_word(0x22f6dcu,f.io_base);native_word(0x22f788u,f.io_base);native_word(0x22f7a4u,f.io_base+15);
    native_word(0x22f5fcu,0xc0d50460u);native_word(0x22f600u,17);
    native_word(0x22f604u,37u+variant);native_word(0x22f608u,1);native_word(0x22f60cu,1);
    native_word(0x22f610u,f.rom_base+0x460u);native_word(0x22f62cu,0xc5430120u+variant);
    native_word(0x22f630u,0xd848a000u);put16(f.native+0x22f766u-0x22f5fcu,0xffffu);
    native_word(0x22f6f0u,113);native_word(0x22f6f4u,51);native_word(0x22f6f8u,114);
    native_word(0x22f6fcu,20);native_word(0x22f700u,43);native_word(0x22f738u,1);native_word(0x22f75cu,1);
    put32(f.instruction,0x0bfef301u);memcpy(f.provider,raw_environment_provider,sizeof(f.provider));
    memcpy(f.rom+0xa36,joywait,sizeof(joywait));memcpy(f.rom+0xa46,waitbutton,sizeof(waitbutton));
    f.rom[0x1b9]=0x31;f.rom[0x1ba]=0xff;f.rom[0x1bb]=0xc0;
    /* Use the actual immutable result bytes, including GetScriptByte. */
    for(i=0;i<CH_RESULT_PATH_COUNT;i++){const ChResultPathRange *r=&ch_result_path_ranges[i];
        uint32_t a=r->native_address?r->offset:f.rom_base+r->offset;uint8_t *p=mapped(a,r->size);
        CHECK(p);memcpy(p,ch_result_expected_path+offset,r->size);offset+=r->size;}
    f.wram[0x1437]=1;f.wram[0x1438]=255;f.wram[0x1439]=27;put16(f.wram+0x143a,0x6e54);
    f.wram[0x1cb5]=3;f.wram[0x1cb6]=52;put16(f.wram+0xd5,0xa53);put16(f.wram+0xd7,0xa39);
    memcpy(f.wram+0x14b6,start,4);f.io[0]=255;f.io[4]=216;f.io[5]=9;f.io[7]=4;
    f.io[15]=1;f.io[255]=15;f.io[0x41]=9;f.io[0x44]=144;f.io[0x70]=1;
    f.io[128+0x55]=2;f.io[128+0x61]=6;f.io[128+0x62]=234;
    ops=(RawEnvironmentOps){{&f,read_memory,0},store_byte,store_rtc,publish,store_span};
    CHECK(raw_environment_session_init(&session,&ops));
}
static void protected_unchanged(const uint8_t start[4],uint32_t div,uint32_t add,uint32_t sub){
    CHECK(!memcmp(f.wram+0x14b6,start,4));CHECK(f.io[4]==div);
    CHECK(f.io[128+0x61]==add&&f.io[128+0x62]==sub);
    CHECK(f.wram[0x121b]==0&&f.wram[0x121c]==0);
}
static void audit_plan(const RawEnvironmentPlan *p){uint32_t i;
    CHECK(p->before.structural_match&&!p->before.fixed_context_applied&&!p->before.manual_hardware_verified);
    for(i=0;i<p->count;i++)CHECK(expected_data(p->writes[i].address)||
        (p->writes[i].address>=0x1aa528u&&p->writes[i].address<0x1aa52cu));
}
static MpSourceConditions conditions(const ManualSourceBinding *b,uint32_t counter,uint32_t held,uint32_t intersection,uint32_t mask){
    MpSourceConditions a;memset(&a,0,sizeof(a));a.abi=MP_ABI;a.counter=counter;
    a.engine_type=a.host_batch_count=1;a.original_host_held=held;a.original_host_intersection=intersection;
    a.actual_applied_mask=mask;a.source_epoch=b->source_epoch;memcpy(a.source_identity.bytes,b->source_identity.bytes,32);return a;
}
static void terminal(const ManualSourceBinding *b,const uint8_t start[4]){
    ChBoundarySample boundary;ChFinalPrompt prompt;SourceObservation observed;CQWaitingState waiting;
    CQQueryResult result;ManualPrediction prediction;MpConditionalTarget target;MpSourceConditions a;
    ChNativeContext c=caller();uint32_t counter,previous=0,held,intersection,mask;
    CHECK(ch_sample_boundary(&session.ops.read,&boundary)==CH_SAMPLE_OK);
    CHECK(ch_scene_final_prompt(&session.ops.read,&prompt)==1);
    CHECK(source_aligned_waiting(&session.ops.read,&boundary,&prompt,77,&observed,&waiting));
    CHECK(cq_find(&waiting,0,2,&result)==CQ_QUERY_OK&&result.found);
    /* Bind a real pure-query result to this actual fixture source. */
    memset(&prediction,0,sizeof(prediction));prediction.abi=MANUAL_PREDICTION_ABI;
    prediction.status=MANUAL_QUERY_OK;prediction.input_contract=manual_model_certificate()->input_contract;
    prediction.source_epoch=b->source_epoch;prediction.source_identity=b->source_identity;
    prediction.required_context_identity=b->required_context_identity;prediction.origin_counter=b->origin_counter;
    prediction.press_relative=result.effective_n;prediction.model_n=result.effective_n+2;
    prediction.target_counter=b->origin_counter+result.effective_n;prediction.expected_release_counter=prediction.target_counter+3;
    prediction.div_x=result.div_x;prediction.predicted_dv=result.predicted_dv;prediction.branch=result.branch;
    prediction.expected_wait_add=result.expected_wait_add;prediction.expected_wait_sub=result.expected_wait_sub;
    memset(&target,0,sizeof(target));target.abi=MP_ABI;target.source_epoch=b->source_epoch;
    target.effective_a_counter=prediction.target_counter;target.expected_release_counter=prediction.expected_release_counter;
    memcpy(target.source_identity.bytes,b->source_identity.bytes,32);
    memcpy(target.required_context_identity.bytes,b->required_context_identity.bytes,32);
    memcpy(target.model_certificate_identity.bytes,manual_model_certificate()->profile_artifact.bytes,32);
    a=conditions(b,b->origin_counter,0,0,0xffff);CHECK(mp_plan_create(&a,&target,mp_original_input_evidence(),&input)==MP_OK);
    for(counter=b->origin_counter;counter<=prediction.target_counter;counter++){
        held=counter>=prediction.target_counter-1u;intersection=held&previous;
        mask=counter==prediction.target_counter?0xfffeu:0xffffu;
        a=conditions(b,counter,held,intersection,mask);
        CHECK(mp_plan_observe_scan(&input,&a,previous,held,intersection)==MP_OK);
        CHECK(mp_plan_observe_applied_mask(&input,&a,mask)==MP_OK);
        if(counter==prediction.target_counter)break;
        a.counter=counter+1;CHECK(mp_plan_observe_counter(&input,&a,counter)==MP_OK);
        CHECK(cq_waiting_next(&waiting));previous=held;
    }
    put32(f.engine+0x140,prediction.target_counter);put32(f.host,1);put32(f.host+12,1);
    put16(f.native+0x22f766u-0x22f5fcu,0xfffeu);
    f.io[4]=(uint8_t)waiting.clock.div;f.io[128+0x55]=(uint8_t)waiting.bg;
    f.io[128+0x61]=(uint8_t)waiting.a;f.io[128+0x62]=(uint8_t)waiting.s;
    native_word(0x22f600u,waiting.clock.budget);native_word(0x22fa48u,waiting.clock.timer_phase);
    native_word(0x22fa4cu,waiting.clock.timer_phase+1);native_word(0x22fa50u,waiting.clock.div_countdown);
    CHECK(waiting.a==result.expected_wait_add&&waiting.s==result.expected_wait_sub&&waiting.clock.div==result.div_x);
    /* The stored identity excludes BC/DE/HL, but retains AF and 22f604. */
    f.native[0x22f62eu-0x22f5fcu]^=0x5a;f.native[0x22f630u-0x22f5fcu]^=0xa5;
    f.native[0x22f604u-0x22f5fcu]^=1;
    CHECK(raw_environment_prepare(&session.ops,&c,0x12345000,77,1,RAW_ENV_TERMINAL,result.div_x,
        &session.state,&prediction,&input,&plan)==RAW_ENV_REJECTED);
    f.native[0x22f604u-0x22f5fcu]^=1;f.native[0x22f62cu-0x22f5fcu]^=1;
    CHECK(raw_environment_prepare(&session.ops,&c,0x12345000,77,1,RAW_ENV_TERMINAL,result.div_x,
        &session.state,&prediction,&input,&plan)==RAW_ENV_REJECTED);
    f.native[0x22f62cu-0x22f5fcu]^=1;
    CHECK(raw_environment_prepare(&session.ops,&c,0x12345000,77,1,RAW_ENV_TERMINAL,result.div_x,
        &session.state,&prediction,&input,&plan)==RAW_ENV_OK);
    audit_plan(&plan);CHECK(raw_environment_apply(&session.ops,&session.state,&plan)==RAW_ENV_OK);
    CHECK(session.state.terminal_applied&&!session.state.poisoned&&!input.manual_hardware_verified);
    protected_unchanged(start,result.div_x,result.expected_wait_add,result.expected_wait_sub);
}
static void full_session(uint32_t variant,const uint8_t start[4],uint8_t identity[32]){
    ChNativeContext c=caller();ManualSourceBinding binding;uint64_t generation;ChResultSource result;ChResultGate gate;
    setup(variant,start);CHECK(raw_environment_initial_cpu_check(&session.ops.read)==RAW_ENV_CPU_MATCH);
    CHECK(raw_capture_source(&session.ops.read,&c,0x12345000,77,1,&captured));
    CHECK(captured.pointers[3]==f.wram_base&&captured.pointers[6]==f.io_base&&captured.pointers[2]==f.engine_base);
    CHECK(raw_environment_session_prepare(&session,&c,0x12345000,77));audit_plan(&session.plan);
    CHECK(session.state.preparation_applied&&!session.state.initial_applied&&session.state.rtc_owned);
    protected_unchanged(start,216,6,234);put32(f.engine+0x140,101);
    /* Different live work registers between preparation and initial source. */
    f.native[0x22f62eu-0x22f5fcu]^=0x77;
    CHECK(raw_environment_session_source(&session,&c,0x12345000,77,&captured,&binding,&generation));
    audit_plan(&session.plan);CHECK(generation==1&&binding.origin_counter==101&&session.state.initial_applied);
    CHECK(!memcmp(binding.source_identity.bytes,captured.identity,32));
    CHECK(!memcmp(session.state.preparation_start_time,start,4));
    memcpy(identity,session.state.nonbudget_CPU_identity,32);protected_unchanged(start,216,6,234);
    memset(&result,0,sizeof(result));result.epoch=77;memcpy(result.source_identity,binding.source_identity.bytes,32);
    CHECK(ch_result_read_snapshot(&session.ops.read,&result.source));CHECK(ch_result_begin(&gate,&result)==CH_RESULT_PENDING);
    terminal(&binding,start);CHECK(f.stores>0&&f.rtc_stores==1&&f.publishes==1);
    CHECK(raw_environment_restore_rtc(&session.ops,&session.state)==RAW_ENV_OK&&!session.state.rtc_owned);
}
static void reject_changes(const uint8_t start[4]){
    ChNativeContext c=caller();uint32_t stores;setup(0,start);
    f.drift_address=f.wram_base+0x300u;
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    setup(0,start);native_word(0x22f6d4u,0x33000000u);
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    setup(0,start);native_word(0x22f5fcu,0xc0d50461u);
    CHECK(raw_environment_initial_cpu_check(&session.ops.read)==RAW_ENV_CPU_MISMATCH);
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    setup(0,start);f.extra[0x7d]=1;
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    setup(0,start);f.wram[0x14b6]=140;
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    CHECK(raw_environment_initial_source_rejection(&session.plan,&session.state)==RAW_ENV_SOURCE_SAVE_TIME);
    setup(0,start);f.wram[0x14b6]=73;
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    CHECK(raw_environment_initial_source_rejection(&session.plan,&session.state)==RAW_ENV_SOURCE_SAVE_TIME);
    setup(0,start);f.wram[0x14b6]=72;f.wram[0x14b7]=23;
    CHECK(!raw_environment_session_prepare(&session,&c,0x12345000,77));CHECK(!f.stores&&!f.rtc_stores);
    CHECK(raw_environment_initial_source_rejection(&session.plan,&session.state)==RAW_ENV_SOURCE_SAVE_TIME);
    setup(0,start);
    CHECK(raw_environment_prepare(&session.ops,&c,0x12345000,77,1,RAW_ENV_PREPARATION,183,
        &session.state,0,0,&plan)==RAW_ENV_OK);audit_plan(&plan);stores=f.stores;
    f.native[0x22f62cu-0x22f5fcu]^=1;
    CHECK(raw_environment_apply(&session.ops,&session.state,&plan)==RAW_ENV_REJECTED);
    CHECK(f.stores==stores&&!f.rtc_stores&&!session.state.preparation_applied);
    f.native[0x22f62cu-0x22f5fcu]^=1;native_word(0x22f6c8u,f.wram_base+4);
    CHECK(raw_environment_apply(&session.ops,&session.state,&plan)==RAW_ENV_REJECTED);
    CHECK(f.stores==stores&&!f.rtc_stores&&!session.state.preparation_applied);
}
static RawSample runtime_sample(void){ChBoundarySample b;ChFinalPrompt p;RawSample s;int status;
    status=ch_sample_boundary(&session.ops.read,&b);
    CHECK(status==CH_SAMPLE_OK||status==CH_SAMPLE_UNSUPPORTED);
    memset(&s,0,sizeof(s));s.counter=b.counter;s.counter_read=b.counter_valid;
    s.sample_complete=b.complete;s.ordinary_supported=b.ordinary_supported;
    s.batch=b.batch_count;s.host_phase=b.host_phase;s.input_type=b.engine_type;
    s.host_held=b.host_cache[0];s.host_intersection=b.host_cache[3];
    s.input_enable=b.input_enable;s.guest_mask=b.guest_active_low_mask;s.scene_epoch=77;
    s.script_final_prompt=(uint32_t)(ch_scene_final_prompt(&session.ops.read,&p)==1&&p.match);
    return s;
}
static void source_recovery(uint32_t fault_case,const uint8_t start[4]){
    static RawRuntime runtime;
    ChNativeContext c=caller();ManualSourceBinding initial,fresh;uint64_t generation;
    ChResultSource result;ChResultSnapshot before;ChResultGate gate;RawSample s;
    uint32_t stores,rtc_stores;uint8_t old_identity[32];
    setup(fault_case,start);CHECK(raw_environment_session_prepare(&session,&c,0x12345000,77));
    put32(f.engine+0x140,101);
    CHECK(raw_environment_session_source(&session,&c,0x12345000,77,&captured,&initial,&generation));
    raw_runtime_init(&runtime,0);s=runtime_sample();raw_runtime_before_scan(&runtime,&s);
    CHECK(raw_runtime_bind_source(&runtime,&initial,generation));
    CHECK(runtime.source_bound&&runtime.source_generation==1&&!runtime.refresh_needed);
    memcpy(old_identity,initial.source_identity.bytes,32);stores=f.stores;rtc_stores=f.rtc_stores;
    /* A transient scheduler mode change withdraws the source through the real
       runtime. The scene and released input remain eligible for recovery. */
    put32(f.engine+0x140,102);put32(f.engine+0x144,1);
    s=runtime_sample();CHECK(!s.ordinary_supported&&s.script_final_prompt);
    raw_runtime_before_scan(&runtime,&s);
    CHECK(!runtime.source_bound&&runtime.refresh_needed&&!runtime.fault);
    CHECK(raw_runtime_source_recovery_pending(&runtime));
    CHECK(!raw_environment_session_first_needed(&session,&runtime));
    CHECK(raw_environment_session_first(&session,&c,0x12345000,77,&runtime)==0);
    CHECK(session.state.rtc_owned&&!session.cleanup_attempted&&f.rtc_stores==rtc_stores&&f.stores==stores);
    /* Resume at an actual, released final-prompt sample. Rebase captures and
       applies the live image, then result readback precedes runtime binding. */
    put32(f.engine+0x140,103);put32(f.engine+0x144,0);
    s=runtime_sample();CHECK(s.ordinary_supported&&s.script_final_prompt);
    raw_runtime_before_scan(&runtime,&s);CHECK(raw_runtime_source_recovery_pending(&runtime));
    CHECK(ch_result_read_snapshot(&session.ops.read,&before));
    CHECK(raw_environment_session_rebase(&session,&c,0x12345000,77,&captured,&fresh,&generation));
    audit_plan(&session.plan);CHECK(generation==2&&fresh.origin_counter==103&&session.state.origin_counter==103);
    CHECK(memcmp(old_identity,fresh.source_identity.bytes,32));
    memset(&result,0,sizeof(result));result.epoch=77;memcpy(result.source_identity,fresh.source_identity.bytes,32);
    CHECK(ch_result_read_snapshot(&session.ops.read,&result.source));CHECK(!memcmp(&before,&result.source,sizeof(before)));
    CHECK(ch_result_begin(&gate,&result)==CH_RESULT_PENDING);
    CHECK(raw_runtime_bind_source(&runtime,&fresh,generation));session.generation=generation;
    CHECK(runtime.source_bound&&runtime.source_generation==2&&!runtime.refresh_needed);
    CHECK(!raw_runtime_source_recovery_pending(&runtime));
    CHECK(session.state.rtc_owned&&!session.cleanup_attempted&&f.rtc_stores==rtc_stores);
    protected_unchanged(start,216,6,234);
    /* Recovery does not defer cleanup for a runtime fault or leaving the text. */
    if(fault_case)raw_runtime_environment_failed(&runtime);
    else{put16(f.wram+0x143a,0x6e55);s=runtime_sample();CHECK(!s.script_final_prompt);
        raw_runtime_before_scan(&runtime,&s);}
    CHECK(!runtime.source_bound&&!raw_runtime_source_recovery_pending(&runtime));
    CHECK(raw_environment_session_first_needed(&session,&runtime));
    CHECK(raw_environment_session_first(&session,&c,0x12345000,77,&runtime)==1);
    CHECK(!session.state.rtc_owned&&session.cleanup_attempted&&!session.state.poisoned);
    CHECK(f.rtc_stores==rtc_stores+1&&!memcmp(f.instruction,"\x01\xf3\xfe\x0b",4));
    CHECK(!raw_environment_session_rebase(&session,&c,0x12345000,77,&captured,&fresh,&generation));
}
int main(void){const uint8_t start_a[4]={0,0,0,0},start_b[4]={7,23,59,59};uint8_t a[32],b[32];
    full_session(0,start_a,a);full_session(1,start_b,b);CHECK(memcmp(a,b,32));reject_changes(start_b);
    source_recovery(0,start_a);source_recovery(1,start_b);
    printf("passed: %u real C environment checks; mappings/saves, CPU binding, terminal, source recovery and cleanup guards (host fixture only)\n",checks);
    return 0;
}
