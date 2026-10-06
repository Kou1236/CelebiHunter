#include "result_gate.h"
#include "pinned_path.h"
#include "pinned_script_transitions.h"
#include <stddef.h>
#include <string.h>
typedef char ChResultStatePrefixMatches[(offsetof(ChResultSnapshot,path)==sizeof(ChResultProgress))?1:-1];
static void progress_from_snapshot(ChResultProgress *p,const ChResultSnapshot *s) {
    memcpy(p,s,sizeof(*p));
}
static uint32_t little(const uint8_t *p,uint32_t n) {
    uint32_t i,v=0;for(i=0;i<n;i++)v|=(uint32_t)p[i]<<(8*i);return v;
}
static int word(const ChReadOps *o,uint32_t a,uint32_t *v) {
    uint8_t b[4];if(!o->read_bytes(o->user,a,b,4))return 0;*v=little(b,4);return 1;
}
static int mapping_valid(const ChResultProgress *s) {
    return s->rom&&s->wram&&s->io&&s->engine&&
        !(s->rom&3u)&&!(s->wram&3u)&&!(s->io&3u)&&!(s->engine&3u)&&
        s->rom<=UINT32_MAX-0xa0000u&&s->wram<=UINT32_MAX-0x8000u&&
        s->io<=UINT32_MAX-256u&&s->engine<=UINT32_MAX-0x154u;
}
static int once_progress(const ChReadOps *o,ChResultProgress *s) {
    uint8_t b[45];
    memset(s,0,sizeof(*s));s->abi=CH_RESULT_ABI;
    if(!word(o,0x22f6c4,&s->rom)||!word(o,0x22f6c8,&s->wram)||
       !word(o,0x22f6dc,&s->io)||!word(o,0x22f698,&s->engine)||!mapping_valid(s)||
       !word(o,s->engine+0x140,&s->counter))return 0;
    if(!o->read_bytes(o->user,s->wram+0x1204,b,45))return 0;
    s->temp_enemy=b[0];memcpy(s->enemy,b+2,CH_RESULT_ENEMY_BYTES);
    s->battle_mode=b[41];s->temp_wild=b[42];s->trainer=b[43];s->battle_type=b[44];
    if(!o->read_bytes(o->user,s->wram+0x1143,&s->cur_level,1)||
       !o->read_bytes(o->user,s->wram+0x2dc,&s->link,1)||
       !o->read_bytes(o->user,s->wram+0xfc0,&s->tower,1)||
       !o->read_bytes(o->user,s->wram+0x1cb5,s->map,2)||
       !o->read_bytes(o->user,s->wram+0x1437,s->script,9)||
       !o->read_bytes(o->user,0x22f766,b,2))return 0;
    s->applied_mask=(uint16_t)little(b,2);
    if(!o->read_bytes(o->user,0x22f5fc,b,2))return 0;
    s->script_cpu_pc=little(b,2);
    if(!o->read_bytes(o->user,s->io+0x9du,b,1)||
       !word(o,0x22f634,&s->script_fetch))return 0;
    s->script_cpu_bank=b[0];
    return 1;
}
int ch_result_read_progress(const ChReadOps *o,ChResultProgress *out) {
    ChResultProgress a,b;
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!o||!o->read_bytes||!once_progress(o,&a)||!once_progress(o,&b)||memcmp(&a,&b,sizeof(a)))return 0;
    *out=a;return 1;
}
static int once(const ChReadOps *o,ChResultSnapshot *s) {
    ChResultProgress p;uint32_t i,offset=0;
    memset(s,0,sizeof(*s));
    if(!once_progress(o,&p))return 0;
    memcpy(s,&p,sizeof(p));
    for(i=0;i<CH_RESULT_PATH_COUNT;i++) {
        const ChResultPathRange *r=&ch_result_path_ranges[i];
        uint32_t a=r->native_address?r->offset:s->rom+r->offset;
        if(!o->read_bytes(o->user,a,s->path+offset,r->size))return 0;
        offset+=r->size;
    }
    return offset==CH_RESULT_PATH_BYTES;
}
int ch_result_read_snapshot(const ChReadOps *o,ChResultSnapshot *out) {
    ChResultSnapshot a,b;
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!o||!o->read_bytes||!once(o,&a)||!once(o,&b)||memcmp(&a,&b,sizeof(a)))return 0;
    *out=a;return 1;
}
static int path_valid(const ChResultSnapshot *s) {
    return !memcmp(s->path,ch_result_expected_path,CH_RESULT_PATH_BYTES);
}
static int map_scene(const ChResultProgress *s) {
    return !s->link&&!(s->tower&1u)&&!s->trainer&&s->map[0]==3u&&s->map[1]==52u&&
        s->script[0]>=1u&&s->script[0]<=3u&&
        (s->script[1]==0u||s->script[1]==255u);
}
static int script_commit(const ChResultProgress *s,const ChResultProgress *origin) {
    uint32_t i,j;
    /* Original GB stores of stack size/bank/PC can straddle the counted
       unit. Admit only the exact original post-instruction tuples, with
       original bank and fetch pointer independently proving that code. */
    if(s->script[0]!=1u||s->script[1]!=255u||s->script_cpu_bank!=37u||
       s->script_cpu_pc<0x4000u||s->script_cpu_pc>=0x8000u||
       s->script_fetch!=s->rom+0x90000u+s->script_cpu_pc)return 0;
    for(i=0;i<CH_SCRIPT_COMMIT_COUNT;i++) {
        const ChScriptCommit *c=&ch_script_commits[i];
        if(c->pc!=s->script_cpu_pc)continue;
        for(j=0;j<7u;j++) {
            uint8_t expected=(c->known_mask&(1u<<j))?c->script[j]:origin->script[j+2u];
            if(s->script[j+2u]!=expected)break;
        }
        if(j==7u)return 1;
    }
    return 0;
}
static int script_scene(const ChResultProgress *s,const ChResultProgress *origin) {
    uint32_t pc=little(s->script+3,2);
    if(s->script[2]==27u&&s->script[5]==0u&&pc>=0x6e54u&&pc<=0x6e73u)return 1;
    /* Original 1B:6E57 showemote calls 25:73B6 and saves 1B:6E5B.
       GetScriptByte may straddle a native counted unit between argument
       bytes, so include every next-PC prefix of this exact 11-byte child.
       No other child bank, parent or additional nesting is admitted. */
    if(s->script[2]==37u&&s->script[5]==1u&&s->script[6]==27u&&
       little(s->script+7,2)==0x6e5bu&&pc>=0x73b6u&&pc<=0x73c1u)return 1;
    return script_commit(s,origin);
}
void ch_result_invalidate(ChResultGate *g,uint32_t reason) {
    if(g){g->status=(uint32_t)CH_RESULT_REJECTED;g->reason=reason;}
}
static int reject(ChResultGate *g,uint32_t reason) {
    ch_result_invalidate(g,reason);return CH_RESULT_REJECTED;
}
int ch_result_begin(ChResultGate *g,const ChResultSource *source) {
    uint32_t i,any=0;const ChResultSnapshot *s;ChResultProgress p;
    if(!g)return CH_RESULT_REJECTED;
    memset(g,0,sizeof(*g));
    g->abi=CH_RESULT_ABI;
    if(!source||!source->epoch)return reject(g,CH_RESULT_BAD_SOURCE);
    s=&source->source;
    progress_from_snapshot(&p,s);
    for(i=0;i<32;i++)any|=source->source_identity[i];
    if(!any||s->abi!=CH_RESULT_ABI||!mapping_valid(&p)||!map_scene(&p)||!script_scene(&p,&p)||
       s->battle_mode||s->battle_type||s->cur_level||s->temp_enemy||s->temp_wild||
       s->script[0]!=1u||s->script[1]!=255u||
       little(s->script+3,2)!=0x6e54u||(s->applied_mask&1u)==0)return reject(g,CH_RESULT_BAD_SOURCE);
    if(!path_valid(s))return reject(g,CH_RESULT_BAD_PATH);
    for(i=0;i<CH_RESULT_ENEMY_BYTES;i++)if(s->enemy[i])return reject(g,CH_RESULT_BAD_SOURCE);
    g->binding=*source;g->last_counter=s->counter;return CH_RESULT_PENDING;
}
static int future(uint32_t a,uint32_t b) {uint32_t d=a-b;return d&&d<0x80000000u;}
static int press_valid(const ChResultGate *g,const ChResultFrameReceipt *r) {
    return r->press_marker==0x1a82dcu&&!(r->press_mask&1u)&&
        r->effective_press_counter-r->raw_press_counter==1u&&
        future(r->effective_press_counter,g->binding.source.counter)&&
        future(r->current_counter,r->effective_press_counter);
}
static int generation_ready(const ChResultProgress *s) {
    return s->enemy[0]==251u&&s->enemy[13]==30u&&s->temp_enemy==251u&&s->temp_wild==251u&&
        s->cur_level==30u&&s->battle_mode==1u&&s->battle_type==11u&&little(s->script+3,2)==0x6e73u&&
        s->script[0]==1u&&s->script[1]==255u&&s->script[2]==27u&&s->script[5]==0u;
}
int ch_result_observe_progress(ChResultGate *g,const ChResultProgress *s,
                               const ChResultFrameReceipt *r) {
    uint32_t first_press,delta;ChResultProgress origin;
    if(!g||!s||!r||g->abi!=CH_RESULT_ABI||g->status!=(uint32_t)CH_RESULT_PENDING)
        return CH_RESULT_REJECTED;
    progress_from_snapshot(&origin,&g->binding.source);
    if(r->epoch!=g->binding.epoch||memcmp(r->source_identity,g->binding.source_identity,32))
        return reject(g,CH_RESULT_BAD_IDENTITY);
    if(s->abi!=CH_RESULT_ABI||!mapping_valid(s)||s->rom!=origin.rom||s->wram!=origin.wram||
       s->io!=origin.io||s->engine!=origin.engine)return reject(g,CH_RESULT_BAD_MAPPING);
    first_press=r->press_observed&&!g->press_observed;
    if(r->press_observed&&(!press_valid(g,r)||(g->press_observed&&
        (r->raw_press_counter!=g->raw_press_counter||r->effective_press_counter!=g->effective_press_counter))))
        return reject(g,CH_RESULT_BAD_INPUT);
    delta=s->counter-g->last_counter;
    if(r->current_counter!=s->counter||r->previous_scan_counter!=g->last_counter||
       delta>=0x80000000u)
        return reject(g,CH_RESULT_BAD_UNIT);
    if(first_press) {
        /* The previous observed boundary is the actual effective-A scan.
           The current boundary is already after A; its prediction-domain
           fields cannot invalidate an original A delivered before it. */
        if(g->last_counter!=r->effective_press_counter||!r->previous_ordinary_supported)
            return reject(g,CH_RESULT_BAD_UNIT);
    } else if(!g->press_observed&&(delta!=1u||r->engine_entries!=1u||
              !r->previous_ordinary_supported||!r->current_ordinary_supported))
        return reject(g,CH_RESULT_BAD_UNIT);
    if(!map_scene(s)||!script_scene(s,&origin))return reject(g,CH_RESULT_BAD_SCENE);
    if(first_press) {
        g->press_observed=1;g->raw_press_counter=r->raw_press_counter;
        g->effective_press_counter=r->effective_press_counter;
    }
    g->last_counter=s->counter;g->observations++;
    g->ready=0;memset(&g->ready_state,0,sizeof(g->ready_state));
    if(!g->press_observed)return CH_RESULT_PENDING;
    /* Generation and the manual input certificate are separate facts. A
       real completed enemy remains observable while A is held, after a late
       release, or when no release receipt exists. Never read release fields
       here or set a manual hardware verification flag. */
    /* Level is written only AFTER both original DV byte stores on Celebi's
       pinned LoadEnemyMon path. Source zero rejects a stale prior enemy. */
    if(!generation_ready(s))return CH_RESULT_PENDING;
    g->ready=1;g->ready_state=*s;return CH_RESULT_READY;
}
int ch_result_complete(ChResultGate *g,const ChResultSnapshot *s,ChResultObservation *out) {
    ChResultProgress p,origin;
    if(out)memset(out,0,sizeof(*out));
    if(!g||!s||!out||g->abi!=CH_RESULT_ABI||g->status!=(uint32_t)CH_RESULT_PENDING||
       !g->ready||!g->press_observed)return CH_RESULT_REJECTED;
    progress_from_snapshot(&p,s);progress_from_snapshot(&origin,&g->binding.source);
    if(s->abi!=CH_RESULT_ABI||!mapping_valid(&p)||s->rom!=origin.rom||s->wram!=origin.wram||
       s->io!=origin.io||s->engine!=origin.engine)return reject(g,CH_RESULT_BAD_MAPPING);
    if(!path_valid(s))return reject(g,CH_RESULT_BAD_PATH);
    if(s->counter!=g->last_counter||memcmp(&p,&g->ready_state,sizeof(p)))
        return reject(g,CH_RESULT_BAD_UNIT);
    if(!map_scene(&p)||!script_scene(&p,&origin)||!generation_ready(&p))
        return reject(g,CH_RESULT_BAD_SCENE);
    out->abi=CH_RESULT_ABI;out->counter=s->counter;out->species=251;out->level=30;
    out->dv=(uint32_t)s->enemy[6]<<8|s->enemy[7];out->complete=1;
    out->epoch=g->binding.epoch;memcpy(out->source_identity,g->binding.source_identity,32);
    out->observed=*s;g->status=CH_RESULT_COMPLETE;return CH_RESULT_COMPLETE;
}
int ch_result_observe(ChResultGate *g,const ChResultSnapshot *s,
                     const ChResultFrameReceipt *r,ChResultObservation *out) {
    ChResultProgress p;int status;
    if(out)memset(out,0,sizeof(*out));
    if(!g||!s||!r||!out||g->abi!=CH_RESULT_ABI||g->status!=(uint32_t)CH_RESULT_PENDING)
        return CH_RESULT_REJECTED;
    if(!path_valid(s))return reject(g,CH_RESULT_BAD_PATH);
    progress_from_snapshot(&p,s);status=ch_result_observe_progress(g,&p,r);
    return status==CH_RESULT_READY?ch_result_complete(g,s,out):status;
}
uint32_t ch_result_snapshot_bytes(void){return (uint32_t)sizeof(ChResultSnapshot);}
uint32_t ch_result_progress_bytes(void){return (uint32_t)sizeof(ChResultProgress);}
uint32_t ch_result_gate_bytes(void){return (uint32_t)sizeof(ChResultGate);}
uint32_t ch_result_receipt_bytes(void){return (uint32_t)sizeof(ChResultFrameReceipt);}
uint32_t ch_result_observation_bytes(void){return (uint32_t)sizeof(ChResultObservation);}
