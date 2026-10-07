/* Compile the actual coordinator, replacing only native/SDK boundaries. The
 * result origin/path admission and raw service/control path remain real C. */
#define CH_RAW_DEVICE_COORDINATOR_TEST 1
#define ch_result_read_snapshot fixture_result_read
#define ch_result_read_progress fixture_result_progress_read
#define raw_environment_restore_rtc fixture_restore_rtc
void fixture_restart_barrier(void);
#define __dmb fixture_restart_barrier
#include "../../rng/device_3ds.c"
#undef __dmb
#undef ch_result_read_snapshot
#undef ch_result_read_progress
#undef raw_environment_restore_rtc
#include "../../rng/result/pinned_path.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t checks,entered,left,snapshot_reads,progress_reads,restore_calls,hud_published;
static uint32_t audio_fixture,output_gain,audio_stores,audio_busy,display_leaves,display_cancels;
static uint32_t display_steps;
static uint64_t fixture_system_tick;
uint64_t svcGetSystemTick(void){return ++fixture_system_tick;}
static uint32_t fixture_display_state;
static uint64_t expected_sample_epoch;
static int worker_read_ok=1;
static uint32_t present_fixture,present_admitted,present_paints,present_receipts;
static RawPresentArgs present_args;
static int backend_ok=1,env_status=RAW_ENV_OK,read_ok=1,progress_read_ok=1,post_read_ok=1,post_changed,restore_status=RAW_ENV_OK;
static uint32_t result_trace_failure,result_trace_drift;
static RawSample fixture_sample;
static ChFinalPrompt fixture_prompt;
static ChResultSnapshot fixture_result;
static RawHud published_hud;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
void LightLock_Init(LightLock *p){*p=0;}
void LightLock_Lock(LightLock *p){CHECK(*p==0);*p=1;}
void LightLock_Unlock(LightLock *p){CHECK(*p==1);*p=0;}
void fixture_restart_barrier(void){CHECK(device.source_lock==1);}
uint32_t raw_device_test_tls(void){return 0x12345000u;}
int32_t svcSleepThread(int64_t n){(void)n;CHECK(0);return 0;}
Thread threadCreate(void (*f)(void *),void *u,size_t n,int32_t p,int32_t a,bool d){
    (void)f;(void)u;(void)n;(void)p;(void)a;(void)d;CHECK(0);return 0;
}
int32_t threadJoin(Thread t,uint64_t n){(void)t;(void)n;CHECK(0);return 0;}
void threadFree(Thread t){(void)t;CHECK(0);}
int raw_platform_sample(void *u,RawSample *s){RawPlatformBinding *b=u;
    CHECK(!expected_sample_epoch||b->source_epoch==expected_sample_epoch);
    *s=fixture_sample;memset(&b->last_boundary,0,sizeof(b->last_boundary));
    b->last_boundary.engine_type=1;b->last_boundary.batch_count=1;
    b->last_boundary.engine_pointer=0x27be7c;b->last_boundary.counter=s->counter;b->last_boundary.counter_valid=1;
    b->last_boundary.complete=b->last_boundary.coherent=1;
    b->last_boundary.ordinary_supported=(uint8_t)s->ordinary_supported;
    b->last_boundary.guest_active_low_mask=(uint16_t)s->guest_mask;
    b->last_boundary.host_cache[0]=s->host_held;b->last_boundary.host_cache[3]=s->host_intersection;
    b->last_sample_status=s->ordinary_supported?CH_SAMPLE_OK:CH_SAMPLE_UNSUPPORTED;
    b->last_prompt=fixture_prompt;b->last_prompt.match=(uint8_t)s->script_final_prompt;
    b->last_scene_status=(int)s->script_final_prompt;return (int)s->sample_complete;
}
int raw_platform_keys(void *u,uint32_t *k){(void)u;*k=0;return 1;}
int raw_environment_backend_enter(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    CHECK(c->lr==0x1a8340u);entered++;if(!backend_ok)return 0;
    CHECK(!s->active);s->active=1;return 1;
}
int raw_environment_backend_enter_restart(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    CHECK(c->lr==RAW_RESTART_RECEIPT_LR||c->lr==RAW_MENU_RESET_RECEIPT_LR);entered++;if(!backend_ok)return 0;
    CHECK(!s->active);if(!raw_restart_receipt(&device.binding.read,c))return 0;
    s->active=s->cleanup_only=1;return 1;
}
int raw_environment_backend_enter_restart_boundary(RawEnvironmentBackend3ds *s,const ChNativeContext *c){
    CHECK(c->lr==0x1042f4u&&ch_raw_service.restart_pending&&device.restart_notice.valid);
    entered++;if(!backend_ok)return 0;
    CHECK(!s->active);s->active=s->cleanup_only=1;return 1;
}
void raw_environment_backend_leave(RawEnvironmentBackend3ds *s){CHECK(s->active);s->active=s->cleanup_only=0;left++;}
int fixture_result_read(const ChReadOps *o,ChResultSnapshot *s){uint8_t byte;uint32_t i;snapshot_reads++;
    if(result_trace_failure){(void)o->read_bytes(o->user,0x0badcafeu,&byte,1);return 0;}
    if(result_trace_drift){for(i=0;i<6u;i++)CHECK(o->read_bytes(o->user,0x22f5fcu,&byte,1));return 0;}
    if(!read_ok||(device.environment.state.initial_applied&&!post_read_ok))return 0;
    *s=fixture_result;s->counter=fixture_sample.counter;
    if(device.environment.state.initial_applied&&post_changed)s->enemy[1]=1;
    return 1;
}
int fixture_result_progress_read(const ChReadOps *o,ChResultProgress *s){(void)o;
    progress_reads++;if(!read_ok||!progress_read_ok)return 0;
    memcpy(s,&fixture_result,sizeof(*s));s->counter=fixture_sample.counter;return 1;
}
int fixture_restore_rtc(const RawEnvironmentOps *o,RawEnvironmentState *s){(void)o;
    restore_calls++;CHECK(s->rtc_owned);
    if(restore_status==RAW_ENV_OK)s->rtc_owned=0;else s->poisoned=1;
    return restore_status;
}
void raw_hud_3ds_publish(void *u,const RawHud *h){(void)u;published_hud=*h;hud_published++;}
int raw_hud_3ds_present(RawHud3dsSink *s,uint32_t a,uint32_t b,uint8_t *c,uint8_t *d,uint32_t e,uint32_t f){
    CHECK(present_fixture&&s==&device.screen);
    CHECK(a==present_args.screen_id&&b==present_args.swap&&(uintptr_t)c==present_args.fb_a&&
        (uintptr_t)d==present_args.fb_b&&e==present_args.stride&&f==present_args.format);
    present_paints++;return RAW_HUD_SINK_PAINTED;
}
int raw_present_decode(const RawPresentBinding *s,const ChNativeContext *c,RawPresentArgs *a){
    CHECK(present_fixture&&s==&device.presentation&&c->lr==RAW_PRESENT_RETURN);
    *a=present_args;return 1;
}
int raw_paused_display_update(RawPausedDisplay *s,const RawHud *h,int p){
    CHECK(p==1);published_hud=*h;
    if(fixture_display_state)s->state=fixture_display_state;
    return fixture_display_state==RAW_PAUSE_PANEL_PENDING?RAW_PAUSE_WAIT:RAW_PAUSE_READY;
}
int raw_paused_display_leave(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_leaves++;return 1;}
int raw_paused_display_cancel(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_cancels++;return 1;}
int raw_paused_display_step(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_steps++;return 1;}
int raw_paused_display_game_present(RawPausedDisplay *s,const RawPauseRecord *p){
    CHECK(present_fixture&&s==&device.pause_display&&s->state==RAW_PAUSE_STEP_WAIT);
    CHECK(p->fb_a==present_args.fb_a&&p->fb_b==present_args.fb_b);
    present_receipts++;return (int)present_admitted;
}
int raw_paused_audio_3ds_init(RawAudio3ds *s,const ChReadOps *r,RawPauseAudioOps *o){
    (void)s;(void)r;(void)o;CHECK(0);return 0;
}
/* Startup is outside this host fixture. Real startup/device object builds are
 * checked separately; accidental calls here fail instead of fabricating SDK. */
void ch_manual_alias_probe(void){CHECK(0);}
void ch_raw_bridge(void){CHECK(0);}
void ch_3ds_backend_ops(Ch3dsBackend *b,ChInstallOps *o,ChReadOps *r){(void)b;(void)o;(void)r;CHECK(0);}
int ch_3ds_hid_init(Ch3dsBackend *b){(void)b;CHECK(0);return 0;}
void ch_3ds_hid_exit(Ch3dsBackend *b){(void)b;CHECK(0);}
void ch_3ds_startup_finished(Ch3dsBackend *b){(void)b;CHECK(0);}
int ch_3ds_read_begin(Ch3dsBackend *b){(void)b;return 0;}
void ch_3ds_read_end(Ch3dsBackend *b){(void)b;CHECK(0);}
int ch_3ds_worker_read_ops(const Ch3dsBackend *original,Ch3dsBackend *local,ChReadOps *out){
    CHECK(original==&device.backend&&local!=original);
    if(!worker_read_ok)return 0;
    memset(local,0,sizeof(*local));*out=device.binding.read;return 1;
}
int raw_hud_3ds_sink_init(RawHud3dsSink *s,uint32_t n,const RawHudLayout *l){(void)s;(void)n;(void)l;CHECK(0);return 0;}
int raw_present_prepare(RawPresentBinding *s,const ChReadOps *o){(void)s;(void)o;CHECK(0);return 0;}
int raw_present_activate(RawPresentBinding *s,uint32_t n){(void)s;(void)n;CHECK(0);return 0;}
int raw_paused_3ds_init(RawPause3ds *s,const RawPresentBinding *b,RawPauseOps *o){(void)s;(void)b;(void)o;CHECK(0);return 0;}
int raw_paused_display_init(RawPausedDisplay *s,const RawPauseOps *o,const RawHudLayout *l){(void)s;(void)o;(void)l;CHECK(0);return 0;}
int raw_environment_backend_init(RawEnvironmentBackend3ds *s,Ch3dsBackend *b,const ChReadOps *r,RawEnvironmentOps *o){
    (void)s;(void)b;(void)r;(void)o;CHECK(0);return 0;
}
#define main waiting_cases_main
#include "cases.c"
#undef main
static void first_step_presenter(void){ChNativeContext c;uint32_t before;
    reset();ch_raw_service.ops.present=present;present_fixture=present_admitted=1;
    present_paints=present_receipts=0;memset(&c,0,sizeof(c));c.lr=RAW_PRESENT_RETURN;
    present_args=(RawPresentArgs){0};present_args.screen_id=0;present_args.swap=0;
    present_args.fb_a=0x16000000;present_args.fb_b=0x17000000;
    present_args.stride=480;present_args.format=0x343;
    device.pause_display.state=RAW_PAUSE_STEP_WAIT;before=reads;
    /* The real native route must paint before it returns the original writer
       chain. An unpainted receipt cannot keep this submitted L frame visible. */
    CHECK(raw_service_route(&ch_raw_service,&c)==RAW_PRESENT_TARGET);
    CHECK(present_paints==1&&present_receipts==1&&reads==before);
    present_admitted=0;CHECK(raw_service_route(&ch_raw_service,&c)==RAW_PRESENT_TARGET);
    CHECK(present_paints==1&&present_receipts==2);
    present_args.screen_id=1;CHECK(raw_service_route(&ch_raw_service,&c)==RAW_PRESENT_TARGET);
    CHECK(present_paints==1&&present_receipts==2);
    device.pause_display.state=RAW_PAUSE_RETAINED;
    CHECK(raw_service_route(&ch_raw_service,&c)==RAW_PRESENT_TARGET);CHECK(present_paints==1);
    present_fixture=0;
}
#ifndef WAITING_DEVICE_MAIN
#define WAITING_DEVICE_MAIN main
#endif
static void step_trace_coordinator(void){RawRuntime *r;ChNativeContext c={0};uint64_t old_tick;
    reset();r=&ch_raw_service.runtime;r->runtime=CH_RUNTIME_STEPPING;r->at_gate=1u;
    r->scene_epoch=44u;r->last_request=12u;r->counter=55u;r->step_target=56u;
    c.lr=0x1042f4u;audio_fixture=1u;fixture_system_tick=100u;
    CHECK(paused(&device,&c,r,4)==1&&device.step_trace.seen==RAW_STEP_TRACE_ACCEPTED);
    CHECK(device.step_trace.origin_counter==55u&&device.step_trace.target_counter==56u);
    present_fixture=present_admitted=1u;present_args.screen_id=0u;
    device.pause_display.state=RAW_PAUSE_STEP_WAIT;c.lr=RAW_PRESENT_RETURN;
    present(&device,&c);CHECK(device.step_trace.seen==7u);
    r->runtime=CH_RUNTIME_PAUSED;r->completed_request=12u;r->counter=56u;
    device.source_available=0u;c.lr=0x1042f4u;completed(&device,&c,r,1u);
    CHECK(device.step_trace.seen==15u&&!device.step_trace.fault);
    fixture_display_state=RAW_PAUSE_PANEL_PENDING;
    CHECK(paused(&device,&c,r,1)==RAW_PAUSE_WAIT&&device.step_trace.seen==31u);
    fixture_display_state=RAW_PAUSE_ACTIVE;
    CHECK(paused(&device,&c,r,1)==RAW_PAUSE_READY&&device.step_trace.seen==63u);
    CHECK(device.step_trace.accepted_tick<device.step_trace.present_begin_tick&&
        device.step_trace.present_end_tick<device.step_trace.actual_boundary_tick&&
        device.step_trace.actual_boundary_tick<device.step_trace.clone_published_tick&&
        device.step_trace.clone_published_tick<device.step_trace.clone_acknowledged_tick);
    old_tick=fixture_system_tick;device.pause_display.state=RAW_PAUSE_OFF;c.lr=RAW_PRESENT_RETURN;
    present(&device,&c);CHECK(fixture_system_tick==old_tick&&!device.step_trace.fault);
    present_fixture=audio_fixture=fixture_display_state=0u;
}
int WAITING_DEVICE_MAIN(void){int status=waiting_cases_main();first_step_presenter();
    step_trace_coordinator();
    printf("first L presenter: paints current original frame before submission; %u total checks\n",checks);
    return status;
}
