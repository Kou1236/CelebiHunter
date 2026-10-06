#include "environment.h"
#include "environment_data.h"
#include <string.h>
static const uint32_t housekeeping[]={0x22f6e4};
enum { RTC_INSTRUCTION=0x1aa528,
    PREPARATION_RTC=0x8a4418c,PREPARATION_LATCHED_RTC=0x22f684 };
static const uint32_t RTC_ORIGINAL=0x0bfef301u,RTC_OWNED_NOP=0xe1a00000u;
/* Independent preparation policy: only this retained StartTime class is
   covered. Original GetClock/FixTime derives the guest clock after release.
   These ten data bytes are outside the unchanged base environment profile. */
static const uint8_t preparation_start_time[]={4,22,55,4};
static const uint8_t preparation_rtc[]={3,31,21,67,0};
static const uint8_t selected_cpu_bytes[]={
    0x60,0x04,0xd5,0xc0,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x70,0x04,0x80,0x08,0x00,0x00,0x00,0x00,
    0x20,0x01,0x43,0xc5,0x00,0xa0,0x48,0xd8,0x00,0x00};
static int byte(const RawSourceCapture *s,uint32_t a,uint8_t *out) {
    uint32_t i;
    if(!s||!out||s->region_count>RAW_SOURCE_REGION_COUNT||s->byte_count>RAW_SOURCE_MAX_BYTES)return 0;
    for(i=0;i<s->region_count;i++){const RawSourceRegion *r=&s->regions[i];
        if(r->offset>s->byte_count||r->size>s->byte_count-r->offset)return 0;
        if(a>=r->address&&a-r->address<r->size){*out=s->bytes[r->offset+a-r->address];return 1;}}
    return 0;
}
static uint32_t word_at(const RawSourceCapture *s,uint32_t a) {
    uint8_t b;uint32_t v=0,i;for(i=0;i<4;i++){if(!byte(s,a+i,&b))return UINT32_MAX;v|=(uint32_t)b<<(8*i);}return v;
}
static int desired(uint32_t address,uint32_t stage,uint8_t *value) {
    uint32_t i;
    /* DIV is observed from the original engine, never a data-store target. */
    if(address==raw_environment_layout[2]+4)return 0;
    if((stage==RAW_ENV_INITIAL||stage==RAW_ENV_PREPARATION)&&
       address>=RTC_INSTRUCTION&&address<RTC_INSTRUCTION+4) {
        *value=(uint8_t)(RTC_OWNED_NOP>>(8*(address-RTC_INSTRUCTION)));return 1;
    }
    if(stage==RAW_ENV_PREPARATION) {
        if(address>=PREPARATION_RTC&&address<PREPARATION_RTC+sizeof(preparation_rtc)) {
            *value=preparation_rtc[address-PREPARATION_RTC];return 1;
        }
        if(address>=PREPARATION_LATCHED_RTC&&address<PREPARATION_LATCHED_RTC+sizeof(preparation_rtc)) {
            *value=preparation_rtc[address-PREPARATION_LATCHED_RTC];return 1;
        }
    }
    for(i=0;i<sizeof(housekeeping)/sizeof(housekeeping[0]);i++)
        if(address==housekeeping[i]){*value=0;return 1;}
    for(i=0;i<sizeof(raw_environment_rows)/sizeof(raw_environment_rows[0]);i++) {
        const RawEnvironmentRow *r=&raw_environment_rows[i];
        if(address>=r->address&&address-r->address<r->size) {
            *value=raw_environment_values[r->offset+address-r->address];return 1;
        }
    }
    return 0;
}
int raw_environment_data_address(uint32_t a){uint8_t value;
    if((a>=PREPARATION_RTC&&a<PREPARATION_RTC+sizeof(preparation_rtc))||
       (a>=PREPARATION_LATCHED_RTC&&a<PREPARATION_LATCHED_RTC+sizeof(preparation_rtc)))return 1;
    return desired(a,RAW_ENV_TERMINAL,&value);
}
static int cpu_bytes(const RawSourceCapture *s,uint8_t b[34]) {
    static const uint32_t regions[][2]={{0x22f5fc,4},{0x22f604,20},{0x22f62c,8},{0x22f764,1}};
    uint32_t i,j,k=0;
    for(i=0;i<4;i++)for(j=0;j<regions[i][1];j++)
        if(!byte(s,regions[i][0]+j,&b[k++]))return 0;
    return byte(s,raw_environment_serial_flag,&b[k]);
}
static int cpu_identity(const RawSourceCapture *s,uint8_t digest[32]) {
    uint8_t b[34];if(!cpu_bytes(s,b))return 0;
    ch_platform_sha256(b,sizeof(b),digest);return 1;
}
static int preparation_cpu_match(const RawSourceCapture *s) {
    uint8_t b[34],digest[32];uint32_t i;
    if(!cpu_bytes(s,b))return 0;
    /* Bind these underlying comparison bytes to the unchanged full-CPU
       certificate, then exclude exactly BC/DE/HL (six bytes), not flags. */
    ch_platform_sha256(selected_cpu_bytes,sizeof(selected_cpu_bytes),digest);
    if(memcmp(digest,raw_environment_selected_cpu_sha256,32))return 0;
    for(i=0;i<sizeof(b);i++)if((i<26||i>=32)&&b[i]!=selected_cpu_bytes[i])return 0;
    return 1;
}
static int preparation_time_class(const RawSourceCapture *s,int normalized) {
    uint8_t v;uint32_t i;
    if(s->pointers[0]!=PREPARATION_RTC||s->pointers[1]!=PREPARATION_RTC+8||
       s->pointers[7]!=s->pointers[3]+4096)return 0;
    for(i=0;i<sizeof(preparation_start_time);i++)
        if(!byte(s,s->pointers[7]+0x4b6+i,&v)||v!=preparation_start_time[i])return 0;
    if(normalized)for(i=0;i<sizeof(preparation_rtc);i++)
        if(!byte(s,PREPARATION_RTC+i,&v)||v!=preparation_rtc[i]||
           !byte(s,PREPARATION_LATCHED_RTC+i,&v)||v!=preparation_rtc[i])return 0;
    return 1;
}
static int initial_rtc_state(const RawEnvironmentState *state,const RawSourceCapture *s) {
    uint32_t elapsed=s->counter-state->preparation_counter;
    if(state->initial_applied||state->terminal_applied)return 0;
    if(state->preparation_applied)return state->rtc_owned&&state->epoch==s->epoch&&
        elapsed&&elapsed<0x80000000u&&word_at(s,RTC_INSTRUCTION)==RTC_OWNED_NOP&&
        preparation_time_class(s,1);
    return !state->rtc_owned&&word_at(s,RTC_INSTRUCTION)==RTC_ORIGINAL;
}
static int preparation_state(const RawEnvironmentState *state,const RawSourceCapture *s) {
    return !state->initial_applied&&!state->terminal_applied&&!state->rtc_owned&&
        !state->preparation_applied&&word_at(s,RTC_INSTRUCTION)==RTC_ORIGINAL&&
        preparation_time_class(s,0);
}
static int rebase_state(const RawEnvironmentState *state,const RawSourceCapture *s) {
    uint32_t elapsed=s->counter-state->origin_counter;
    return state->initial_applied&&!state->terminal_applied&&state->preparation_applied&&
        state->rtc_owned&&!state->poisoned&&state->epoch==s->epoch&&
        elapsed&&elapsed<0x80000000u&&word_at(s,RTC_INSTRUCTION)==RTC_OWNED_NOP&&
        preparation_time_class(s,1);
}
int raw_environment_initial_cpu_check(const ChReadOps *o) {
    static const uint32_t regions[][2]={{0x22f5fc,4},{0x22f604,20},{0x22f62c,8},{0x22f764,1}};
    uint8_t bytes[34],digest[32];uint32_t i,offset=0;
    if(!o||!o->read_bytes)return RAW_ENV_CPU_UNREADABLE;
    for(i=0;i<4;i++) {
        if(!o->read_bytes(o->user,regions[i][0],bytes+offset,regions[i][1]))return RAW_ENV_CPU_UNREADABLE;
        offset+=regions[i][1];
    }
    if(!o->read_bytes(o->user,raw_environment_serial_flag,bytes+offset,1))return RAW_ENV_CPU_UNREADABLE;
    ch_platform_sha256(bytes,sizeof(bytes),digest);
    return memcmp(digest,raw_environment_selected_cpu_sha256,32)?RAW_ENV_CPU_MISMATCH:RAW_ENV_CPU_MATCH;
}
uint32_t raw_environment_initial_source_rejection(const RawEnvironmentPlan *p,const RawEnvironmentState *s) {
    uint8_t digest[32];const RawSourceCapture *before;
    if(!p||!s||(p->stage!=RAW_ENV_INITIAL&&p->stage!=RAW_ENV_PREPARATION))return RAW_ENV_SOURCE_PREFLIGHT;
    before=&p->before;
    if(!before->structural_match)return RAW_ENV_SOURCE_CAPTURE;
    if(before->pointers[3]!=raw_environment_layout[0]||before->pointers[5]!=raw_environment_layout[1]||
       before->pointers[6]!=raw_environment_layout[2])return RAW_ENV_SOURCE_LAYOUT;
    if(s->poisoned||s->initial_applied||s->terminal_applied)return RAW_ENV_SOURCE_STATE;
    if(p->stage==RAW_ENV_PREPARATION) {
        if(s->rtc_owned||s->preparation_applied)return RAW_ENV_SOURCE_STATE;
        if(word_at(before,RTC_INSTRUCTION)!=RTC_ORIGINAL||!preparation_time_class(before,0))return RAW_ENV_SOURCE_RTC;
        if(!preparation_cpu_match(before))return RAW_ENV_SOURCE_CPU;
    } else {
        if((!s->preparation_applied&&s->rtc_owned)||(s->preparation_applied&&
           (!s->rtc_owned||s->epoch!=before->epoch||!((before->counter-s->preparation_counter)&&
            before->counter-s->preparation_counter<0x80000000u))))return RAW_ENV_SOURCE_STATE;
        if(!initial_rtc_state(s,before))return RAW_ENV_SOURCE_RTC;
        if(!cpu_identity(before,digest)||memcmp(digest,raw_environment_selected_cpu_sha256,32))return RAW_ENV_SOURCE_CPU;
    }
    return RAW_ENV_SOURCE_PREFLIGHT;
}
static int add_write(RawEnvironmentPlan *p,uint32_t a,uint8_t v) {
    uint8_t old;uint32_t i;
    for(i=0;i<p->count;i++)if(p->writes[i].address==a)return p->writes[i].value==v;
    if(!byte(&p->before,a,&old))return 0;
    if(old==v)return 1;
    if(p->count==RAW_ENV_MAX_WRITES)return 0;
    p->writes[p->count++]=(RawEnvironmentWrite){a,old,v,{0,0}};return 1;
}
static int valid_ops(const RawEnvironmentOps *o){return o&&o->read.read_bytes&&
    o->write_data_byte&&o->write_rtc_word&&o->publish_rtc;}
static int receipt_log(const MpInputPlan *p,uint32_t counter){
    const MpObservation *raw,*raw_mask,*tail,*scan,*mask;uint32_t n=p->log_count;
    if(n<5||n>MP_LOG_CAPACITY)return 0;
    raw=&p->log[n-5];raw_mask=&p->log[n-4];tail=&p->log[n-3];scan=&p->log[n-2];mask=&p->log[n-1];
    return raw->kind==MP_LOG_SCAN&&raw->counter==counter-1&&!(raw->previous_held&1)&&
        (raw->current_held&1)&&!(raw->intersection&1)&&raw->intersection==(raw->previous_held&raw->current_held)&&
        raw_mask->kind==MP_LOG_PROVIDER&&raw_mask->counter==counter-1&&raw_mask->provider_mask==0xffff&&
        raw_mask->observation_point==MP_POINT_APPLIED_MASK_MARKER_1A82DC&&
        tail->kind==MP_LOG_COUNTER&&tail->counter==counter-1&&tail->next_counter==counter&&
        scan->kind==MP_LOG_SCAN&&scan->counter==counter&&scan->previous_held==raw->current_held&&
        scan->current_held==p->last_held&&scan->intersection==p->last_intersection&&
        scan->intersection==(scan->previous_held&scan->current_held)&&
        mask->kind==MP_LOG_PROVIDER&&mask->counter==counter&&mask->provider_mask==0xfffe&&
        mask->observation_point==MP_POINT_APPLIED_MASK_MARKER_1A82DC;
}
int raw_environment_prepare(const RawEnvironmentOps *o,const ChNativeContext *c,uint32_t tls,
    uint64_t epoch,uint32_t first,uint32_t stage,uint32_t x,const RawEnvironmentState *state,
    const ManualPrediction *prediction,const MpInputPlan *input,RawEnvironmentPlan *p) {
    RawSourceCapture *s;uint32_t i,j;uint8_t v,digest[32],provider[sizeof(raw_environment_provider)];
    if(!valid_ops(o)||!p||!state||state->poisoned||x>255||
       !(stage==RAW_ENV_INITIAL||stage==RAW_ENV_TERMINAL||stage==RAW_ENV_PREPARATION||stage==RAW_ENV_REBASE))return RAW_ENV_REJECTED;
    memset(p,0,sizeof(*p));p->stage=stage;p->div_x=x;s=&p->before;
    ch_platform_sha256((const uint8_t *)state,sizeof(*state),p->state_identity);
    if(!raw_capture_applied_source(&o->read,c,tls,epoch,first,stage==RAW_ENV_TERMINAL?0xfffe:0xffff,s))return RAW_ENV_REJECTED;
    if(s->pointers[3]!=raw_environment_layout[0]||s->pointers[5]!=raw_environment_layout[1]||
        s->pointers[6]!=raw_environment_layout[2]||
        memcmp(raw_environment_profile_sha256,manual_model_certificate()->normalization_profile.bytes,32))return RAW_ENV_REJECTED;
    if(!o->read.read_bytes(o->read.user,0x195df4,provider,sizeof(provider))||
        memcmp(provider,raw_environment_provider,sizeof(provider))||
        word_at(s,0x22f788)!=s->pointers[6]||word_at(s,0x22f7a4)!=s->pointers[6]+15)return RAW_ENV_REJECTED;
    if(!byte(s,s->pointers[6],&v)||v!=255||!byte(s,s->pointers[6]+15,&v)||v!=1)return RAW_ENV_REJECTED;
    if(!byte(s,s->pointers[5]+0x55,&v)||v>2)return RAW_ENV_REJECTED;
    if(stage==RAW_ENV_PREPARATION) {
        if(x!=183||!preparation_state(state,s)||!preparation_cpu_match(s))return RAW_ENV_REJECTED;
    } else if(stage==RAW_ENV_INITIAL) {
        if(x!=183||!initial_rtc_state(state,s)||!cpu_identity(s,digest)||
           memcmp(digest,raw_environment_selected_cpu_sha256,32))return RAW_ENV_REJECTED;
    } else if(stage==RAW_ENV_REBASE) {
        if(x!=183||!rebase_state(state,s)||!cpu_identity(s,digest)||
           memcmp(digest,raw_environment_selected_cpu_sha256,32))return RAW_ENV_REJECTED;
    } else {
        if(!cpu_identity(s,digest)||!byte(s,s->pointers[6]+4,&v)||v!=x)return RAW_ENV_REJECTED;
        if(!state->initial_applied||state->terminal_applied||!state->rtc_owned||state->epoch!=epoch||
            (state->preparation_applied&&!preparation_time_class(s,1))||
            word_at(s,0x1aa528)!=0xe1a00000||memcmp(digest,state->nonbudget_CPU_identity,32)||
            !prediction||prediction->abi!=MANUAL_PREDICTION_ABI||prediction->status!=MANUAL_QUERY_OK||
            prediction->source_epoch!=epoch||prediction->origin_counter!=state->origin_counter||
            memcmp(prediction->source_identity.bytes,state->source_identity,32)||
            memcmp(prediction->required_context_identity.bytes,manual_model_certificate()->clock_derivation.bytes,32)||
            prediction->target_counter!=s->counter||prediction->div_x!=x||prediction->manual_input_validated||
            prediction->input_contract!=manual_model_certificate()->input_contract||
            prediction->expected_wait_add!=s->rng_add||prediction->expected_wait_sub!=s->rng_sub||
            prediction->target_counter!=prediction->origin_counter+prediction->press_relative||
            prediction->model_n!=prediction->press_relative+2||prediction->model_n>MANUAL_MODEL_MAX_N||
            prediction->expected_release_counter!=s->counter+3||
            !input||input->abi!=MP_ABI||input->error||input->state!=MP_CONDITIONAL_HOLD||
            !receipt_log(input,s->counter)||
            !input->raw_press_seen||!input->effective_a_seen||!input->scan_recorded||!input->mask_recorded||
            input->manual_hardware_verified||input->source_epoch!=epoch||input->current_counter!=s->counter||
            input->observed_raw_press_counter+1!=s->counter||input->observed_effective_a_counter!=s->counter||
            input->last_provider_mask!=0xfffe||input->original_host_batch_count!=1||
            input->original_engine_type!=1||input->original_host_phase!=0||
            input->effective_a_counter!=prediction->target_counter||
            input->player_raw_press_counter!=prediction->target_counter-1||
            input->expected_release_counter!=prediction->expected_release_counter||
            input->player_raw_release_counter!=prediction->expected_release_counter||
            input->scan_seen_at_counter!=s->counter||input->provider_seen_at_counter!=s->counter||
            input->last_held!=s->boundary.host_cache[0]||input->last_intersection!=s->boundary.host_cache[3]||
            memcmp(input->required_context_identity.bytes,prediction->required_context_identity.bytes,32)||
            memcmp(input->model_certificate_identity.bytes,manual_model_certificate()->profile_artifact.bytes,32)||
            memcmp(input->source_identity.bytes,state->source_identity,32))return RAW_ENV_REJECTED;
        if(state->source_bg>2||!byte(s,s->pointers[5]+0x55,&v)||
           v!=(state->source_bg+prediction->press_relative)%3u)return RAW_ENV_REJECTED;
    }
    for(i=0;i<sizeof(raw_environment_rows)/sizeof(raw_environment_rows[0]);i++) {
        const RawEnvironmentRow *r=&raw_environment_rows[i];
        for(j=0;j<r->size;j++) {
            if(!desired(r->address+j,stage,&v)||!add_write(p,r->address+j,v))return RAW_ENV_REJECTED;
        }
    }
    for(i=0;i<sizeof(housekeeping)/sizeof(housekeeping[0]);i++)
        if(!add_write(p,housekeeping[i],0))return RAW_ENV_REJECTED;
    if(stage==RAW_ENV_PREPARATION)for(i=0;i<sizeof(preparation_rtc);i++) {
        if(!desired(PREPARATION_RTC+i,stage,&v)||!add_write(p,PREPARATION_RTC+i,v)||
           !desired(PREPARATION_LATCHED_RTC+i,stage,&v)||!add_write(p,PREPARATION_LATCHED_RTC+i,v))return RAW_ENV_REJECTED;
    }
    if(stage==RAW_ENV_INITIAL||stage==RAW_ENV_PREPARATION)for(i=0;i<4;i++) {
        if(!desired(0x1aa528+i,stage,&v)||!add_write(p,0x1aa528+i,v))return RAW_ENV_REJECTED;
    }
    /* Only clock rows and the guarded RTC policy are writable. Audio channels,
       APU/queue and VBlank/BGThird stay byte-exact to this live before image. */
    for(i=1;i<p->count;i++){RawEnvironmentWrite w=p->writes[i];j=i;
        while(j&&p->writes[j-1].address>w.address){p->writes[j]=p->writes[j-1];j--;}p->writes[j]=w;}
    ch_platform_sha256((const uint8_t *)p,(size_t)((uint8_t *)p->identity-(uint8_t *)p),p->identity);
    return RAW_ENV_OK;
}
static int compare_current(const RawEnvironmentOps *o,const RawEnvironmentPlan *p,int after) {
    uint32_t i,j,k,n,q;uint8_t b[256],want;ChBoundarySample boundary;
    for(i=0;i<p->before.region_count;i++) {
        const RawSourceRegion *r=&p->before.regions[i];
        q=0;
        if(after){uint32_t lo=0,hi=p->count;
            while(lo<hi){uint32_t mid=lo+(hi-lo)/2;
                if(p->writes[mid].address<r->address)lo=mid+1;else hi=mid;}
            q=lo;
        }
        for(j=0;j<r->size;j+=sizeof(b)) {
            n=r->size-j;if(n>sizeof(b))n=sizeof(b);
            if(!o->read.read_bytes(o->read.user,r->address+j,b,n))return 0;
            for(k=0;k<n;k++) {
                want=p->before.bytes[r->offset+j+k];
                if(after) {
                    uint32_t address=r->address+j+k;
                    /* Both arrays are sorted within this region. Consume
                       the same exact projected writes in one pass rather
                       than a binary search for every captured byte. */
                    while(q<p->count&&p->writes[q].address<address)q++;
                    if(q<p->count&&p->writes[q].address==address){want=p->writes[q].value;q++;}
                }
                if(b[k]!=want)return 0;
            }
        }
    }
    for(i=0;i<8;i++){uint8_t data[4];static const uint32_t addresses[]={0x22f640,0x22f644,0x22f698,0x22f6c8,0x22f6d4,0x22f6d8,0x22f6dc,0x22f768};
        if(!o->read.read_bytes(o->read.user,addresses[i],data,4)||
            memcmp(data,&p->before.pointers[i],4))return 0;}
    if(ch_sample_boundary(&o->read,&boundary)!=CH_SAMPLE_OK||
        memcmp(&boundary,&p->before.boundary,sizeof(boundary)))return 0;
    {uint8_t provider[sizeof(raw_environment_provider)];
     if(!o->read.read_bytes(o->read.user,0x195df4,provider,sizeof(provider))||
        memcmp(provider,raw_environment_provider,sizeof(provider)))return 0;}
    return 1;
}
static int rollback(const RawEnvironmentOps *o,RawEnvironmentState *state,const RawEnvironmentPlan *p,uint32_t attempted,int rtc) {
    uint32_t i;uint8_t current;int clean=1;
    for(i=attempted;i>0;i--){const RawEnvironmentWrite *w=&p->writes[i-1];
        if(w->address>=0x1aa528&&w->address<0x1aa52c)continue;
        if(!o->read.read_bytes(o->read.user,w->address,&current,1))clean=0;
        else if(current==w->value){if(!o->write_data_byte(o->read.user,w->address,w->before))clean=0;}
        else if(current!=w->before)clean=0;
    }
    if(rtc){uint8_t b[4];
        if(!o->read.read_bytes(o->read.user,0x1aa528,b,4))clean=0;
        else if(!memcmp(b,"\x00\x00\xa0\xe1",4)||!memcmp(b,"\x01\xf3\xfe\x0b",4)) {
            if(!o->write_rtc_word(o->read.user,0x0bfef301)||!o->publish_rtc(o->read.user)||
               !o->read.read_bytes(o->read.user,0x1aa528,b,4)||memcmp(b,"\x01\xf3\xfe\x0b",4))clean=0;
            else state->rtc_owned=0;
        } else clean=0;
    }
    if(!compare_current(o,p,0))clean=0;
    if(!clean){state->poisoned=1;return RAW_ENV_POISONED;}
    return RAW_ENV_STORE_FAILED;
}
/* Complete structural and typed preflight, before the first store. A checksum
   is an integrity receipt; it is never a substitute for these domain guards. */
static int preflight(const RawEnvironmentState *s,const RawEnvironmentPlan *p) {
    uint8_t digest[32],old,v;uint32_t i,j,changed=0;
    if(p->before.abi!=1||p->before.region_count!=RAW_SOURCE_REGION_COUNT||p->before.byte_count>RAW_SOURCE_MAX_BYTES||
       !p->before.structural_match||!p->before.epoch||p->before.caller.lr!=0x1a8340||p->div_x>255)return 0;
    ch_platform_sha256((const uint8_t *)s,sizeof(*s),digest);
    if(memcmp(digest,p->state_identity,32))return 0;
    if(p->before.pointers[3]!=raw_environment_layout[0]||p->before.pointers[5]!=raw_environment_layout[1]||
       p->before.pointers[6]!=raw_environment_layout[2])return 0;
    if(!byte(&p->before,p->before.pointers[5]+0x55,&v)||v>2)return 0;
    if(p->stage==RAW_ENV_PREPARATION) {
        if(p->div_x!=183||!preparation_state(s,&p->before)||!preparation_cpu_match(&p->before))return 0;
    } else if(p->stage==RAW_ENV_INITIAL) {
        if(p->div_x!=183||!initial_rtc_state(s,&p->before)||!cpu_identity(&p->before,digest)||
           memcmp(digest,raw_environment_selected_cpu_sha256,32))return 0;
    } else if(p->stage==RAW_ENV_REBASE) {
        if(p->div_x!=183||!rebase_state(s,&p->before)||!cpu_identity(&p->before,digest)||
           memcmp(digest,raw_environment_selected_cpu_sha256,32))return 0;
    } else if(p->stage==RAW_ENV_TERMINAL) {
        if(!byte(&p->before,p->before.pointers[6]+4,&v)||v!=p->div_x||
           !s->initial_applied||s->terminal_applied||!s->rtc_owned||s->epoch!=p->before.epoch||
           word_at(&p->before,RTC_INSTRUCTION)!=RTC_OWNED_NOP||
           (s->preparation_applied&&!preparation_time_class(&p->before,1))||
           !cpu_identity(&p->before,digest)||memcmp(digest,s->nonbudget_CPU_identity,32))return 0;
        if(s->source_bg>2||p->before.counter-s->origin_counter>MANUAL_MODEL_MAX_PRESS||
           !byte(&p->before,p->before.pointers[5]+0x55,&v)||
           v!=(s->source_bg+(p->before.counter-s->origin_counter))%3u)return 0;
    } else return 0;
    for(i=0;i<RAW_SOURCE_REGION_COUNT;i++){const RawSourceRegion *r=&p->before.regions[i];
        if(r->offset>p->before.byte_count||r->size>p->before.byte_count-r->offset||
           (uint64_t)r->address+r->size>UINT64_C(0x100000000))return 0;}
    for(i=0;i<p->count;i++){const RawEnvironmentWrite *w=&p->writes[i];
        if((i&&p->writes[i-1].address>=w->address)||!desired(w->address,p->stage,&v)||v!=w->value||
           !byte(&p->before,w->address,&old)||old!=w->before||old==v)return 0;}
    /* Count each allowed address once; aliases/housekeeping overrides cannot
       omit a required byte from a recomputed or partially initialized plan. */
    for(i=0;i<sizeof(raw_environment_rows)/sizeof(raw_environment_rows[0]);i++) {
        const RawEnvironmentRow *r=&raw_environment_rows[i];
        for(j=0;j<r->size;j++) {
            if(!desired(r->address+j,p->stage,&v)||!byte(&p->before,r->address+j,&old))return 0;
            if(v!=old)changed++;
        }
    }
    for(i=0;i<sizeof(housekeeping)/sizeof(housekeeping[0]);i++) {
        int covered=0;
        for(j=0;j<sizeof(raw_environment_rows)/sizeof(raw_environment_rows[0]);j++)
            if(housekeeping[i]>=raw_environment_rows[j].address&&housekeeping[i]-raw_environment_rows[j].address<raw_environment_rows[j].size)covered=1;
        if(!covered){if(!byte(&p->before,housekeeping[i],&old))return 0;if(old)changed++;}
    }
    if(p->stage==RAW_ENV_PREPARATION)for(i=0;i<sizeof(preparation_rtc);i++) {
        if(!byte(&p->before,PREPARATION_RTC+i,&old)||!desired(PREPARATION_RTC+i,p->stage,&v))return 0;
        if(old!=v)changed++;
        if(!byte(&p->before,PREPARATION_LATCHED_RTC+i,&old)||
           !desired(PREPARATION_LATCHED_RTC+i,p->stage,&v))return 0;
        if(old!=v)changed++;
    }
    if(p->stage==RAW_ENV_INITIAL||p->stage==RAW_ENV_PREPARATION)for(i=0;i<4;i++) {
        if(!byte(&p->before,0x1aa528+i,&old)||!desired(0x1aa528+i,p->stage,&v))return 0;
        if(old!=v)changed++;
    }
    return changed==p->count;
}
int raw_environment_apply(const RawEnvironmentOps *o,RawEnvironmentState *state,const RawEnvironmentPlan *p) {
    uint8_t digest[32],values[256];uint32_t i,n,k,attempted=0;int rtc=0;
    if(!valid_ops(o)||!state||!p||state->poisoned||p->count>RAW_ENV_MAX_WRITES)return RAW_ENV_REJECTED;
    ch_platform_sha256((const uint8_t *)p,(size_t)((const uint8_t *)p->identity-(const uint8_t *)p),digest);
    if(memcmp(digest,p->identity,32)||
       !(p->stage==RAW_ENV_INITIAL||p->stage==RAW_ENV_TERMINAL||p->stage==RAW_ENV_PREPARATION||p->stage==RAW_ENV_REBASE)||
       !preflight(state,p)||!compare_current(o,p,0))return RAW_ENV_REJECTED;
    for(i=0;i<p->count;) {
        const RawEnvironmentWrite *w=&p->writes[i];
        if(w->address>=0x1aa528&&w->address<0x1aa52c){i++;continue;}
        /* Group only adjacent changed addresses. Neither unchanged gaps nor
           protected state enter the callback; all original typed preflight
           and complete before/after readback guards remain above/below. */
        n=1;
        if(o->write_data_span)while(n<sizeof(values)&&i+n<p->count&&
            p->writes[i+n].address==w->address+n&&
            !(p->writes[i+n].address>=0x1aa528&&p->writes[i+n].address<0x1aa52c))n++;
        attempted=i+n;
        if(o->write_data_span){
            for(k=0;k<n;k++)values[k]=p->writes[i+k].value;
            if(!o->write_data_span(o->read.user,w->address,values,n))return rollback(o,state,p,attempted,rtc);
        } else if(!o->write_data_byte(o->read.user,w->address,w->value))return rollback(o,state,p,attempted,rtc);
        i+=n;
    }
    /* A prepared INITIAL already owns the NOP. Do not reinstall it or revoke
       preparation ownership on a later data rollback. A fresh installation
       begins only from the exact original instruction validated above. */
    if((p->stage==RAW_ENV_INITIAL||p->stage==RAW_ENV_PREPARATION)&&
       word_at(&p->before,RTC_INSTRUCTION)==RTC_ORIGINAL) {
        rtc=1;state->rtc_owned=1;
        if(!o->write_rtc_word(o->read.user,RTC_OWNED_NOP)||!o->publish_rtc(o->read.user))return rollback(o,state,p,p->count,rtc);
    }
    if(!compare_current(o,p,1))return rollback(o,state,p,p->count,rtc);
    if(p->stage==RAW_ENV_PREPARATION) {
        state->preparation_applied=1;state->preparation_counter=p->before.counter;
        state->rtc_owned=1;state->epoch=p->before.epoch;
    } else if(p->stage==RAW_ENV_INITIAL||p->stage==RAW_ENV_REBASE){state->initial_applied=1;state->rtc_owned=1;state->epoch=p->before.epoch;
        state->origin_counter=p->before.counter;memcpy(state->source_identity,p->before.identity,32);
        {uint8_t bg;byte(&p->before,p->before.pointers[5]+0x55,&bg);state->source_bg=bg;}
        cpu_identity(&p->before,state->nonbudget_CPU_identity);}
    else state->terminal_applied=1;
    return RAW_ENV_OK;
}
int raw_environment_restore_rtc(const RawEnvironmentOps *o,RawEnvironmentState *s) {
    uint8_t b[4];
    if(!valid_ops(o)||!s||!s->rtc_owned)return RAW_ENV_REJECTED;
    if(!o->read.read_bytes(o->read.user,0x1aa528,b,4)||
        (memcmp(b,"\x00\x00\xa0\xe1",4)&&memcmp(b,"\x01\xf3\xfe\x0b",4))||
        !o->write_rtc_word(o->read.user,0x0bfef301)||!o->publish_rtc(o->read.user)||
        !o->read.read_bytes(o->read.user,0x1aa528,b,4)||memcmp(b,"\x01\xf3\xfe\x0b",4)) {
        s->poisoned=1;return RAW_ENV_POISONED;
    }
    s->rtc_owned=0;return RAW_ENV_OK;
}
