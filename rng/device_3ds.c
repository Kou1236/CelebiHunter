#include <3ds.h>
#include <string.h>
#include "device_3ds.h"
#include "job_mailbox.h"
#include "platform/backend_3ds.h"
#include "environment_backend_3ds.h"
#include "hud/sink_3ds.h"
#include "hud/present_context.h"
#include "hud/paused_3ds.h"
#include "result/result_gate.h"
#include "audio/paused_audio_3ds.h"
#include "source-observer/aligned_waiting.h"
extern void ch_manual_alias_probe(void);
#define RAW_SOURCE_READONLY_RETRIES 4u
#define RAW_SOURCE_RETRY_ADVANCES 16u
#define RAW_WAIT_UNAVAILABLE_ADVANCES 4u
typedef struct {
    uint32_t failures,first_failure_counter,stage,counter,previous_counter,target;
    uint32_t applied_mask,previous_vblank,actual_vblank,first_valid;
    CQWaitingState previous,expected,actual;
    SourceAlignedDiagnostic aligned;
} RawWaitingDiagnostic;
typedef struct {
    Ch3dsBackend backend;
    RawPlatformBinding binding;
    ChInstall install;
    RawDeviceHostOps host;
    RawMailbox mailbox;
    LightLock job_lock,hud_lock,source_lock;
    Thread worker;
    RawHud hud;
    RawSourceCapture capture;
    RawEnvironmentBackend3ds environment_backend;
    RawEnvironmentSession environment;
    RawHud3dsSink screen;
    RawPresentBinding presentation;
    RawPause3ds pause_backend;
    RawPausedDisplay pause_display;
    ChResultSource result_source;
    ChResultGate result_gate;
    ChResultSnapshot result_snapshot;
    ChResultObservation actual_result;
    uint32_t stop,source_requested,source_available;
    uint32_t startup_phase;
    int32_t install_status;
    /* Append diagnostics so prior Device offsets remain usable for read-only
       investigation of an already running build. */
    RawDeviceSourceDiagnostic source_diagnostic;
    /* Renewal preflight stays separate from the still valid result receipt. */
    ChResultSource refresh_result_source;
    ChResultGate refresh_result_gate;
    uint32_t refresh_attempted,refresh_attempt_counter;
    ChResultProgress result_progress;
    RawAudio3ds audio_backend;
    RawPausedAudio pause_audio;
    int32_t pause_audio_status;
    SourceObservation waiting_observation;
    uint32_t waiting_observation_valid;
    SourceAlignedDiagnostic waiting_read_diagnostic;
    RawWaitingDiagnostic waiting_diagnostic;
} Device;
static Device device;
static RawQueryCache worker_query_cache;
typedef struct {const ChReadOps *source;uint32_t calls,failed,ordinal,address,size;} RawResultReadTrace;
static int traced_result_read(void *user,uint32_t address,void *out,uint32_t size){
    RawResultReadTrace *t=user;t->calls++;
    if(!t->source->read_bytes(t->source->user,address,out,size)){
        if(!t->failed){t->failed=1u;t->ordinal=t->calls;t->address=address;t->size=size;}
        return 0;
    }
    return 1;
}
static int result_snapshot_traced(Device *d,ChResultSnapshot *out){
    RawResultReadTrace t;ChReadOps read=d->binding.read;int good;
    memset(&t,0,sizeof(t));t.source=&d->binding.read;read.user=&t;read.read_bytes=traced_result_read;
    good=ch_result_read_snapshot(&read,out);
    d->source_diagnostic.result_read_kind=good?RAW_RESULT_READ_OK:
        t.failed?RAW_RESULT_READ_FAILED:t.calls<=5u?RAW_RESULT_READ_LAYOUT_REJECTED:RAW_RESULT_READS_DISAGREED;
    d->source_diagnostic.result_read_calls=t.calls;
    d->source_diagnostic.result_read_ordinal=t.ordinal?t.ordinal:t.calls;
    d->source_diagnostic.result_read_address=t.address;d->source_diagnostic.result_read_size=t.size;
    return good;
}
static char *diag_copy(char *p,const char *s){while(*s)*p++=*s++;*p=0;return p;}
static char *diag_number(char *p,uint32_t v){char b[10];uint32_t n=0;
    do{b[n++]=(char)('0'+v%10u);}while(v/=10u);while(n)*p++=b[--n];*p=0;return p;}
static char *diag_hex(char *p,uint32_t v){const char *h="0123456789ABCDEF";uint32_t i;
    for(i=0;i<8;i++)*p++=h[(v>>(28u-4u*i))&15u];
    *p=0;return p;}
static char *diag_hex16(char *p,uint32_t v){const char *h="0123456789ABCDEF";uint32_t i;
    for(i=0;i<4;i++)*p++=h[(v>>(12u-4u*i))&15u];
    *p=0;return p;}
static char *diag_copy_bound(char *p,char *end,const char *s){
    while(*s&&p<end)*p++=*s++;
    *p=0;return p;
}
static void diag_rows(RawHud *h,uint32_t color,const char *text){
    uint32_t n,k,row;
    while(*text&&h->count<12u){
        for(n=0;n<50u&&text[n];n++){}
        if(n==50u&&text[n]){
            for(k=n;k>0u&&text[k]!=' ';k--){}
            if(k)n=k;
        }
        row=h->count++;h->color[row]=color;
        memcpy(h->line[row],text,n);h->line[row][n]=0;
        text+=n;while(*text==' ')text++;
    }
}
static void add_diag(RawHud *h,uint32_t color,const char *code,const char *message){
    char text[160],*p=text,*end=text+sizeof(text)-1u;if(!h||h->count>=12u)return;
    p=diag_copy_bound(p,end,"Check ");p=diag_copy_bound(p,end,code);
    p=diag_copy_bound(p,end,": ");(void)diag_copy_bound(p,end,message);
    diag_rows(h,color,text);
}
static void add_diag_detail(RawHud *h,uint32_t color,const char *code,const char *message,const char *detail){
    if(!h||h->count>=12u)return;
    add_diag(h,color,code,message);
    /* Detail values need their own 50-column row to remain visible. */
    if(!detail||!*detail||h->count>=12u)return;
    diag_rows(h,color,detail);
}
static const char *scene_rejection_text(uint32_t reason){
    switch(reason){
    case CH_SCENE_POINTER_READ:return "ROM/WRAM base pointer unreadable";
    case CH_SCENE_POINTER_INVALID:return "ROM/WRAM base pointer has invalid value";
    case CH_SCENE_POINTER_LAYOUT:return "ROM/WRAM pointer layout unsupported";
    case CH_SCENE_ROM_READ:return "Crystal instruction bytes unreadable";
    case CH_SCENE_ROM_SIGNATURE:return "Crystal instruction signature differs";
    case CH_SCENE_BANK_READ:return "game bank register unreadable";
    case CH_SCENE_BANK_UNSUPPORTED:return "game bank state unsupported";
    case CH_SCENE_STATE_READ:return "script/map/joypad memory unreadable";
    case CH_SCENE_SCRIPT_STATE:return "not at the supported Shrine script step";
    case CH_SCENE_STACK_READ:return "game stack unreadable";
    case CH_SCENE_STACK_PATH:return "expected prompt call path not found";
    case CH_SCENE_INPUT_HELD:return "release game buttons";
    default:return "supported final GS Ball prompt not recognized";
    }
}
static const char *env_rejection_text(uint32_t reason){
    switch(reason){
    case RAW_ENV_SOURCE_CAPTURE:return "stable source memory capture failed";
    case RAW_ENV_SOURCE_LAYOUT:return "live memory layout unsupported";
    case RAW_ENV_SOURCE_STATE:return "environment state did not match";
    case RAW_ENV_SOURCE_RTC:return "RTC instruction/state mismatch";
    case RAW_ENV_SOURCE_CPU:return "CPU context outside model";
    case RAW_ENV_SOURCE_SAVE_TIME:return "Save time cannot match model clock";
    default:return "environment preflight rejected the source";
    }
}
static const char *result_reason_text(uint32_t reason){
    switch(reason){
    case CH_RESULT_BAD_SOURCE:return "result source changed";
    case CH_RESULT_BAD_PATH:return "encounter instruction path mismatch";
    case CH_RESULT_BAD_IDENTITY:return "result identity mismatch";
    case CH_RESULT_BAD_UNIT:return "unexpected scheduler unit";
    case CH_RESULT_BAD_SCENE:return "scene changed before result read";
    case CH_RESULT_BAD_INPUT:return "input receipt mismatch";
    case CH_RESULT_BAD_MAPPING:return "result memory mapping unsupported";
    default:return "result snapshot not ready";
    }
}
static uint32_t unit_reason(const RawDeviceSourceDiagnostic *s){
    if(s->sample_status==(uint32_t)CH_SAMPLE_READ_FAILED)return 1u;
    if(s->sample_status==(uint32_t)CH_SAMPLE_INCOHERENT)return 2u;
    if(!s->counter_valid)return 4u;
    if(s->batch_count!=1u)return 5u;
    if(s->host_phase!=0u)return 6u;
    if(s->engine_type!=1u)return 7u;
    if(s->input_enable!=0u)return 8u;
    if(s->sample_status==(uint32_t)CH_SAMPLE_UNSUPPORTED)return 3u;
    return s->ordinary_supported?9u:10u;
}
static void add_source_diag(Device *d,const RawRuntime *r,RawHud *h){
    const RawDeviceSourceDiagnostic *s=&d->source_diagnostic;const RawWaitingDiagnostic *w=&d->waiting_diagnostic;
    const char *message=0;char code[32]={0};char *p=code;uint32_t color=0xffdd70u;
    if(!r||!h||h->count>=12u)return;
    if(r->source_bound&&!r->controls.candidate_valid&&!r->waiting_valid&&w->failures){
        if(s->waiting_unavailable_failures<RAW_WAIT_UNAVAILABLE_ADVANCES&&
           r->counter-s->waiting_unavailable_since<RAW_WAIT_UNAVAILABLE_ADVANCES){
            add_diag(h,color,"W00","rechecking live state; Target unavailable");return;
        }
        color=0xff9090u;
        if(w->stage==RAW_WAIT_DIAG_ALIGNMENT){
            if(w->aligned.stage==SOURCE_ALIGNED_READ){
                p=diag_copy(p,"M");p=diag_number(p,w->aligned.read_ordinal);
                p=diag_copy(p,"/P");p=diag_number(p,w->aligned.read_pass);
                *p++='@';p=diag_hex(p,w->aligned.read_address);*p=0;
                add_diag(h,color,code,"state read failed at recorded address");return;
            }
            if(w->aligned.stage==SOURCE_ALIGNED_DRIFT){
                add_diag(h,color,"W01/D","state changed during the double read");return;
            }
            else if(w->aligned.stage==SOURCE_ALIGNED_DOMAIN){
                p=diag_copy(p,"T");if(w->aligned.domain_guard<10u)*p++='0';p=diag_number(p,w->aligned.domain_guard);*p=0;
                switch(w->aligned.domain_guard){
                case 1:message="emulator pointer pair mismatch";break;
                case 2:message="game CPU position/register state mismatch";break;
                case 3:message="TIMA modulo mismatch";break;
                case 4:message="TAC timer mode mismatch";break;
                case 5:message="timer interrupt state mismatch";break;
                case 6:message="HBlank flags outside model";break;
                case 7:message="LCD countdown outside model";break;
                case 8:message="LCD timing periods outside model";break;
                case 9:message="LCD mode outside model";break;
                case 10:message="LY/STAT register mismatch";break;
                case 11:message="DIV enable state mismatch";break;
                case 12:message="background phase outside model";break;
                case 13:message="TIMA phase outside model";break;
                case 14:message="TIMA countdown outside model";break;
                case 15:message="timer deadline mismatch";break;
                case 16:message="scheduler budget mismatch";break;
                default:message="unrecognized timing guard";break;
                }
                add_diag(h,color,code,message);return;
            }else message="CPU/scheduler boundary outside the model";
            add_diag(h,color,"W01/B",message);return;
        }
        switch(w->stage){
        case RAW_WAIT_DIAG_BINDING:message="live RNG/background no longer matches source";break;
        case RAW_WAIT_DIAG_COUNTER:message="scheduler advanced before state validation";break;
        case RAW_WAIT_DIAG_PROJECTION:message="timer projection could not advance";break;
        case RAW_WAIT_DIAG_STATE:message="live DIV/RNG differs from projected state";break;
        case RAW_WAIT_DIAG_INFER:message="observed RNG instruction pair did not match";break;
        case RAW_WAIT_DIAG_OPERANDS:message="observed DIV operands did not match timer model";break;
        case RAW_WAIT_DIAG_RESEED:message="could not capture a fresh source state";break;
        default:message="waiting state check failed";break;
        }
        p=diag_copy(p,"W");if(w->stage<10u)*p++='0';p=diag_number(p,w->stage);*p=0;
        add_diag(h,color,code,message);return;
    }
    if(s->closed&&s->phase!=RAW_SOURCE_ADMITTED){color=0xff9090u;
        switch(s->phase){
        case RAW_SOURCE_STOP_OWNERSHIP:message="plugin lost native-thread ownership";break;
        case RAW_SOURCE_STOP_PATH:message="encounter result code path mismatch";break;
        case RAW_SOURCE_STOP_STORE:message="environment store/readback failed";break;
        case RAW_SOURCE_STOP_POISON:message="environment is poisoned; restart game";break;
        case RAW_SOURCE_STOP_APPLIED:message="environment attempt already used";break;
        case RAW_SOURCE_STOP_VERIFY:message="captured source failed readback check";break;
        default:message="source acquisition stopped";break;
        }
        p=diag_copy(p,"S");if(s->phase<10u)*p++='0';p=diag_number(p,s->phase);
        if(s->phase==RAW_SOURCE_STOP_PATH){
            *p++='/';*p++='R';if(s->result_reason<10u)*p++='0';p=diag_number(p,s->result_reason);
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->waiting_stage==RAW_WAIT_DIAG_ALIGNMENT&&
                 s->waiting_alignment.stage!=SOURCE_ALIGNED_OK){
            const SourceAlignedDiagnostic *a=&s->waiting_alignment;
            if(a->stage==SOURCE_ALIGNED_DOMAIN){
                p=diag_copy(p,"/T");if(a->domain_guard<10u)*p++='0';p=diag_number(p,a->domain_guard);
                message="initial timing guard rejected source";
            }else if(a->stage==SOURCE_ALIGNED_READ){
                p=diag_copy(p,"/M");p=diag_number(p,a->read_ordinal);
                p=diag_copy(p,"/P");p=diag_number(p,a->read_pass);*p++='@';p=diag_hex(p,a->read_address);
                message="initial state read failed";
            }else if(a->stage==SOURCE_ALIGNED_DRIFT){
                p=diag_copy(p,"/W01/D");message="initial paired state reads differed";
            }else{p=diag_copy(p,"/W01/B");message="initial scheduler boundary outside model";}
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->waiting_stage==RAW_WAIT_DIAG_BINDING){
            p=diag_copy(p,"/W02");message="initial counter/RNG/source did not match";
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->result_read_kind==RAW_RESULT_READ_FAILED){
            *p++='/';*p++='M';p=diag_number(p,s->result_read_ordinal);*p++='@';p=diag_hex(p,s->result_read_address);
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->result_read_kind==RAW_RESULT_READ_LAYOUT_REJECTED){
            p=diag_copy(p,"/L");
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->result_read_kind==RAW_RESULT_READS_DISAGREED){
            p=diag_copy(p,"/D");
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->result_reason){
            *p++='/';*p++='R';if(s->result_reason<10u)*p++='0';p=diag_number(p,s->result_reason);
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY&&s->environment_rejection){
            *p++='/';*p++='E';if(s->environment_rejection<10u)*p++='0';p=diag_number(p,s->environment_rejection);
            if(s->environment_rejection==RAW_ENV_SOURCE_SAVE_TIME)
                message=env_rejection_text(s->environment_rejection);
        }else if(s->phase==RAW_SOURCE_STOP_VERIFY){
            p=diag_copy(p,"/V");
        }else if(s->phase==RAW_SOURCE_STOP_STORE||s->phase==RAW_SOURCE_STOP_POISON){
            *p++='/';*p++='E';if(s->environment_status<10u)*p++='0';p=diag_number(p,s->environment_status);
        }
        *p=0;
        add_diag(h,color,code,message);return;
    }
    if(raw_runtime_source_recovery_pending(r)){
        add_diag(h,color,"S18","source expired; waiting for released revalidation");return;
    }
    if(s->refresh_readonly_retries&&r->refresh_needed){
        add_diag(h,color,"S18","rechecking source read; Target unavailable");return;
    }
    if(!d->source_requested||d->source_available)return;
    switch(s->phase){
    case RAW_SOURCE_WAIT_PROMPT:
        p=diag_copy(p,"G");if(s->scene_rejection<10u)*p++='0';p=diag_number(p,s->scene_rejection);
        if(s->scene_rejection_detail){
            *p++='/';
            *p++=(s->scene_rejection==CH_SCENE_POINTER_READ||
                s->scene_rejection==CH_SCENE_POINTER_INVALID)?'P':
                s->scene_rejection==CH_SCENE_POINTER_LAYOUT?'L':
                s->scene_rejection==CH_SCENE_BANK_UNSUPPORTED?'B':
                (s->scene_rejection==CH_SCENE_ROM_READ||s->scene_rejection==CH_SCENE_STATE_READ)?'R':
                s->scene_rejection==CH_SCENE_INPUT_HELD?'I':'C';
            if(s->scene_rejection==CH_SCENE_BANK_UNSUPPORTED)p=diag_hex16(p,s->scene_rejection_detail);
            else{if(s->scene_rejection_detail<10u)*p++='0';p=diag_number(p,s->scene_rejection_detail);}
            *p=0;
        }
        add_diag(h,color,code,scene_rejection_text(s->scene_rejection));return;
    case RAW_SOURCE_WAIT_UNIT:
        p=diag_copy(p,"S01/U");if(unit_reason(s)<10u)*p++='0';p=diag_number(p,unit_reason(s));*p=0;
        switch(unit_reason(s)){
        case 1:message="scheduler sample could not be read";break;
        case 2:message="scheduler fields changed during paired read";break;
        case 3:message="scheduler unit is outside supported mode";break;
        case 4:message="scheduler counter is unavailable";break;
        case 5:message="scheduler batch is not one";break;
        case 6:message="host input phase is not zero";break;
        case 7:message="game input engine type is unsupported";break;
        case 8:message="game input is not enabled for the modeled path";break;
        case 9:message="scheduler sample is incomplete";break;
        default:message="engine/task state unsupported";break;
        }
        {char detail[40];char *q=detail;q=diag_copy(q,"t=");q=diag_hex16(q,s->engine_type);
         q=diag_copy(q," b=");q=diag_hex16(q,s->batch_count);q=diag_copy(q," p=");
         q=diag_hex16(q,s->host_phase);q=diag_copy(q," en=");q=diag_hex16(q,s->input_enable);
         add_diag_detail(h,color,code,message,detail);return;}
    case RAW_SOURCE_WAIT_INPUT:
        p=diag_copy(p,"S02/I");*p++='0';
        p=diag_number(p,((s->host_held|s->host_intersection)&1u)?1u:2u);*p=0;
        message=((s->host_held|s->host_intersection)&1u)?
            "release A and wait for a neutral scan":"game has not released all buttons";
        {char detail[48];char *q=detail;q=diag_copy(q,"held=");q=diag_hex16(q,s->host_held);
         q=diag_copy(q," intersection=");q=diag_hex16(q,s->host_intersection);
         q=diag_copy(q," guest=");q=diag_hex16(q,s->guest_mask);
         add_diag_detail(h,color,code,message,detail);return;}
    case RAW_SOURCE_WAIT_CPU:
        {char detail[32];char *q=detail;q=diag_copy(q,"pc=");q=diag_hex16(q,s->guest_pc);
         q=diag_copy(q," sp=");q=diag_hex16(q,s->guest_sp);
         add_diag_detail(h,color,"S03","CPU outside 0460/C0D5 stop",detail);return;}
    case RAW_SOURCE_WAIT_READ:
        if(s->result_read_kind==RAW_RESULT_READ_FAILED){
            p=diag_copy(p,"S04/M");p=diag_number(p,s->result_read_ordinal);*p++='@';
            p=diag_hex(p,s->result_read_address);*p=0;
            add_diag(h,color,code,"result read failed at this address");return;
        }
        if(s->result_read_kind==RAW_RESULT_READ_LAYOUT_REJECTED){add_diag(h,color,"S04/L","result pointers/layout rejected");return;}
        if(s->result_read_kind==RAW_RESULT_READS_DISAGREED){
            {char detail[22];char *q=detail;q=diag_copy(q,"reads=");q=diag_number(q,s->result_read_calls);
             add_diag_detail(h,color,"S04/D","paired result reads differed",detail);return;}
        }
        add_diag(h,color,"S04/U","result snapshot unavailable");return;
    case RAW_SOURCE_WAIT_ENEMY:
        {char detail[42];char *q=detail;q=diag_copy(q,"nonzero=");q=diag_number(q,s->result_enemy_nonzero);
         q=diag_copy(q," first[");q=diag_number(q,s->result_enemy_first_index);q=diag_copy(q,"]=");
         q=diag_hex16(q,s->result_enemy_first_value);
         add_diag_detail(h,color,"S05","encounter buffer is not empty",detail);return;}
    case RAW_SOURCE_WAIT_RESULT:
        p=diag_copy(p,"S06/R");if(s->result_reason<10u)*p++='0';p=diag_number(p,s->result_reason);*p=0;
        message=result_reason_text(s->result_reason);break;
    case RAW_SOURCE_WAIT_PREFLIGHT:
        p=diag_copy(p,"S07/E");if(s->environment_rejection<10u)*p++='0';p=diag_number(p,s->environment_rejection);*p=0;
        message=env_rejection_text(s->environment_rejection);break;
    case RAW_SOURCE_WAIT_CPU_CONTEXT:
        p=diag_copy(p,"S15/E");if(s->environment_rejection<10u)*p++='0';p=diag_number(p,s->environment_rejection);*p=0;
        message="CPU context outside supported model";break;
    case RAW_SOURCE_WAIT_CPU_READ:
        p=diag_copy(p,"S16/E");if(s->environment_rejection<10u)*p++='0';p=diag_number(p,s->environment_rejection);*p=0;
        message="CPU context read failed";break;
    case RAW_SOURCE_PREPARED:message="environment prepared; waiting for a fresh scheduler state";break;
    default:return;
    }
    if(!*code){p=diag_copy(p,"S");if(s->phase<10u)*p++='0';p=diag_number(p,s->phase);*p=0;}
    add_diag(h,color,code,message);
}
static int sample(void *u,RawSample *s){Device *d=u;int scope=ch_3ds_read_begin(&d->backend);
    d->binding.omit_arrival_check=ch_raw_service.runtime.encounter_started;
    int good=raw_platform_sample(&d->binding,s);
    if(scope)ch_3ds_read_end(&d->backend);
    RawDeviceSourceDiagnostic *a=&d->source_diagnostic;uint32_t j;
    LightLock_Lock(&d->source_lock);
    a->counter=s->counter;a->guest_pc=d->binding.last_prompt.guest_pc;
    a->guest_sp=d->binding.last_prompt.guest_sp;a->guest_mask=s->guest_mask;
    a->sample_status=(uint32_t)d->binding.last_sample_status;
    a->scene_status=(uint32_t)d->binding.last_scene_status;
    a->scene_rejection=d->binding.last_prompt.rejection;
    a->scene_rejection_detail=d->binding.last_prompt.rejection_detail;
    a->counter_valid=s->counter_read;a->ordinary_supported=s->ordinary_supported;
    a->engine_type=s->input_type;a->batch_count=s->batch;a->host_phase=s->host_phase;
    a->input_enable=s->input_enable;a->host_held=s->host_held;a->host_down=s->host_down;
    a->host_up=s->host_up;a->host_intersection=s->host_intersection;
    a->scene_script_mode=d->binding.last_prompt.script_mode;
    a->scene_script_running=d->binding.last_prompt.script_running;
    a->scene_script_bank=d->binding.last_prompt.script_bank;
    a->scene_script_next=d->binding.last_prompt.script_next;
    a->scene_map_group=d->binding.last_prompt.map_group;
    a->scene_map_number=d->binding.last_prompt.map_number;
    a->engine_pointer=d->binding.last_boundary.engine_pointer;
    a->recording_gate=d->binding.last_boundary.recording_gate;
    a->queued_tasks=d->binding.last_boundary.queued_tasks;
    a->engine_mode=d->binding.last_boundary.engine_mode;
    a->engine_flags=d->binding.last_boundary.engine_flags;
    a->selected_config_pointer=d->binding.last_boundary.selected_config_pointer;
    a->selected_config_mode=d->binding.last_boundary.selected_config_mode;
    a->previous_provider_keys=d->binding.last_boundary.previous_provider_keys;
    a->scene_rom_pointer=d->binding.last_prompt.rom_pointer;
    a->scene_wram0_pointer=d->binding.last_prompt.wram0_pointer;
    a->scene_wram1_pointer=d->binding.last_prompt.wram1_pointer;
    a->scene_hram_pointer=d->binding.last_prompt.hram_pointer;
    a->scene_io_pointer=d->binding.last_prompt.io_pointer;
    a->scene_rom_bank=d->binding.last_prompt.rom_bank;
    a->scene_stack_wait_seen=d->binding.last_prompt.stack_wait_seen;
    a->scene_stack_joy_seen=d->binding.last_prompt.stack_joy_seen;
    a->scene_joyp=d->binding.last_prompt.joyp;
    for(j=0;j<8u;j++)a->scene_joy_mirrors[j]=d->binding.last_prompt.joy_mirrors[j];
    if(!a->closed&&!a->attempts) {
        a->phase=!s->script_final_prompt?RAW_SOURCE_WAIT_PROMPT:
            !s->ordinary_supported?RAW_SOURCE_WAIT_UNIT:
            ((s->host_held|s->host_intersection)&1u)||s->guest_mask!=0xffffu?RAW_SOURCE_WAIT_INPUT:
            RAW_SOURCE_WAIT_CPU;
    }
    LightLock_Unlock(&d->source_lock);return good;
}
static int keys(void *u,uint32_t *k){Device *d=u;return raw_platform_keys(&d->binding,k);}
/* Keep full rejection reasons in their diagnostic records. The player HUD
   carries only requested fields; a pending output restore changes its title
   color instead of adding the former research/status panel. */
static void diagnose_hud(Device *d,const RawRuntime *r,RawHud *h){
    if(h->count&&d->pause_audio.initialized&&d->pause_audio_status!=1&&
       (r->runtime==CH_RUNTIME_PAUSED||d->pause_audio.owned))h->color[0]=0xffdd70u;
    LightLock_Lock(&d->source_lock);
    d->source_diagnostic.runtime_fault=r->fault;d->source_diagnostic.query_status=r->query_status;
    d->source_diagnostic.query_detail=r->query_failure_detail;
    d->source_diagnostic.source_bound=r->source_bound;
    d->source_diagnostic.candidate_valid=r->controls.candidate_valid;
    d->source_diagnostic.waiting_valid=r->waiting_valid;
    d->source_diagnostic.current_advance=r->counter;
    d->source_diagnostic.target_advance=r->raw_target;
    d->source_diagnostic.input_plan_error=r->input_plan.error;
    add_source_diag(d,r,h);
    LightLock_Unlock(&d->source_lock);
}
static void hud(void *u,const RawHud *h){Device *d=u;RawHud diagnosed=*h;int changed;
    diagnose_hud(d,&ch_raw_service.runtime,&diagnosed);
    LightLock_Lock(&d->hud_lock);
    changed=memcmp(&d->hud,&diagnosed,sizeof(diagnosed))!=0;
    if(changed)d->hud=diagnosed;
    LightLock_Unlock(&d->hud_lock);
    /* Publication is a value update, not a frame submission. The original
       presenter still paints every actual fresh image using this same HUD. */
    if(changed)raw_hud_3ds_publish(&d->screen,&diagnosed);
    if(d->host.publish_hud)d->host.publish_hud(d->host.user,&diagnosed);
}
static void wait_poll(void *u){(void)u;svcSleepThread(5000000);}
static int submit(void *u,const RawJob *j){Device *d=u;int ok;
    LightLock_Lock(&d->job_lock);ok=raw_mailbox_submit(&d->mailbox,j);LightLock_Unlock(&d->job_lock);return ok;
}
static int receive(void *u,RawJob *j,ManualPrediction *p){Device *d=u;int ok;
    LightLock_Lock(&d->job_lock);ok=raw_mailbox_receive(&d->mailbox,j,p);LightLock_Unlock(&d->job_lock);return ok;
}
static void worker(void *u){Device *d=u;RawJob j;ManualPrediction p;int have,stop;
    for(;;){
        LightLock_Lock(&d->job_lock);stop=(int)d->stop;
        have=stop?0:raw_mailbox_take(&d->mailbox,&j);LightLock_Unlock(&d->job_lock);
        if(stop)return;
        if(!have){svcSleepThread(1000000);continue;}
        raw_job_solve_cached(&j,0,&worker_query_cache,&p);
        LightLock_Lock(&d->job_lock);raw_mailbox_finish(&d->mailbox,&j,&p);LightLock_Unlock(&d->job_lock);
    }
}
#ifdef CH_RAW_DEVICE_COORDINATOR_TEST
/* Only the offline coordinator fixture replaces the privileged TLS read. */
extern uint32_t raw_device_test_tls(void);
static uint32_t tls(void){return raw_device_test_tls();}
#else
static uint32_t tls(void){uint32_t v;__asm__ volatile("mrc p15,0,%0,c13,c0,3":"=r"(v));return v;}
#endif
static int stop_source(Device *d,uint32_t phase){
    d->source_requested=0;d->source_diagnostic.closed=1;d->source_diagnostic.phase=phase;return -1;
}
/* These retries occur only before a new environment write. Every failed
   attempt consumes the finite budget and waits for actual original advances. */
static int retry_initial_readonly(Device *d,uint32_t counter){
    RawDeviceSourceDiagnostic *a=&d->source_diagnostic;
    a->initial_retry_counter=counter;
    if(++a->initial_readonly_retries>=RAW_SOURCE_READONLY_RETRIES)
        return stop_source(d,RAW_SOURCE_STOP_VERIFY);
    return 0;
}
static int retry_refresh_readonly(Device *d,uint32_t counter){
    RawDeviceSourceDiagnostic *a=&d->source_diagnostic;
    RawRuntime *r=&ch_raw_service.runtime;
    /* The old search window stays withdrawn throughout the backoff. A stale
       worker job must not clear the pending checked-source refresh. */
    r->source_bound=0;r->waiting_valid=0;d->waiting_observation_valid=0;
    ch_controller_request_query(&r->controls);memset(&r->view.query,0,sizeof(r->view.query));
    a->refresh_retry_counter=counter;
    if(++a->refresh_readonly_retries>=RAW_SOURCE_READONLY_RETRIES)return -1;
    return 0;
}
/* The original engine caller owns these reads. Future timing remains a
   read-only projection; neither successful observation nor a mismatch supplies
   an input, pauses the game, or alters a timer. */
static void save_waiting_alignment(Device *d){
    RawDeviceSourceDiagnostic *s=&d->source_diagnostic;const SourceAlignedDiagnostic *a=&d->waiting_read_diagnostic;
    s->waiting_guard=a->domain_guard;s->waiting_alignment_stage=a->stage;
    s->waiting_read_ordinal=a->read_ordinal;s->waiting_read_address=a->read_address;
    s->waiting_read_size=a->read_size;s->waiting_read_pass=a->read_pass;s->waiting_alignment=*a;
}
static void waiting_discard(Device *d,uint32_t stage,const CQWaitingState *expected,
    const CQWaitingState *actual,const SourceObservation *observed){
    RawRuntime *r=&ch_raw_service.runtime;RawWaitingDiagnostic *a=&d->waiting_diagnostic;
    if(!a->first_valid){a->first_valid=1u;a->first_failure_counter=r->counter;}
    a->failures++;a->stage=stage;a->counter=r->counter;a->previous_counter=r->waiting_counter;
    a->target=r->raw_target;a->applied_mask=d->binding.last_boundary.guest_active_low_mask;
    a->previous_vblank=d->waiting_observation.vblank;
    a->actual_vblank=observed?observed->vblank:0u;a->previous=r->waiting;
    if(expected)a->expected=*expected;else memset(&a->expected,0,sizeof(a->expected));
    if(actual)a->actual=*actual;else memset(&a->actual,0,sizeof(a->actual));
    a->aligned=d->waiting_read_diagnostic;
    d->source_diagnostic.waiting_stage=stage;
    save_waiting_alignment(d);
    d->source_diagnostic.waiting_failures=a->failures;
    d->source_diagnostic.waiting_expected=a->expected;
    d->source_diagnostic.waiting_actual=a->actual;
    if(!d->source_diagnostic.waiting_unavailable_failures)
        d->source_diagnostic.waiting_unavailable_since=r->counter;
    if(!d->source_diagnostic.waiting_unavailable_failures||
       d->source_diagnostic.waiting_unavailable_counter!=r->counter)
        d->source_diagnostic.waiting_unavailable_failures++;
    d->source_diagnostic.waiting_unavailable_counter=r->counter;
    /* Losing a conditional trajectory is not a failed environment write.
       Cancel every old token/plan; only a new complete actual seed may query.
       Keep player execution and the established source/RTC ownership intact. */
    r->waiting_valid=0;r->plan_active=0;r->plan_pending=0;
    r->input_condition_matches=0;d->waiting_observation_valid=0;
    ch_controller_request_query(&r->controls);memset(&r->view.query,0,sizeof(r->view.query));
}
static int observe_waiting(Device *d,const ManualSourceBinding *fresh){
    RawRuntime *r=&ch_raw_service.runtime;SourceObservation observed;
    CQWaitingState actual,expected;SourceNormalConditions conditions;
    SourceInferredNormalPair pair;uint32_t delta;int status;
    int scope=ch_3ds_read_begin(&d->backend);
    status=source_aligned_waiting_diagnostic(&d->binding.read,&d->binding.last_boundary,
        &d->binding.last_prompt,d->binding.source_epoch,&observed,&actual,&d->waiting_read_diagnostic);
    if(scope)ch_3ds_read_end(&d->backend);
    if(status!=1){
        if(fresh){
            /* Initial admission failures also need a public read-only record.
               Do not cancel runtime plans or count a waiting-trajectory loss. */
            d->source_diagnostic.waiting_stage=RAW_WAIT_DIAG_ALIGNMENT;
            save_waiting_alignment(d);return 0;
        }
        waiting_discard(d,RAW_WAIT_DIAG_ALIGNMENT,0,0,0);return 1;
    }
    if(fresh){
        if(observed.counter!=fresh->origin_counter||actual.a!=fresh->rng_add||
           actual.s!=fresh->rng_sub||observed.epoch!=fresh->source_epoch){
            d->source_diagnostic.waiting_stage=RAW_WAIT_DIAG_BINDING;
            d->source_diagnostic.waiting_actual=actual;
            memset(&d->source_diagnostic.waiting_expected,0,sizeof(d->source_diagnostic.waiting_expected));
            d->source_diagnostic.waiting_expected.a=fresh->rng_add;
            d->source_diagnostic.waiting_expected.s=fresh->rng_sub;
            save_waiting_alignment(d);return 0;
        }
        d->source_diagnostic.waiting_stage=0u;
    }else{
        if(observed.counter!=r->counter||observed.epoch!=r->source.source_epoch||
           actual.bg!=(r->source_bg+(observed.counter-r->source.origin_counter)%3u)%3u){
            waiting_discard(d,RAW_WAIT_DIAG_BINDING,0,&actual,&observed);
            r->refresh_needed=1u;return 1;
        }
        if(!r->waiting_valid||!d->waiting_observation_valid)goto accept_actual;
        delta=observed.counter-r->waiting_counter;
        if(delta>1u){waiting_discard(d,RAW_WAIT_DIAG_COUNTER,0,&actual,&observed);goto accept_actual;}
        expected=r->waiting;
        if(delta&&!cq_waiting_next(&expected)){
            waiting_discard(d,RAW_WAIT_DIAG_PROJECTION,0,&actual,&observed);goto accept_actual;}
        if(memcmp(&expected,&actual,sizeof(actual))){
            waiting_discard(d,RAW_WAIT_DIAG_STATE,&expected,&actual,&observed);goto accept_actual;}
        if(delta){
            memset(&conditions,0,sizeof(conditions));conditions.abi=SOURCE_OBSERVER_ABI;
            conditions.restricted_wait_path_model=1u;conditions.normal_pair_count=1u;
            conditions.epoch=observed.epoch;conditions.before_counter=r->waiting_counter;
            conditions.after_counter=observed.counter;
            if(source_observer_infer(&d->waiting_observation,&observed,&conditions,&pair)!=SOURCE_OBSERVER_OK){
                waiting_discard(d,RAW_WAIT_DIAG_INFER,&expected,&actual,&observed);goto accept_actual;}
            /* Check both operands independently from RNG changes, without
               scanning empirical phase hypotheses on the emulation thread. */
            {uint8_t predicted[2];
             if(!released_normal_pair21(&r->waiting.clock,predicted)||
                pair.div1!=predicted[0]||pair.div2!=predicted[1]){
                 waiting_discard(d,RAW_WAIT_DIAG_OPERANDS,&expected,&actual,&observed);goto accept_actual;}}
        }
    }
accept_actual:
    r->waiting=actual;r->waiting_counter=observed.counter;r->waiting_valid=1u;
    d->source_diagnostic.waiting_unavailable_failures=0;
    d->source_diagnostic.waiting_unavailable_since=d->source_diagnostic.waiting_unavailable_counter=0;
    d->waiting_observation=observed;d->waiting_observation_valid=1u;return 1;
}
static int refresh_source(Device *d,const ChNativeContext *c,ManualSourceBinding *s,uint64_t *generation){
    RawRuntime *r=&ch_raw_service.runtime;const ChBoundarySample *b=&d->binding.last_boundary;
    int got=0;
    if(!r->refresh_needed||(!r->search_started&&r->runtime!=CH_RUNTIME_STEPPING)||r->step_by_physical_a||
       r->original_raw_a_seen||r->actual_raw_press_seen||r->encounter_started||r->fault||
       r->controls.candidate_valid||r->plan_active||
       (!r->source_bound&&!raw_runtime_source_recovery_pending(r))||!d->source_available||
       !d->environment.state.initial_applied||!d->environment.state.rtc_owned||
       d->environment.state.terminal_applied||d->environment.state.poisoned||
       d->environment.cleanup_attempted||d->environment.terminal_attempted)return 0;
    if(!c||c->lr!=0x1a8340u||d->binding.last_scene_status!=1||!d->binding.last_prompt.match||
       d->binding.last_prompt.guest_pc!=0x460u||d->binding.last_prompt.guest_sp!=0xc0d5u||
       d->binding.last_sample_status!=CH_SAMPLE_OK||!b->complete||!b->coherent||!b->ordinary_supported||
       ((b->host_cache[0]|b->host_cache[3])&1u)||b->guest_active_low_mask!=0xffffu)return 0;
    /* Repeated original callbacks at this counted unit cannot redo a failed
       request. Only a later real counter boundary permits another attempt. */
    if(d->refresh_attempted&&d->refresh_attempt_counter==b->counter)return 0;
    if(d->source_diagnostic.refresh_readonly_retries&&
       b->counter-d->source_diagnostic.refresh_retry_counter<RAW_SOURCE_RETRY_ADVANCES)return 0;
    d->refresh_attempted=1;d->refresh_attempt_counter=b->counter;
    d->source_diagnostic.cpu_check_status=(uint32_t)raw_environment_initial_cpu_check(&d->binding.read);
    if(d->source_diagnostic.cpu_check_status!=RAW_ENV_CPU_MATCH)return 0;
    if(!raw_environment_backend_enter(&d->environment_backend,c))return stop_source(d,RAW_SOURCE_STOP_OWNERSHIP);
    d->source_diagnostic.waiting_stage=0;
    memset(&d->refresh_result_source,0,sizeof(d->refresh_result_source));
    d->refresh_result_source.epoch=d->binding.source_epoch;
    if(result_snapshot_traced(d,&d->refresh_result_source.source)){
        ch_platform_sha256((const uint8_t *)&d->refresh_result_source.source,
            sizeof(d->refresh_result_source.source),d->refresh_result_source.source_identity);
        if(ch_result_begin(&d->refresh_result_gate,&d->refresh_result_source)==CH_RESULT_PENDING){
            got=raw_environment_session_rebase(&d->environment,c,tls(),d->binding.source_epoch,&d->capture,s,generation);
            if(got){
                if(!result_snapshot_traced(d,&d->result_snapshot)||
                   memcmp(&d->refresh_result_source.source,&d->result_snapshot,sizeof(d->result_snapshot)))got=-1;
                else{
                    memcpy(d->refresh_result_source.source_identity,s->source_identity.bytes,32);
                    if(ch_result_begin(&d->refresh_result_gate,&d->refresh_result_source)!=CH_RESULT_PENDING)got=-1;
                    else{
                        d->result_source=d->refresh_result_source;d->result_gate=d->refresh_result_gate;
                        d->environment.generation=*generation;d->source_diagnostic.phase=RAW_SOURCE_ADMITTED;
                    }
                }
            }else if(d->environment.last_status==RAW_ENV_STORE_FAILED||
                     d->environment.last_status==RAW_ENV_POISONED||d->environment.state.poisoned)got=-1;
            else got=retry_refresh_readonly(d,b->counter);
        }else got=-1;
    }else if(d->source_diagnostic.result_read_kind==RAW_RESULT_READ_FAILED||
             d->source_diagnostic.result_read_kind==RAW_RESULT_READS_DISAGREED)
        got=retry_refresh_readonly(d,b->counter);
    else got=-1;
    if(got==1){r->source_bg=d->environment.state.source_bg;
        if(!observe_waiting(d,s))got=-1;
        else d->source_diagnostic.refresh_readonly_retries=0;}
    raw_environment_backend_leave(&d->environment_backend);return got;
}
static int source(void *u,const ChNativeContext *c,ManualSourceBinding *s,uint64_t *generation){
    Device *d=u;RawDeviceSourceDiagnostic *a=&d->source_diagnostic;
    const ChBoundarySample *b=&d->binding.last_boundary;uint32_t i;int got=0,admitted=0;
    /* Typed settings and full actual before/after reads stay on the owned
       original engine caller, never on the solver or presentation thread. */
    LightLock_Lock(&d->source_lock);
    if(ch_raw_service.runtime.refresh_needed){
        admitted=refresh_source(d,c,s,generation);
        if(admitted<0){
            uint32_t phase=a->closed&&a->phase>=RAW_SOURCE_STOP_OWNERSHIP&&a->phase<=RAW_SOURCE_STOP_VERIFY?
                a->phase:d->environment.state.poisoned||d->environment.last_status==RAW_ENV_POISONED?
                RAW_SOURCE_STOP_POISON:d->environment.last_status==RAW_ENV_STORE_FAILED?
                RAW_SOURCE_STOP_STORE:RAW_SOURCE_STOP_VERIFY;
            a->result_reason=d->refresh_result_gate.reason;
            admitted=stop_source(d,phase);
        }
        goto done;
    }
    if(ch_raw_service.runtime.search_requested&&!a->closed)d->source_requested=1;
    if(!d->source_requested||a->closed)goto done;
    if(d->environment.state.poisoned||d->environment.last_status==RAW_ENV_POISONED) {
        admitted=stop_source(d,RAW_SOURCE_STOP_POISON);goto done;
    }
    if(d->environment.last_status==RAW_ENV_STORE_FAILED) {
        admitted=stop_source(d,RAW_SOURCE_STOP_STORE);goto done;
    }
    if(d->environment.state.preparation_applied&&d->environment.cleanup_attempted){
        admitted=stop_source(d,RAW_SOURCE_STOP_APPLIED);goto done;
    }
    if(d->source_available||d->environment.state.initial_applied||
       (d->environment.state.rtc_owned&&!d->environment.state.preparation_applied)) {
        admitted=stop_source(d,RAW_SOURCE_STOP_APPLIED);goto done;
    }
    if(!c||c->lr!=0x1a8340u||d->binding.last_scene_status!=1||!d->binding.last_prompt.match) {
        a->phase=RAW_SOURCE_WAIT_PROMPT;goto done;
    }
    if(d->binding.last_sample_status!=CH_SAMPLE_OK||!b->complete||!b->coherent||!b->ordinary_supported) {
        a->phase=RAW_SOURCE_WAIT_UNIT;goto done;
    }
    if(((b->host_cache[0]|b->host_cache[3])&1u)||b->guest_active_low_mask!=0xffffu) {
        a->phase=RAW_SOURCE_WAIT_INPUT;goto done;
    }
    /* Scene identity admits several positions within the wait. The captured
       model accepts only this exact CPU position, already read by sample().
       No extra process/SVC reads are needed to reject the other positions. */
    if(d->binding.last_prompt.guest_pc!=0x460u||d->binding.last_prompt.guest_sp!=0xc0d5u) {
        if(!a->attempts)a->phase=RAW_SOURCE_WAIT_CPU;
        goto done;
    }
    /* Full read-only rejection is retryable, but not on every original unit
       or repeated callback. Once the cooldown has elapsed, the next matching
       CPU position is tried immediately (there is no counter modulo gate). */
    if(d->environment.state.preparation_applied&&b->counter==d->environment.state.preparation_counter){
        a->phase=RAW_SOURCE_PREPARED;goto done;
    }
    if(d->environment.state.preparation_applied&&a->initial_readonly_retries&&
       b->counter-a->initial_retry_counter<RAW_SOURCE_RETRY_ADVANCES)goto done;
    if(a->attempts&&!d->environment.state.preparation_applied&&b->counter-a->last_attempt_counter<16u)goto done;
    a->attempts++;a->last_attempt_counter=b->counter;
    a->result_enemy_nonzero=0u;a->result_enemy_first_index=UINT32_MAX;a->result_enemy_first_value=0u;
    a->result_reason=0u;a->environment_rejection=RAW_ENV_SOURCE_PREFLIGHT;
    a->result_read_kind=0u;a->result_read_calls=0u;a->result_read_ordinal=0u;
    a->result_read_address=0u;a->result_read_size=0u;
    /* Check only semantic CPU/context invariants with five small reads.
       A match is not admission: full capture, environment preflight and
       result-origin validation still run independently. */
    a->cpu_check_status=(uint32_t)raw_environment_initial_cpu_check(&d->binding.read);
    if(d->environment.state.preparation_applied&&a->cpu_check_status!=(uint32_t)RAW_ENV_CPU_MATCH) {
        a->phase=a->cpu_check_status==(uint32_t)RAW_ENV_CPU_MISMATCH?
            RAW_SOURCE_WAIT_CPU_CONTEXT:RAW_SOURCE_WAIT_CPU_READ;
        a->environment_rejection=a->cpu_check_status==(uint32_t)RAW_ENV_CPU_MISMATCH?RAW_ENV_SOURCE_CPU:RAW_ENV_SOURCE_CAPTURE;
        admitted=retry_initial_readonly(d,b->counter);
        goto done;
    }
    a->full_attempts++;
    if(raw_environment_backend_enter(&d->environment_backend,c)){
        memset(&d->result_source,0,sizeof(d->result_source));
        d->result_source.epoch=d->binding.source_epoch;
        if(result_snapshot_traced(d,&d->result_source.source)) {
            /* Preliminary gate uses this exact read image's real digest. The
               final gate binds the larger source receipt returned below. */
            ch_platform_sha256((const uint8_t *)&d->result_source.source,
                sizeof(d->result_source.source),d->result_source.source_identity);
            if(ch_result_begin(&d->result_gate,&d->result_source)==CH_RESULT_PENDING) {
                if(!d->environment.state.preparation_applied){
                    got=raw_environment_session_prepare(&d->environment,c,tls(),d->binding.source_epoch);
                    if(got){
                        /* No source receipt, generation or query is created
                           here. Only original player-controlled execution
                           can reach the fresh readback origin. */
                        got=0;a->phase=RAW_SOURCE_PREPARED;
                    }else if(d->environment.last_status==RAW_ENV_STORE_FAILED)
                        admitted=stop_source(d,RAW_SOURCE_STOP_STORE);
                    else if(d->environment.last_status==RAW_ENV_POISONED||d->environment.state.poisoned)
                        admitted=stop_source(d,RAW_SOURCE_STOP_POISON);
                    else {
                        a->environment_rejection=raw_environment_initial_source_rejection(&d->environment.plan,&d->environment.state);
                        if(a->environment_rejection==RAW_ENV_SOURCE_SAVE_TIME)
                            admitted=stop_source(d,RAW_SOURCE_STOP_VERIFY);
                        else a->phase=RAW_SOURCE_WAIT_PREFLIGHT;
                    }
                    goto backend_done;
                }
                got=raw_environment_session_source(&d->environment,c,tls(),d->binding.source_epoch,&d->capture,s,generation);
                if(got) {
                    /* Close before post-apply verification: even a later
                       verification failure cannot reapply or rebuild source. */
                    d->source_requested=0;a->closed=1;
                    if(!result_snapshot_traced(d,&d->result_snapshot)||
                       memcmp(&d->result_source.source,&d->result_snapshot,sizeof(d->result_snapshot))) {
                        got=0;admitted=stop_source(d,RAW_SOURCE_STOP_VERIFY);
                    } else {
                        memcpy(d->result_source.source_identity,s->source_identity.bytes,32);
                        if(ch_result_begin(&d->result_gate,&d->result_source)!=CH_RESULT_PENDING){
                            got=0;admitted=stop_source(d,RAW_SOURCE_STOP_VERIFY);
                        }
                    }
                    if(!got&&d->environment.state.rtc_owned) {
                        d->environment.cleanup_attempted=1;
                        d->environment.last_status=(uint32_t)raw_environment_restore_rtc(&d->environment.ops,&d->environment.state);
                        if(d->environment.state.poisoned)a->phase=RAW_SOURCE_STOP_POISON;
                    }
                } else if(d->environment.last_status==RAW_ENV_STORE_FAILED)
                    admitted=stop_source(d,RAW_SOURCE_STOP_STORE);
                else if(d->environment.last_status==RAW_ENV_POISONED||d->environment.state.poisoned)
                    admitted=stop_source(d,RAW_SOURCE_STOP_POISON);
                else if(d->environment.state.initial_applied||
                        (d->environment.state.rtc_owned&&!d->environment.state.preparation_applied))
                    admitted=stop_source(d,RAW_SOURCE_STOP_APPLIED);
                else {
                    a->environment_rejection=raw_environment_initial_source_rejection(&d->environment.plan,&d->environment.state);
                    if(a->environment_rejection==RAW_ENV_SOURCE_SAVE_TIME)
                        admitted=stop_source(d,RAW_SOURCE_STOP_VERIFY);
                    else a->phase=a->environment_rejection==RAW_ENV_SOURCE_CPU?
                            RAW_SOURCE_WAIT_CPU_CONTEXT:RAW_SOURCE_WAIT_PREFLIGHT;
                }
            } else if(d->result_gate.reason==CH_RESULT_BAD_PATH)
                admitted=stop_source(d,RAW_SOURCE_STOP_PATH);
            else {
                a->phase=RAW_SOURCE_WAIT_RESULT;
                a->result_enemy_nonzero=0u;a->result_enemy_first_index=UINT32_MAX;
                a->result_enemy_first_value=0u;
                for(i=0;i<CH_RESULT_ENEMY_BYTES;i++)if(d->result_source.source.enemy[i]) {
                    if(a->result_enemy_first_index==UINT32_MAX){
                        a->result_enemy_first_index=i;a->result_enemy_first_value=d->result_source.source.enemy[i];
                    }
                    a->result_enemy_nonzero++;
                }
                if(a->result_enemy_nonzero)a->phase=RAW_SOURCE_WAIT_ENEMY;
            }
        } else a->phase=RAW_SOURCE_WAIT_READ;
backend_done:
        if(!got&&!admitted&&d->environment.state.preparation_applied&&
           !d->environment.state.initial_applied&&!d->environment.state.poisoned&&
           a->phase!=RAW_SOURCE_PREPARED)
            admitted=retry_initial_readonly(d,b->counter);
        raw_environment_backend_leave(&d->environment_backend);
    } else admitted=stop_source(d,RAW_SOURCE_STOP_OWNERSHIP);
    if(got){
        ch_raw_service.runtime.source_bg=d->environment.state.source_bg;
        if(observe_waiting(d,s)){d->source_available=1;a->phase=RAW_SOURCE_ADMITTED;
            a->initial_readonly_retries=0;admitted=1;}
        else{got=0;admitted=stop_source(d,RAW_SOURCE_STOP_VERIFY);}
    }
done:
    /* Preserve the failing operation's status; successful RTC cleanup must
       not replace an environment-store error with an apparent OK receipt. */
    a->environment_status=d->environment.last_status;
    /* Failed preparation/admission must release an owned RTC instruction at
       the same original caller. No second environment attempt follows. */
    if(admitted<0&&d->environment.state.rtc_owned&&!d->environment.cleanup_attempted&&
       c&&c->lr==0x1a8340u&&raw_environment_backend_enter(&d->environment_backend,c)){
        d->environment.cleanup_attempted=1;
        d->environment.last_status=(uint32_t)raw_environment_restore_rtc(&d->environment.ops,&d->environment.state);
        raw_environment_backend_leave(&d->environment_backend);
        if(d->environment.state.poisoned){
            a->phase=RAW_SOURCE_STOP_POISON;a->environment_status=RAW_ENV_POISONED;
        }
    }
    if(admitted>=0||!ch_raw_service.runtime.refresh_needed)a->result_reason=d->result_gate.reason;
    LightLock_Unlock(&d->source_lock);
    /* The returned prediction remains conditional. A paired role read is
       neither global external-writer quiescence nor manual hardware proof. */
    return admitted;
}
static int first(void *u,const ChNativeContext *c,const RawRuntime *r){Device *d=u;int status=0;
    LightLock_Lock(&d->source_lock);
    if(raw_environment_session_first_needed(&d->environment,r)&&raw_environment_backend_enter(&d->environment_backend,c)){
        status=raw_environment_session_first(&d->environment,c,tls(),d->binding.source_epoch,r);
        raw_environment_backend_leave(&d->environment_backend);
    }else if(raw_environment_session_first_needed(&d->environment,r))status=-1;
    if(status>=0&&r->source_bound&&!r->encounter_started&&!r->fault)
        (void)observe_waiting(d,0);
    LightLock_Unlock(&d->source_lock);return status;
}
static void present(void *u,const ChNativeContext *c){Device *d=u;RawPresentArgs a;
    /* The pause owner alone may paint its leased slots. Retained slots also
       stay excluded after an unconfirmed restore. */
    if(d->pause_display.state==RAW_PAUSE_STEP_WAIT){
        if(raw_present_decode(&d->presentation,c,&a)&&a.screen_id==0){
            RawPauseRecord record={a.swap,a.fb_a,a.fb_b,a.stride,a.format,a.display_select,0};
            if(raw_paused_display_game_present(&d->pause_display,&record))
                (void)raw_hud_3ds_original_unpainted_present(&d->screen,a.screen_id,a.swap,
                    (uint8_t *)(uintptr_t)a.fb_a,(uint8_t *)(uintptr_t)a.fb_b,
                    a.stride,a.format);
        }
        return;
    }
    if(d->pause_display.state!=RAW_PAUSE_OFF)return;
    /* Decoder reads current original register/stack parameters; no retained
       surface, extra present operation, controller or source reads. */
    if(raw_present_decode(&d->presentation,c,&a))
        raw_hud_3ds_present(&d->screen,a.screen_id,a.swap,(uint8_t *)(uintptr_t)a.fb_a,
            (uint8_t *)(uintptr_t)a.fb_b,a.stride,a.format);
}
static void completed_owned(void *u,const ChNativeContext *c,RawRuntime *r,uint32_t good){
    Device *d=u;ChResultFrameReceipt receipt;int status;
    if(d->result_gate.abi!=CH_RESULT_ABI||d->result_gate.status!=(uint32_t)CH_RESULT_PENDING||
       !d->source_available)return;
    if(!good||c->lr!=0x1042f4u||!r->have_previous_scan){
        ch_result_invalidate(&d->result_gate,CH_RESULT_BAD_UNIT);return;
    }
    if(!ch_result_read_progress(&d->binding.read,&d->result_progress)){
        ch_result_invalidate(&d->result_gate,CH_RESULT_BAD_MAPPING);return;
    }
    memset(&receipt,0,sizeof(receipt));receipt.epoch=r->source.source_epoch;
    memcpy(receipt.source_identity,r->source.source_identity.bytes,32);
    receipt.current_counter=r->counter;receipt.previous_scan_counter=r->scan_before_counter;
    receipt.engine_entries=r->engine_entries;receipt.previous_ordinary_supported=r->previous_unit_supported;
    receipt.current_ordinary_supported=r->unit_supported;
    receipt.raw_press_counter=r->actual_raw_press;receipt.effective_press_counter=r->effective_press;
    receipt.press_marker=0x1a82dcu;receipt.press_mask=(uint16_t)r->actual_press_mask;
    /* Keep the generated-data reader independent from the strict manual
       release certificate. The runtime retains its original release checks. */
    receipt.press_observed=r->actual_raw_press_seen&&r->encounter_started;
    status=ch_result_observe_progress(&d->result_gate,&d->result_progress,&receipt);
    if(status==CH_RESULT_READY){
        if(!result_snapshot_traced(d,&d->result_snapshot)){
            ch_result_invalidate(&d->result_gate,CH_RESULT_BAD_MAPPING);return;
        }
        status=ch_result_complete(&d->result_gate,&d->result_snapshot,&d->actual_result);
    }
    if(status==CH_RESULT_COMPLETE&&r->actual_raw_press_seen&&r->encounter_started)
        raw_runtime_actual_dv(r,(uint16_t)d->actual_result.dv);
}
static void completed(void *u,const ChNativeContext *c,RawRuntime *r,uint32_t good){Device *d=u;
    /* If the bounded display exit had to retain buffers, retry an outstanding
       output-volume restore on the same original caller, before HID continues.
       Audio ownership never fabricates another gameplay command. */
    if(c&&c->lr==0x1042f4u&&r->runtime==CH_RUNTIME_RUNNING&&d->pause_audio.owned)
        d->pause_audio_status=raw_paused_audio_leave(&d->pause_audio,1);
    LightLock_Lock(&d->source_lock);
    {int scope=ch_3ds_read_begin(&d->backend);completed_owned(u,c,r,good);
    if(scope)ch_3ds_read_end(&d->backend);}
    LightLock_Unlock(&d->source_lock);
}
static int paused(void *u,const ChNativeContext *c,const RawRuntime *r,uint32_t action){
    Device *d=u;RawHud h;int display;
    if(!c||!r||c->lr!=0x1042f4u||!r->at_gate||r->scan_open)return RAW_PAUSE_ERROR;
    if(action==4)return raw_paused_display_step(&d->pause_display,1);
    /* Restore normally; a bounded failed restore retains owned buffers and
       yields to the player's pending command. No fallback fabricates keys. */
    if(action==2||action==3){
        d->pause_audio_status=raw_paused_audio_leave(&d->pause_audio,1);
        display=action==2?raw_paused_display_leave(&d->pause_display,1):
            raw_paused_display_cancel(&d->pause_display,1);
        return display!=1?display:d->pause_audio_status;
    }
    if(action!=1)return RAW_PAUSE_ERROR;
    d->pause_audio_status=raw_paused_audio_enter(&d->pause_audio,1);
    raw_runtime_hud(r,&h);diagnose_hud(d,r,&h);return raw_paused_display_update(&d->pause_display,&h,1);
}
int raw_device_startup(const RawDeviceHostOps *host){
    ChInstallOps install_ops;RawServiceOps service_ops;ChInstallPoint points[5];
    RawEnvironmentOps environment_ops;
    RawPauseOps pause_ops;
    RawPauseAudioOps audio_ops;
    uint32_t page,offset,probe,i;int status;
    static const uint32_t sites[5]={CH_RAW_PRE_SITE,CH_POST_SCAN_SITE,CH_MARKER_SITE,CH_SOURCE_SITE,CH_PRESENT_SITE};
    static const uint32_t words[5]={0xeb00332a,0xeb00182d,0xeb000a45,0xeb000ac7,0xeb001568};
    if(device.worker||device.install.alias_mapped)return 0;
    memset(&device,0,sizeof(device));if(host)device.host=*host;device.backend.startup_owned=1;
    device.startup_phase=1;
    device.binding.source_epoch=1;device.source_requested=0;
    device.source_diagnostic.version=5;
    device.source_diagnostic.result_enemy_first_index=UINT32_MAX;
    LightLock_Init(&device.job_lock);LightLock_Init(&device.hud_lock);LightLock_Init(&device.source_lock);
    raw_mailbox_init(&device.mailbox);ch_3ds_backend_ops(&device.backend,&install_ops,&device.binding.read);
    if(!raw_hud_3ds_sink_init(&device.screen,0,0)||!raw_present_prepare(&device.presentation,&device.binding.read))return 0;
    if(!raw_paused_3ds_init(&device.pause_backend,&device.presentation,&pause_ops)||
       !raw_paused_display_init(&device.pause_display,&pause_ops,0))return 0;
    device.pause_backend.clean_user=&device.screen;
    device.pause_backend.clean_overlay=raw_hud_3ds_restore_background;
    /* Pin audio code here without calling it or reading uninitialized runtime
       objects. Game callbacks become reachable only after pristine-title
       identity admission and the original five-site installation succeed. */
    if(!raw_paused_audio_3ds_init(&device.audio_backend,&device.binding.read,&audio_ops)||
       !raw_paused_audio_init(&device.pause_audio,&audio_ops))return 0;
    device.pause_audio_status=1;
    device.startup_phase=2;
    if(!raw_environment_backend_init(&device.environment_backend,&device.backend,&device.binding.read,&environment_ops)||
       !raw_environment_session_init(&device.environment,&environment_ops))return 0;
    device.startup_phase=3;
    if(!ch_3ds_hid_init(&device.backend))return 0;
    device.startup_phase=4;
    service_ops=(RawServiceOps){&device,sample,keys,hud,wait_poll,submit,receive,source,first,present,completed,paused};
    if(!ch_raw_bind(&service_ops)){ch_3ds_hid_exit(&device.backend);return 0;}
    device.worker=threadCreate(worker,&device,0x10000,0x3e,-2,false);
    if(!device.worker){ch_3ds_hid_exit(&device.backend);return 0;}
    device.startup_phase=5;
    page=(uint32_t)(uintptr_t)ch_raw_bridge&~4095u;
    offset=(uint32_t)(uintptr_t)ch_raw_bridge-page;
    probe=(uint32_t)(uintptr_t)ch_manual_alias_probe-page;
    for(i=0;i<5;i++)points[i]=(ChInstallPoint){sites[i],words[i],offset};
    status=ch_install_startup(&device.install,&install_ops,page,probe,points,5);
    device.install_status=status;device.startup_phase=6;
    if(status==CH_INSTALL_OK){
        device.startup_phase=7;
        if(!raw_present_activate(&device.presentation,device.install.installed_words[4])){
            device.install.poisoned=1;status=CH_INSTALL_POISONED;
        }else device.startup_phase=8;
    }
    ch_3ds_startup_finished(&device.backend);
    if(status==CH_INSTALL_OK)return 1;
    LightLock_Lock(&device.job_lock);device.stop=1;raw_mailbox_stop(&device.mailbox);LightLock_Unlock(&device.job_lock);
    threadJoin(device.worker,UINT64_MAX);threadFree(device.worker);device.worker=0;
    /* A poisoned partial hook installation cannot release game startup. The
       caller must report/abort its loader startup, never pretend rollback. */
    if(status==CH_INSTALL_POISONED)return -1;
    ch_3ds_hid_exit(&device.backend);return 0;
}
void raw_device_startup_diagnostic(RawDeviceStartupDiagnostic *out,int32_t status){
    uint32_t i;
    static const uint32_t sites[5]={CH_RAW_PRE_SITE,CH_POST_SCAN_SITE,CH_MARKER_SITE,CH_SOURCE_SITE,CH_PRESENT_SITE};
    static const uint32_t original[5]={0xeb00332a,0xeb00182d,0xeb000a45,0xeb000ac7,0xeb001568};
    if(!out)return;
    memset(out,0,sizeof(*out));out->magic=0x47444843u;out->version=1;
    out->phase=device.startup_phase;out->caller_status=(uint32_t)status;
    out->install_status=device.install_status;
    out->source_page=device.install.source_page;out->alias_page=device.install.alias_page;
    out->point_count=device.install.point_count;out->attempted_count=device.install.attempted_count;
    out->alias_mapped=device.install.alias_mapped;out->poisoned=device.install.poisoned;
    for(i=0;i<5;i++){
        out->sites[i]=sites[i];out->original_words[i]=original[i];
        out->installed_words[i]=device.install.installed_words[i];
        if(device.binding.read.read_bytes&&device.binding.read.read_bytes(device.binding.read.user,
            sites[i],&out->current_words[i],sizeof(out->current_words[i])))out->readable_bits|=1u<<i;
    }
}
int raw_device_hud_copy(RawHud *h){if(!h||!device.worker)return 0;
    LightLock_Lock(&device.hud_lock);*h=device.hud;LightLock_Unlock(&device.hud_lock);return 1;}
int raw_device_source_copy(RawSourceCapture *s){int have;if(!s||!device.worker)return 0;
    LightLock_Lock(&device.source_lock);have=(int)device.source_available;if(have)*s=device.capture;
    LightLock_Unlock(&device.source_lock);return have;}
int raw_device_environment_copy(RawEnvironmentState *s){if(!s||!device.worker)return 0;
    LightLock_Lock(&device.source_lock);*s=device.environment.state;LightLock_Unlock(&device.source_lock);return 1;}
int raw_device_result_copy(ChResultObservation *s){int have;if(!s||!device.worker)return 0;
    LightLock_Lock(&device.source_lock);have=(int)device.actual_result.complete;if(have)*s=device.actual_result;
    LightLock_Unlock(&device.source_lock);return have;
}
int raw_device_source_diagnostic_copy(RawDeviceSourceDiagnostic *out){if(!out||!device.worker)return 0;
    LightLock_Lock(&device.source_lock);*out=device.source_diagnostic;
    out->environment_status=device.environment.last_status;out->result_reason=device.result_gate.reason;
    LightLock_Unlock(&device.source_lock);return 1;
}
void raw_device_request_source(void){if(!device.worker)return;
    LightLock_Lock(&device.source_lock);
    if(!device.source_diagnostic.closed&&!device.source_available&&!device.environment.state.initial_applied&&
       !device.environment.state.rtc_owned&&!device.environment.state.poisoned&&
       device.environment.last_status!=RAW_ENV_STORE_FAILED&&device.environment.last_status!=RAW_ENV_POISONED)
        device.source_requested=1;
    LightLock_Unlock(&device.source_lock);
}
