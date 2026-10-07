/* Real result gate and paired readers; constructed RAM fixtures are not
 * native execution. The checkpoint ROM bytes and four-frame reachability
 * were independently checked against saved original native execution. */
#include "result/result_gate.h"
#include "result/pinned_path.h"
#include "result/pinned_script_transitions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t checks;
#define CHECK(v) do { checks++; if(!(v)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#v);exit(1); } } while(0)
static const uint8_t fresh_returns[CH_RESULT_STACK_BYTES]={
    0x6b,0x04,0xff,0x31,0x9f,0x7b,0x1e,0x75,0xcd,0x74,0x20,0x02
};
static ChResultSource origin(void) {
    ChResultSource a;uint32_t i;
    memset(&a,0,sizeof(a));a.epoch=7;a.source_identity[0]=23;
    a.source.abi=CH_RESULT_ABI;a.source.rom=0x10000000u;a.source.wram=0x11000000u;
    a.source.io=0x12000000u;a.source.engine=0x16000000u;a.source.counter=100;
    a.source.map[0]=3;a.source.map[1]=52;
    a.source.script[0]=1;a.source.script[1]=255;a.source.script[2]=27;
    a.source.script[3]=0x54;a.source.script[4]=0x6e;a.source.applied_mask=0xffff;
    a.source.script_cpu_pc=0x460;a.source.guest_sp=0xc0d5;
    /* Normal prior Celebi30 residue, including identical future DV bytes. */
    for(i=0;i<CH_RESULT_ENEMY_BYTES;i++)a.source.enemy[i]=(uint8_t)(i+1);
    a.source.enemy[0]=251;a.source.enemy[13]=30;
    a.source.enemy[6]=0xfa;a.source.enemy[7]=0xaa;
    a.source.temp_enemy=251;a.source.cur_level=30;
    memcpy(a.source.path,ch_result_expected_path,CH_RESULT_PATH_BYTES);return a;
}
static ChResultFrameReceipt receipt(const ChResultGate *g,uint32_t current,int press) {
    ChResultFrameReceipt r;
    memset(&r,0,sizeof(r));r.epoch=g->binding.epoch;
    memcpy(r.source_identity,g->binding.source_identity,32);
    r.current_counter=current;r.previous_scan_counter=g->last_counter;r.engine_entries=1;
    r.previous_ordinary_supported=r.current_ordinary_supported=1;
    if(press) {
        r.press_observed=1;r.press_marker=0x1a82dcu;r.press_mask=0xfffe;
        r.raw_press_counter=101;r.effective_press_counter=102;
    }
    return r;
}
static int observe(ChResultGate *g,const ChResultSnapshot *s,int press) {
    ChResultProgress p;ChResultFrameReceipt r=receipt(g,s->counter,press);
    memcpy(&p,s,sizeof(p));return ch_result_observe_progress(g,&p,&r);
}
static ChResultSnapshot prime(ChResultGate *g) {
    ChResultSource a=origin();ChResultSnapshot s=a.source;
    CHECK(ch_result_begin(g,&a)==CH_RESULT_PENDING);
    s.counter=101;CHECK(observe(g,&s,0)==CH_RESULT_PENDING);
    s.counter=102;CHECK(observe(g,&s,0)==CH_RESULT_PENDING);
    s.counter=103;return s;
}
static void enemy_fields(ChResultSnapshot *s) {
    s->temp_enemy=s->temp_wild=251;s->cur_level=30;
    s->enemy[0]=251;s->enemy[13]=30;s->battle_mode=1;s->battle_type=11;
    s->script[3]=0x73;s->script[4]=0x6e;
}
static void checkpoint(ChResultSnapshot *s) {
    s->script_cpu_pc=0x460;s->guest_sp=0xc0c9;s->script_cpu_bank=15;
    s->script_fetch=s->rom+0x3c000u;
    memcpy(s->generation_stack,fresh_returns,CH_RESULT_STACK_BYTES);
}
static void admission_and_stale(void) {
    ChResultSource a=origin();ChResultGate g;ChResultSnapshot s;ChResultObservation out;
    CHECK(ch_result_begin(&g,&a)==CH_RESULT_PENDING);
    a.source.enemy[0]=19;a.source.temp_enemy=19;a.source.cur_level=42;
    CHECK(ch_result_begin(&g,&a)==CH_RESULT_PENDING);
    memset(a.source.enemy,0,CH_RESULT_ENEMY_BYTES);a.source.temp_enemy=0;a.source.cur_level=10;
    CHECK(ch_result_begin(&g,&a)==CH_RESULT_PENDING); /* item-selected level */
    a.source.battle_mode=1;CHECK(ch_result_begin(&g,&a)==CH_RESULT_REJECTED);
    a=origin();a.source.temp_wild=1;CHECK(ch_result_begin(&g,&a)==CH_RESULT_REJECTED);
    s=prime(&g);enemy_fields(&s);
    /* InitEnemyWildmon has entered mode1, but old Celebi30 still occupies
       enemy RAM until LoadEnemyMon clears it. All old result fields match. */
    s.script_cpu_pc=0x7612;s.script_cpu_bank=15;s.script_fetch=s.rom+0x3c000u;
    CHECK(observe(&g,&s,1)==CH_RESULT_PENDING&&!g.generation_observed&&!g.ready);
    memset(&out,0xff,sizeof(out));CHECK(ch_result_complete(&g,&s,&out)==CH_RESULT_REJECTED);
    CHECK(!out.complete&&!out.dv);
    s.counter++;memset(s.enemy+6,0,2);s.enemy[13]=0;
    CHECK(observe(&g,&s,1)==CH_RESULT_PENDING&&!g.generation_observed);
    s.counter++;s.enemy[6]=0xfa;s.enemy[7]=0xaa;checkpoint(&s);
    CHECK(observe(&g,&s,1)==CH_RESULT_PENDING&&!g.generation_observed); /* level not committed */
}
static void checkpoint_guards(void) {
    uint32_t i;ChResultGate g;ChResultSnapshot s;
    for(i=0;i<8;i++) {
        s=prime(&g);enemy_fields(&s);checkpoint(&s);
        switch(i) {
        case 0:s.guest_sp+=2;break; /* old bytes elsewhere are never scanned */
        case 1:s.guest_sp=0xc0c8;break;
        case 2:s.script_cpu_pc=0x461;break;
        case 3:s.script_cpu_bank=37;break;
        case 4:s.script_fetch=s.rom+0x38000u+s.script_cpu_pc;break;
        case 5:s.generation_stack[0]^=1;break;
        case 6:s.generation_stack[8]^=1;break;
        default:memmove(s.generation_stack+2,s.generation_stack,10);s.generation_stack[0]=0;break;
        }
        CHECK(observe(&g,&s,1)==CH_RESULT_PENDING&&!g.generation_observed&&!g.ready);
    }
    s=prime(&g);enemy_fields(&s);checkpoint(&s);
    CHECK(observe(&g,&s,0)==CH_RESULT_PENDING&&!g.generation_observed); /* no observed original A */
}
static void fresh_completion(void) {
    ChResultGate g;ChResultSnapshot s=prime(&g);ChResultObservation out;uint32_t i;
    enemy_fields(&s);checkpoint(&s);
    CHECK(observe(&g,&s,1)==CH_RESULT_READY&&g.generation_observed&&g.generation_counter==103);
    CHECK(g.generated_dv==0xfaaau); /* unchanged from prior enemy is still fresh */
    s.counter++;s.script_cpu_bank=19;s.script_fetch=s.rom+0x4c000u;
    s.guest_sp=0xc0c1;memset(s.generation_stack,0,sizeof(s.generation_stack));
    CHECK(observe(&g,&s,1)==CH_RESULT_READY&&g.generation_counter==103);
    CHECK(ch_result_complete(&g,&s,&out)==CH_RESULT_COMPLETE);
    CHECK(out.complete&&out.dv==0xfaaau&&out.species==251&&out.level==30);
    CHECK(!out.manual_hardware_verified&&out.epoch==7&&out.source_identity[0]==23);
    s=prime(&g);enemy_fields(&s);checkpoint(&s);CHECK(observe(&g,&s,1)==CH_RESULT_READY);
    s.counter++;s.enemy[6]^=1;
    CHECK(observe(&g,&s,1)==CH_RESULT_PENDING&&!g.ready);
    CHECK(ch_result_complete(&g,&s,&out)==CH_RESULT_REJECTED&&!out.complete);
    for(i=0;i<3;i++) {
        s=prime(&g);enemy_fields(&s);checkpoint(&s);CHECK(observe(&g,&s,1)==CH_RESULT_READY);
        /* Each new ROM range must be independently checked on completion. */
        s.path[1065+(i==0?0:i==1?21:30)]^=1;
        CHECK(ch_result_complete(&g,&s,&out)==CH_RESULT_REJECTED&&g.reason==CH_RESULT_BAD_PATH&&!out.complete);
    }
    s=prime(&g);enemy_fields(&s);checkpoint(&s);CHECK(observe(&g,&s,1)==CH_RESULT_READY);
    s.enemy[7]^=1;
    CHECK(ch_result_complete(&g,&s,&out)==CH_RESULT_REJECTED&&g.reason==CH_RESULT_BAD_UNIT&&!out.complete);
}
static void script_bank_base(void) {
    uint32_t i,j;ChResultSource a;ChResultGate g;ChResultSnapshot s;
    for(i=0;i<CH_SCRIPT_COMMIT_COUNT;i++) {
        const ChScriptCommit *c=&ch_script_commits[i];
        a=origin();CHECK(ch_result_begin(&g,&a)==CH_RESULT_PENDING);s=a.source;s.counter++;
        for(j=0;j<7;j++)if(c->known_mask&(1u<<j))s.script[j+2]=c->script[j];
        s.script_cpu_pc=c->pc;s.script_cpu_bank=37;s.script_fetch=s.rom+0x94000u;
        CHECK(observe(&g,&s,0)==CH_RESULT_PENDING);
        CHECK(ch_result_begin(&g,&a)==CH_RESULT_PENDING);
        s.script_fetch=s.rom+0x90000u+s.script_cpu_pc;
        CHECK(observe(&g,&s,0)==CH_RESULT_REJECTED&&g.reason==CH_RESULT_BAD_SCENE);
    }
}

static uint8_t rom[0xa0000],wram[0x8000],io[256],engine[0x154],globals[0x200],native_counter[16];
static uint32_t fail_address,stack_reads,stack_drift;
static uint8_t *mapped(uint32_t a,uint32_t n) {
    if(a>=0x10000000u&&(uint64_t)a+n<=UINT64_C(0x100a0000))return rom+a-0x10000000u;
    if(a>=0x11000000u&&(uint64_t)a+n<=UINT64_C(0x11008000))return wram+a-0x11000000u;
    if(a>=0x12000000u&&(uint64_t)a+n<=UINT64_C(0x12000100))return io+a-0x12000000u;
    if(a>=0x16000000u&&(uint64_t)a+n<=UINT64_C(0x16000154))return engine+a-0x16000000u;
    if(a>=0x22f5f0u&&(uint64_t)a+n<=UINT64_C(0x22f7f0))return globals+a-0x22f5f0u;
    if(a>=0x1a84f4u&&(uint64_t)a+n<=UINT64_C(0x1a8504))return native_counter+a-0x1a84f4u;
    return NULL;
}
static void store(uint32_t a,uint32_t v,uint32_t n) {
    uint8_t *p=mapped(a,n);uint32_t i;CHECK(p!=NULL);for(i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));
}
static int read_bytes(void *u,uint32_t a,void *out,uint32_t n) {
    uint8_t *p=mapped(a,n);(void)u;
    if(!p||a==fail_address)return 0;memcpy(out,p,n);
    if(a==0x110000c9u&&n==CH_RESULT_STACK_BYTES) {
        stack_reads++;if(stack_drift&&stack_reads==2)((uint8_t *)out)[0]^=1;
    }
    return 1;
}
static void seed(const ChResultSnapshot *s) {
    uint32_t i,k=0;
    memset(rom,0,sizeof(rom));memset(wram,0,sizeof(wram));memset(io,0,sizeof(io));
    memset(engine,0,sizeof(engine));memset(globals,0,sizeof(globals));
    for(i=0;i<CH_RESULT_PATH_COUNT;i++) {
        const ChResultPathRange *p=&ch_result_path_ranges[i];
        uint8_t *to=mapped(p->native_address?p->offset:s->rom+p->offset,p->size);
        CHECK(to!=NULL);memcpy(to,s->path+k,p->size);k+=p->size;
    }
    store(0x22f6c4,s->rom,4);store(0x22f6c8,s->wram,4);store(0x22f6dc,s->io,4);store(0x22f698,s->engine,4);
    store(s->engine+0x140,s->counter,4);store(s->wram+0x1204,s->temp_enemy,1);
    memcpy(wram+0x1206,s->enemy,CH_RESULT_ENEMY_BYTES);
    store(s->wram+0x122d,s->battle_mode,1);store(s->wram+0x122e,s->temp_wild,1);
    store(s->wram+0x122f,s->trainer,1);store(s->wram+0x1230,s->battle_type,1);
    store(s->wram+0x1143,s->cur_level,1);store(s->wram+0x2dc,s->link,1);store(s->wram+0xfc0,s->tower,1);
    memcpy(wram+0x1cb5,s->map,2);memcpy(wram+0x1437,s->script,9);
    store(0x22f766,s->applied_mask,2);store(0x22f5fc,s->script_cpu_pc,2);store(0x22f5fe,s->guest_sp,2);
    store(s->io+0x9d,s->script_cpu_bank,1);store(0x22f634,s->script_fetch,4);
    memcpy(wram+s->guest_sp-0xc000,s->generation_stack,CH_RESULT_STACK_BYTES);
    fail_address=stack_reads=stack_drift=0;
}
static int zeroed(const void *p,uint32_t n) {
    const uint8_t *b=p;uint32_t i;for(i=0;i<n;i++)if(b[i])return 0;return 1;
}
static void paired_stack_reader(void) {
    ChResultSource a=origin();ChResultSnapshot s=a.source,out;ChResultProgress p;
    ChReadOps read={NULL,read_bytes,NULL};enemy_fields(&s);checkpoint(&s);seed(&s);
    CHECK(ch_result_read_progress(&read,&p)&&stack_reads==2&&p.guest_sp==0xc0c9);
    CHECK(!memcmp(p.generation_stack,fresh_returns,CH_RESULT_STACK_BYTES));
    CHECK(ch_result_read_snapshot(&read,&out)&&!memcmp(&out,&s,sizeof(s)));
    stack_reads=0;stack_drift=1;memset(&p,0xff,sizeof(p));
    CHECK(!ch_result_read_progress(&read,&p)&&zeroed(&p,sizeof(p)));
    stack_drift=0;fail_address=0x110000c9u;
    CHECK(!ch_result_read_snapshot(&read,&out)&&zeroed(&out,sizeof(out)));
    fail_address=0;stack_reads=0;store(0x22f5fe,0xffff,2);
    CHECK(ch_result_read_progress(&read,&p)&&p.guest_sp==0xffff&&!stack_reads);
    CHECK(zeroed(p.generation_stack,sizeof(p.generation_stack))); /* no overflowing SP translation */
}
int main(void) {
    admission_and_stale();checkpoint_guards();fresh_completion();script_bank_base();paired_stack_reader();
    printf("passed: %u checks; residual saves, stale Celebi30, original live stack freshness, paired reads and ROM bank base\n",checks);
    return 0;
}
