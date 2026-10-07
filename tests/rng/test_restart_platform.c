#define main sampler_regressions_main
#include "test_platform_sampler_compat.c"
#undef main
#include "../../rng/platform_binding.h"
#include "../../rng/restart_lifecycle.h"
static uint32_t frontend_mode=1u,frontend_gate,frontend_reads,frontend_drift,frontend_fail;
static int lifecycle_read(void *u,uint32_t a,void *out,uint32_t n){
    if(a==RAW_FRONTEND_MODE_ADDRESS&&n==1u){frontend_reads++;
        if(frontend_fail)return 0;
        *(uint8_t *)out=(uint8_t)(frontend_mode+(frontend_drift&&frontend_reads>1u));return 1;
    }
    if(a==RAW_FRONTEND_SCAN_GATE_ADDRESS&&n==1u){*(uint8_t *)out=(uint8_t)frontend_gate;return 1;}
    return read_bytes(u,a,out,n);
}
static int physical(void *u,uint32_t *out){(void)u;*out=0;return 1;}
static void read_sample(RawPlatformBinding *b,RawSample *s){frontend_reads=0;CHECK(raw_platform_sample(b,s)==1);}
int main(void){SamplerFixture f={0};RawPlatformBinding b={0};RawRuntime r;RawSample s;uint32_t n;
    f.engine=0x27be7cu;f.counter=500u;f.engine_type=f.batch=1u;f.config_pointer=0x5e2000u;
    b.read=(ChReadOps){&f,lifecycle_read,physical};b.source_epoch=77u;
    read_sample(&b,&s);CHECK(s.frontend_valid&&s.frontend_mode==1u&&s.frontend_scan_gate==0u);
    CHECK(s.sample_complete&&s.counter_read&&s.counter==500u&&s.scene_epoch==77u);
    raw_runtime_init(&r,0);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    raw_runtime_begin_scan(&r,&s);CHECK(r.scan_open);
    frontend_mode=8u;
    for(n=0;n<4u;n++){read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,CH_KEY_L|CH_KEY_R,&s);
        raw_runtime_begin_scan(&r,&s);CHECK(r.frontend_suspended&&!r.fault&&!r.scan_open&&!r.view.command.kind);}
    frontend_mode=1u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,CH_KEY_L|CH_KEY_R,&s);
    CHECK(!r.frontend_suspended&&!r.fault&&r.runtime==CH_RUNTIME_RUNNING&&!r.view.command.kind);
    frontend_gate=2u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);CHECK(r.frontend_suspended&&!r.fault);
    frontend_gate=0u;f.counter=0u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    CHECK(r.fault==RAW_FAULT_ORDER&&r.controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
    frontend_drift=1u;frontend_reads=0u;CHECK(!raw_platform_sample(&b,&s));
    CHECK(!s.sample_complete&&b.last_sample_status==CH_SAMPLE_INCOHERENT);
    frontend_drift=0u;frontend_fail=1u;CHECK(!raw_platform_sample(&b,&s));
    CHECK(!s.frontend_valid&&!s.sample_complete&&b.last_sample_status==CH_SAMPLE_READ_FAILED);
    /* Drive the real paired platform sampler through a checked new session:
       native menu, loading scan, initialized engine and two game units. */
    frontend_fail=0u;frontend_mode=8u;f.counter=40000u;f.phase=2u;b.source_epoch=78u;
    CHECK(raw_runtime_restart_checked(&r,78u,CH_KEY_L|CH_KEY_R));
    read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,CH_KEY_L|CH_KEY_R,&s);
    CHECK(r.restart_bootstrap_pending&&r.frontend_suspended&&!r.controls.initialized_scene&&!r.fault);
    frontend_mode=1u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    CHECK(r.restart_bootstrap_pending&&!r.controls.initialized_scene&&!r.fault);
    f.counter=0u;f.phase=0u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    CHECK(r.restart_bootstrap_pending&&!r.controls.initialized_scene&&!r.fault);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    f.counter=1u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    CHECK(!r.restart_bootstrap_pending&&r.controls.last_counter==1u&&!r.fault);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    f.counter=2u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    CHECK(r.controls.last_counter==2u&&!r.fault);
    raw_runtime_begin_scan(&r,&s);raw_runtime_after_scan(&r,&s);raw_runtime_engine_entry(&r,&s);
    f.counter=0u;read_sample(&b,&s);raw_runtime_before_scan(&r,&s);raw_runtime_poll_player(&r,0,&s);
    CHECK(r.fault==RAW_FAULT_ORDER&&r.controls.fault==CH_FAULT_COUNTER_DISCONTINUITY);
    printf("passed: %u checks; actual platform frontend readback, menu suspension and strict regression\n",checks);return 0;
}
