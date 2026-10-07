#include "../../rng/platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t engine, counter, engine_type, recording, queued, mode, flags;
    uint32_t batch, phase, config_pointer, config_mode, previous_keys;
} SamplerFixture;

static uint32_t checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)

static void put32(void *out,uint32_t value){uint8_t *b=out;
    b[0]=(uint8_t)value;b[1]=(uint8_t)(value>>8);b[2]=(uint8_t)(value>>16);b[3]=(uint8_t)(value>>24);}
static int read_bytes(void *user,uint32_t address,void *out,uint32_t size){SamplerFixture *f=user;uint32_t value;
    if(address==CH_ENGINE_POINTER_ADDRESS&&size==4u)value=f->engine;
    else if(address==f->engine&&size==4u)value=f->engine_type;
    else if(address==f->engine+0x80u&&size==4u)value=f->recording;
    else if(address==f->engine+0xdcu&&size==4u)value=f->queued;
    else if(address==f->engine+0x140u&&size==4u)value=f->counter;
    else if(address==f->engine+0x144u&&size==4u)value=f->mode;
    else if(address==f->engine+0x150u&&size==4u)value=f->flags;
    else if(address==CH_HOST_CACHE_ADDRESS&&size==16u){memset(out,0,size);return 1;}
    else if(address==CH_HOST_BATCH_ADDRESS&&size==4u)value=f->batch;
    else if(address==CH_HOST_PHASE_ADDRESS&&size==4u)value=f->phase;
    else if(address==CH_CONFIG_POINTER_ADDRESS&&size==4u)value=f->config_pointer;
    else if(address==f->config_pointer&&size==4u)value=f->config_mode;
    else if(address==0x0023278cu&&size==4u)value=f->previous_keys;
    else if(address==0x0022f764u&&size==4u){uint8_t *b=out;b[0]=0;b[1]=0;b[2]=0xff;b[3]=0xff;return 1;}
    else return 0;
    put32(out,value);return 1;
}

int main(void){SamplerFixture f;ChBoundarySample sample;ChReadOps ops;
    memset(&f,0,sizeof(f));f.engine=0x0027be7cu;f.counter=500;f.engine_type=1;f.batch=1;f.config_pointer=0x005e2000u;
    ops=(ChReadOps){&f,read_bytes,0};
    CHECK(ch_sample_boundary(&ops,&sample)==CH_SAMPLE_OK);
    CHECK(sample.engine_pointer==f.engine&&sample.counter==500&&sample.ordinary_supported);
    f.engine=0x0028be7cu;
    CHECK(ch_sample_boundary(&ops,&sample)==CH_SAMPLE_OK);
    CHECK(sample.engine_pointer==f.engine&&sample.counter==500&&sample.ordinary_supported);
    f.engine_type=2;
    CHECK(ch_sample_boundary(&ops,&sample)==CH_SAMPLE_UNSUPPORTED);
    CHECK(sample.complete&&sample.coherent&&!sample.ordinary_supported);
    printf("passed: %u checks; baseline and relocated engine pointers accepted, invalid engine type rejected\n",checks);return 0;
}
