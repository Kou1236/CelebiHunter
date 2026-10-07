/* Compile the actual coordinator, replacing only native/SDK boundaries. The
 * result origin/path admission and raw service/control path remain real C. */
#define CH_RAW_DEVICE_COORDINATOR_TEST 1
#define ch_result_read_snapshot fixture_result_read
#define ch_result_read_progress fixture_result_progress_read
#define raw_environment_restore_rtc fixture_restore_rtc
#include "../../rng/device_3ds.c"
#undef ch_result_read_snapshot
#undef ch_result_read_progress
#undef raw_environment_restore_rtc
#include "../../rng/result/pinned_path.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t checks,entered,left,snapshot_reads,progress_reads,restore_calls,hud_published;
static uint32_t audio_fixture,output_gain,audio_stores,audio_busy,display_leaves,display_cancels;
static uint32_t display_steps;
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
uint32_t raw_device_test_tls(void){return 0x12345000u;}
int32_t svcSleepThread(int64_t n){(void)n;CHECK(0);return 0;}
Thread threadCreate(void (*f)(void *),void *u,size_t n,int32_t p,int32_t a,bool d){
    (void)f;(void)u;(void)n;(void)p;(void)a;(void)d;CHECK(0);return 0;
}
int32_t threadJoin(Thread t,uint64_t n){(void)t;(void)n;CHECK(0);return 0;}
void threadFree(Thread t){(void)t;CHECK(0);}
int raw_platform_sample(void *u,RawSample *s){RawPlatformBinding *b=u;
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
void raw_environment_backend_leave(RawEnvironmentBackend3ds *s){CHECK(s->active);s->active=0;left++;}
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
    (void)s;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;CHECK(0);return 0;
}
int raw_present_decode(const RawPresentBinding *s,const ChNativeContext *c,RawPresentArgs *a){
    (void)s;(void)c;(void)a;CHECK(0);return 0;
}
int raw_paused_display_update(RawPausedDisplay *s,const RawHud *h,int p){
    (void)s;CHECK(p==1);published_hud=*h;return RAW_PAUSE_READY;
}
int raw_paused_display_leave(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_leaves++;return 1;}
int raw_paused_display_cancel(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_cancels++;return 1;}
int raw_paused_display_step(RawPausedDisplay *s,int p){(void)s;CHECK(audio_fixture&&p==1);display_steps++;return 1;}
int raw_paused_display_game_present(RawPausedDisplay *s,const RawPauseRecord *p){(void)s;(void)p;CHECK(0);return 0;}
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
int raw_hud_3ds_sink_init(RawHud3dsSink *s,uint32_t n,const RawHudLayout *l){(void)s;(void)n;(void)l;CHECK(0);return 0;}
int raw_present_prepare(RawPresentBinding *s,const ChReadOps *o){(void)s;(void)o;CHECK(0);return 0;}
int raw_present_activate(RawPresentBinding *s,uint32_t n){(void)s;(void)n;CHECK(0);return 0;}
int raw_paused_3ds_init(RawPause3ds *s,const RawPresentBinding *b,RawPauseOps *o){(void)s;(void)b;(void)o;CHECK(0);return 0;}
int raw_paused_display_init(RawPausedDisplay *s,const RawPauseOps *o,const RawHudLayout *l){(void)s;(void)o;(void)l;CHECK(0);return 0;}
int raw_environment_backend_init(RawEnvironmentBackend3ds *s,Ch3dsBackend *b,const ChReadOps *r,RawEnvironmentOps *o){
    (void)s;(void)b;(void)r;(void)o;CHECK(0);return 0;
}
#include "cases.c"
