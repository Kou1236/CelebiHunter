#include "step_trace.h"
#include <stdio.h>
#include <stdlib.h>
static uint32_t checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"step trace line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
int main(void){RawStepTrace t,saved;uint32_t i;uint64_t start=UINT64_C(0x100000000);
    raw_step_trace_init(&t);saved=t;
    /* Ordinary running callbacks cannot manufacture a trace or alter it. */
    for(i=0;i<100;i++){
        CHECK(!raw_step_trace_present_begin(&t,start+i));
        CHECK(!raw_step_trace_present_end(&t,start+i));
        CHECK(!raw_step_trace_actual_boundary(&t,9,101,start+i));
        CHECK(!raw_step_trace_clone_published(&t,start+i));
        CHECK(!raw_step_trace_clone_acknowledged(&t,start+i));
    }
    CHECK(!memcmp(&t,&saved,sizeof(t)));
    CHECK(raw_step_trace_accept(&t,1,9,100,101,start));
    CHECK(t.request_id==1&&t.epoch==9&&t.origin_counter==100&&t.target_counter==101);
    CHECK(raw_step_trace_present_begin(&t,start+2));
    CHECK(raw_step_trace_present_end(&t,start+5));
    CHECK(raw_step_trace_actual_boundary(&t,9,101,start+8));
    CHECK(raw_step_trace_clone_published(&t,start+20));
    CHECK(raw_step_trace_clone_acknowledged(&t,start+40));
    CHECK(t.seen==63&&!t.fault&&t.accepted_tick==start);
    CHECK(t.present_end_tick-t.present_begin_tick==3);
    CHECK(t.actual_boundary_tick-t.accepted_tick==8);
    CHECK(t.clone_acknowledged_tick-t.actual_boundary_tick==32);
    saved=t;
    CHECK(!raw_step_trace_accept(&t,1,9,100,101,start+100));
    CHECK(!raw_step_trace_present_begin(&t,start+100));
    CHECK(!raw_step_trace_clone_acknowledged(&t,start+100));
    CHECK(!memcmp(&t,&saved,sizeof(t)));
    /* A genuine unit may not present. Zero ticks and counter wrap are valid. */
    CHECK(raw_step_trace_accept(&t,2,10,UINT32_MAX,0,0));
    CHECK(raw_step_trace_actual_boundary(&t,10,0,1));
    CHECK(raw_step_trace_clone_published(&t,2));
    CHECK(raw_step_trace_clone_acknowledged(&t,3));
    CHECK(!(t.seen&(RAW_STEP_TRACE_PRESENT_BEGIN|RAW_STEP_TRACE_PRESENT_END))&&!t.fault);
    CHECK(raw_step_trace_accept(&t,3,10,0,1,5));
    CHECK(!raw_step_trace_clone_acknowledged(&t,6)&&t.fault==RAW_STEP_TRACE_BAD_ORDER);
    CHECK(!raw_step_trace_present_end(&t,6));
    CHECK(raw_step_trace_present_begin(&t,6));
    CHECK(!raw_step_trace_present_end(&t,4)&&t.fault&RAW_STEP_TRACE_TICK_BACKWARD);
    CHECK(!raw_step_trace_actual_boundary(&t,10,1,8));
    CHECK(raw_step_trace_present_end(&t,7));
    CHECK(!raw_step_trace_actual_boundary(&t,11,1,8)&&t.fault&RAW_STEP_TRACE_BAD_BOUNDARY);
    CHECK(!raw_step_trace_actual_boundary(&t,10,2,8));
    CHECK(raw_step_trace_actual_boundary(&t,10,1,8));
    CHECK(!raw_step_trace_clone_published(&t,7));
    CHECK(raw_step_trace_clone_published(&t,9));
    CHECK(raw_step_trace_clone_acknowledged(&t,10));
    CHECK(t.fault==(RAW_STEP_TRACE_BAD_ORDER|RAW_STEP_TRACE_TICK_BACKWARD|RAW_STEP_TRACE_BAD_BOUNDARY));
    CHECK(raw_step_trace_accept(&t,4,10,1,2,11));
    CHECK(t.fault==14&&t.seen==RAW_STEP_TRACE_ACCEPTED&&t.present_begin_tick==0);
    CHECK(!raw_step_trace_accept(&t,0,10,1,2,12)&&t.fault&RAW_STEP_TRACE_BAD_REQUEST);
    CHECK(!raw_step_trace_accept(&t,5,0,1,2,12));
    CHECK(!raw_step_trace_accept(&t,5,10,1,3,12));
    CHECK(t.request_id==4&&t.accepted_tick==11);
    raw_step_trace_init(&t);CHECK(!t.seen&&!t.fault);
    printf("step trace: %u checks passed (observation only).\n",checks);return 0;
}
