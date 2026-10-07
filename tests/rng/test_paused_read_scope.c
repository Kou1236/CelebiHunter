/* Compile the real native pause adapter. Only SDK memory and the original
 * submission endpoint are mocked; every pinned guard and paired read runs. */
#include "hud/paused_3ds.h"
#include "platform/read_scope.h"
#include <stdio.h>
#include <stdlib.h>
static void native_publish(uint32_t,uint32_t,const void *,const void *,uint32_t,uint32_t,uint32_t);
#undef RAW_PRESENT_TARGET
#define RAW_PRESENT_TARGET ((uintptr_t)native_publish)
#ifdef RAW_PAUSE_TEST_OLD_STRICT
#define raw_pause_snapshot_same_image(a,b) (!memcmp((a),(b),sizeof(*(a))))
#endif
#include "../../rng/hud/paused_3ds.c"
static RawPause3ds owner;
static RawPresentBinding binding;
static RawPauseOps ops;
static ChReadScope scope;
static RawPauseSnapshot image;
static uint32_t checks,reads,queries,begins,ends,fail_at,corrupt_guard,drift,native_calls;
static uint32_t epoch_reads,control_reads,queue_reads,contract_reads,begin_rejected;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"pause scope line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
int32_t svcQueryMemory(MemInfo *m,PageInfo *p,uint32_t a){
    queries++;memset(m,0,sizeof(*m));memset(p,0,sizeof(*p));
    if(a>=0x00100000u&&a<0x00300000u)*m=(MemInfo){0x00100000u,0x00200000u,MEMPERM_READ,5};
    else if(a>=0x00600000u&&a<0x00700000u)*m=(MemInfo){0x00600000u,0x00100000u,MEMPERM_READ,5};
    else if(a>=0x10000000u&&a<0x10010000u)*m=(MemInfo){0x10000000u,0x00010000u,MEMPERM_READ,5};
    else return -1;
    return 0;
}
static int query(void *u,uint32_t a,ChReadRegion *region){MemInfo m;PageInfo p;(void)u;
    if(svcQueryMemory(&m,&p,a)<0)return 0;
    *region=(ChReadRegion){m.base_addr,m.size};return 1;
}
static int read_fixture(void *u,uint32_t a,void *out,uint32_t bytes){uint32_t i,v=0;(void)u;
    reads++;
    if(reads==fail_at||!ch_read_scope_readable(&scope,query,0,a,bytes))return 0;
    if(a==RAW_PRESENT_SITE&&bytes==4){v=RAW_PRESENT_ORIGINAL_BL;contract_reads++;}
    else {
        for(i=0;i<sizeof(raw_pause_guard_blocks)/sizeof(raw_pause_guard_blocks[0]);i++)
            if(a==raw_pause_guard_blocks[i].address&&bytes==raw_pause_guard_blocks[i].count*4){
                memcpy(out,raw_pause_guard_blocks[i].words,bytes);
                if(corrupt_guard)((uint8_t *)out)[0]^=1;
                return 1;
            }
        if(a==0x232398u)v=image.context;
        else if(a==image.context+0x5cu)v=image.descriptor;
        else if(a==image.context+0x34u)v=image.event_queue;
        else if(a==image.context+0x40u)v=image.shared_base;
        else if(a==image.context+0x74u)v=image.thread_id;
        else if(a==image.event_queue){v=image.queue_control;queue_reads++;
            if(drift==2&&queue_reads==2)v=(v&~0xffu)|((v+1u)%52u);}
        else if(a==image.descriptor){v=image.control;control_reads++;}
        else if(a==RAW_PAUSE_GRAPHICS_BUSY)v=image.graphics_busy;
        else if(a==RAW_PAUSE_TOP_EPOCH){v=image.top_epoch;epoch_reads++;if(drift==1&&epoch_reads==2)v++;}
        else if(a==image.descriptor+4+28*(image.control&0xffu)&&bytes==sizeof(image.record)){
            memcpy(out,&image.record,bytes);return 1;
        }else {CHECK(0);return 0;}
    }
    CHECK(bytes<=4);memcpy(out,&v,bytes);return 1;
}
int raw_present_contract_current(const RawPresentBinding *b){uint32_t word;
    return b->read.read_bytes(b->read.user,RAW_PRESENT_SITE,&word,4)&&word==RAW_PRESENT_ORIGINAL_BL;
}
static int begin(void *u){CHECK(u==&scope);begins++;
    if(begin_rejected)return 0;
    CHECK(ch_read_scope_begin(&scope));return 1;
}
static void end(void *u){CHECK(u==&scope&&scope.active);ends++;ch_read_scope_end(&scope);}
int32_t svcFlushProcessDataCache(uint32_t a,uint32_t b,uint32_t c){(void)a;(void)b;(void)c;CHECK(!scope.active);return 0;}
uint32_t svcConvertVAToPA(const void *p,bool write){(void)p;(void)write;CHECK(!scope.active);return 0;}
static void native_publish(uint32_t screen,uint32_t swap,const void *a,const void *b,
    uint32_t stride,uint32_t format,uint32_t select){
    CHECK(!scope.active&&screen==0&&swap==1&&select==1);
    CHECK((uintptr_t)a==0x18000000u&&!b&&stride==720&&format==1);native_calls++;
}
static void setup(void){
    memset(&scope,0,sizeof(scope));memset(&image,0,sizeof(image));
    reads=queries=begins=ends=fail_at=corrupt_guard=drift=native_calls=0;
    epoch_reads=control_reads=queue_reads=contract_reads=begin_rejected=0;
    image.context=0x232500;image.shared_base=image.event_queue=0x10002000;
    image.descriptor=image.shared_base+0x200;image.top_epoch=9;
    image.record=(RawPauseRecord){0,0x16000000,0,720,1,0,0};
    memset(&binding,0,sizeof(binding));binding.prepared=1;binding.read=(ChReadOps){0,read_fixture,0};
    CHECK(raw_paused_3ds_init(&owner,&binding,&ops));
    owner.read_scope_user=&scope;owner.read_scope_begin=begin;owner.read_scope_end=end;
}
static void publication_checks(void){RawPauseSnapshot expected;RawPauseRecord submit;
    uint32_t i,epoch;
    submit=(RawPauseRecord){1,0x18000000,0,720,1,1,0};
    for(i=0;i<4;i++){
        setup();
        if(i==1)image.queue_control=51;
        if(i==3)image.top_epoch=0x7ffffffeu;
        CHECK(ops.snapshot(ops.user,&expected)==1);
        if(i==0)image.queue_control=1; /* Head only, including bottom events. */
        if(i==1){image.queue_control=0;image.top_epoch++;}
        if(i==2)image.top_epoch++;
        if(i==3){image.queue_control=1;image.top_epoch=0;}
        epoch=0xdeadbeefu;
        CHECK(ops.publish(ops.user,&expected,&submit,&epoch)==1);
        CHECK(native_calls==1&&epoch==image.top_epoch&&!scope.active&&begins==ends);
        CHECK(epoch_reads==4&&control_reads==4&&queue_reads==4);
    }
    /* Each mismatch remains a no-write result, including opaque upper bits.
       The current receipt is still internally coherent for these cases. */
    for(i=0;i<24;i++){
        setup();CHECK(ops.snapshot(ops.user,&expected)==1);
        switch(i){
        case 0:image.queue_control=0x100;break;
        case 1:image.queue_control=0x10000;break;
        case 2:image.graphics_busy=1;break;
        case 3:image.context+=4;break;
        case 4:image.control=1;break;
        case 5:image.record.fb_a+=4;break;
        case 6:image.queue_control=0x01000000;break;
        case 7:image.top_epoch--;break;
        case 8:image.top_epoch=0x7fffffff;break;
        case 9:image.queue_control=52;break;
        case 10:image.shared_base+=0x100;image.event_queue=image.shared_base;
            image.descriptor=image.shared_base+0x200;break;
        case 11:image.thread_id=1;image.event_queue=image.shared_base+64;
            image.descriptor=image.shared_base+0x280;break;
        case 12:expected.queue_control=0x100;break;
        case 13:expected.graphics_busy=1;break;
        case 14:expected.control=0x100;break;
        case 15:image.control=0x100;break;
        case 16:expected.top_epoch=0x7fffffff;break;
        case 17:expected.queue_control=52;break;
        case 18:image.record.fb_b+=4;break;
        case 19:image.record.stride+=4;break;
        case 20:image.record.format^=1;break;
        case 21:image.record.display_select^=1;break;
        case 22:image.record.zero=1;break;
        case 23:image.record.swap^=1;break;
        }
        epoch=0xdeadbeefu;
        CHECK(ops.publish(ops.user,&expected,&submit,&epoch)!=1);
        CHECK(native_calls==0&&epoch==0xdeadbeefu&&!scope.active&&begins==ends);
    }
    /* Movement within either paired observation still requires a retry. */
    for(i=1;i<=2;i++){
        setup();CHECK(ops.snapshot(ops.user,&expected)==1);
        epoch_reads=queue_reads=0;drift=i;epoch=0xdeadbeefu;
        CHECK(ops.publish(ops.user,&expected,&submit,&epoch)==0);
        CHECK(native_calls==0&&epoch==0xdeadbeefu&&!scope.active&&begins==ends);
    }
    setup();CHECK(ops.snapshot(ops.user,&expected)==1);fail_at=reads+1;epoch=0xdeadbeefu;
    CHECK(ops.publish(ops.user,&expected,&submit,&epoch)==-1&&epoch==0xdeadbeefu&&native_calls==0);
    setup();CHECK(ops.snapshot(ops.user,&expected)==1);
    CHECK(ops.publish(ops.user,&expected,&submit,0)==-1&&native_calls==0&&!scope.active);
}
int main(void){RawPauseSnapshot a,b;RawPauseRecord submit;uint32_t scoped_reads,scoped_queries,epoch;
    setup();CHECK(ops.snapshot(ops.user,&a)==1);
    CHECK(!scope.active&&begins==1&&ends==1&&epoch_reads==2&&control_reads==2&&contract_reads==1);
    scoped_reads=reads;scoped_queries=queries;CHECK(scoped_queries==3);
    setup();owner.read_scope_begin=0;owner.read_scope_end=0;
    CHECK(ops.snapshot(ops.user,&b)==1&&!memcmp(&a,&b,sizeof(a)));
    CHECK(reads==scoped_reads&&queries==reads&&queries>scoped_queries&&epoch_reads==2&&control_reads==2);
    /* Every failed contract/read/coherence exit releases this read-only scope. */
    setup();fail_at=1;CHECK(ops.snapshot(ops.user,&a)==-1&&!scope.active&&begins==ends);
    setup();fail_at=15;CHECK(ops.snapshot(ops.user,&a)==-1&&!scope.active&&begins==ends);
    setup();corrupt_guard=1;CHECK(ops.snapshot(ops.user,&a)==-1&&!scope.active&&begins==ends);
    setup();drift=1;CHECK(ops.snapshot(ops.user,&a)==0&&!scope.active&&begins==ends&&epoch_reads==2);
    setup();begin_rejected=1;CHECK(ops.snapshot(ops.user,&a)==1&&!scope.active&&begins==1&&ends==0);
    setup();owner.read_scope_end=0;CHECK(ops.snapshot(ops.user,&a)==1&&begins==0&&!scope.active);
    setup();CHECK(ops.snapshot(ops.user,&a)==1);
    submit=(RawPauseRecord){1,0x18000000,0,720,1,1,0};
    CHECK(ops.publish(ops.user,&a,&submit,&epoch)==1&&native_calls==1&&!scope.active&&begins==ends);
    CHECK(epoch==a.top_epoch);
    CHECK(epoch_reads==4&&control_reads==4);
    publication_checks();
    printf("paused read scope: %u checks; %u reads retain all checks, mapping queries %u -> %u.\n",
        checks,scoped_reads,scoped_reads,scoped_queries);return 0;
}
