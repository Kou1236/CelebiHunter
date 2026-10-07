#include "raw_service.h"
#include "query_completion.h"
#include <string.h>
static char *text(char *p,const char *s) {while(*s)*p++=*s++;*p=0;return p;}
static char *number(char *p,uint32_t v) {
    char b[10];uint32_t n=0;do{b[n++]=(char)('0'+v%10);}while(v/=10);
    while(n)*p++=b[--n];
    *p=0;return p;
}
static char *hex(char *p,uint32_t v) {const char *h="0123456789ABCDEF";uint32_t i;
    for(i=0;i<4;i++)*p++=h[(v>>(12-4*i))&15];
    *p=0;return p;
}
static char *row(RawHud *h,uint32_t color,const char *s) {
    uint32_t i=h->count++;h->color[i]=color;return text(h->line[i],s);
}
/* Rxx and Qxx are stable report codes; reserve old values when adding causes. */
static const char *fault_text(const RawRuntime *r) {
    if(!r)return "Runtime check failed [R00]";
    switch(r->fault) {
    case RAW_FAULT_READ:return "Game state read failed [R01]";
    case RAW_FAULT_UNIT:return "Unsupported scheduler state [R02]";
    case RAW_FAULT_FILTER:return r->plan_failed&&r->input_plan.error==MP_COUNTER_MISMATCH?
        "Input scan counter mismatch [R16]":"Input scan did not match [R03]";
    case RAW_FAULT_ORDER:
        switch(r->controls.fault) {
        case CH_FAULT_BAD_ACK:return "Pause/step acknowledgement failed [R05]";
        case CH_FAULT_STEP_OVERSHOT:return "Step passed target Advance [R06]";
        case CH_FAULT_SCENE_CHANGED_DURING_COMMAND:return "Scene changed during control [R07]";
        case CH_FAULT_COUNTER_DISCONTINUITY:return "Advance counter discontinuity [R08]";
        default:return "Unexpected game update [R04]";
        }
    case RAW_FAULT_COMMAND:return "Pause/step request rejected [R09]";
    case RAW_FAULT_ENVIRONMENT:
        /* The environment callback failed. A prior input-plan cancellation
           cannot supply the cause of a source/read/write/restore failure. */
        return "Source check failed [R10]";
    case RAW_FAULT_DISPLAY:return r->display_pending?
        "Display restore pending [R21]":"Display fault [R20]";
    default:return r->display_error?"Display not ready [R22]":"Runtime check failed [R00]";
    }
}
static const char *query_text(const RawRuntime *r) {
    if(!r)return "Search failed [Q00]";
    switch(r->query_status) {
    case MANUAL_QUERY_NO_FUTURE_CANDIDATE:return "No shiny in search window [Q11]";
    case MANUAL_QUERY_OUTSIDE_DOMAIN:
        return r->query_failure_detail==RAW_QUERY_DETAIL_WINDOW_EXPIRED?
            "Source window expired; refreshing [Q07]":"Search range outside model [Q10]";
    case MANUAL_QUERY_UNSUPPORTED_CERTIFICATE:return "Timing profile unsupported [Q09]";
    case MANUAL_QUERY_STALE_SOURCE:return "Live source changed [Q06]";
    case MANUAL_QUERY_SOLVER_BOUND:return "Search limit reached [Q08]";
    case MANUAL_QUERY_INVALID:
        switch(r->query_failure_detail) {
        case RAW_QUERY_DETAIL_JOB_BINDING:return "Copied search input failed validation [Q01]";
        case RAW_QUERY_DETAIL_RESULT_BINDING:return "Search result no longer matches live state [Q02]";
        case RAW_QUERY_DETAIL_PLAN_ADMISSION:return "Player input plan could not be tracked [Q03]";
        case RAW_QUERY_DETAIL_SUBMISSION:return "Search job could not be queued [Q04]";
        case RAW_QUERY_DETAIL_WORKER_STATUS:return "Search worker rejected its input [Q05]";
        default:return "Search input invalid [Q00]";
        }
    default:return "Search failed [Q00]";
    }
}
static void controls(const RawRuntime *r,RawHud *h,uint32_t color,int ready){
    row(h,color,"");
    row(h,color,r->runtime==CH_RUNTIME_PAUSED?
        (ready?"A starts | L step | R run":"L step | R run"):
        r->runtime==CH_RUNTIME_STEPPING?"Advancing one step":"L+R: pause");
    row(h,color,"Start+Up: show/hide HUD");
}
void raw_runtime_hud(const RawRuntime *r,RawHud *h) {
    const uint32_t white=0xffffff,yellow=0xffdd70,red=0xff9090;
    uint32_t distance=r->raw_target-r->counter;
    int target=r->controls.candidate_valid&&r->counter_valid&&distance<0x80000000u;
    int forecast=r->encounter_started?r->encounter_candidate.status==MANUAL_QUERY_OK:target;
    char *p;
    memset(h,0,sizeof(*h));h->visible=r->controls.overlay_visible;
    row(h,white,"CelebiHunter v1.3.0");
    p=row(h,white,"Advance ");
    if(r->counter_valid)p=number(p,r->counter);else p=text(p,"--");
    p=text(p," | Target ");
    if(r->encounter_started&&forecast)number(p,r->raw_target);
    else if(target)number(p,r->raw_target);else text(p,"--");
    p=row(h,yellow,"Predicted DV ");
    if(forecast)p=hex(p,r->encounter_started?r->encounter_candidate.predicted_dv:r->candidate.predicted_dv);
    else p=text(p,"--");
    p=text(p," | Actual DV ");
    if(r->actual_seen)hex(p,r->actual_dv);else text(p,"--");
    if(r->fault||r->display_error){
        row(h,red,fault_text(r));
        row(h,white,"");
        row(h,white,r->runtime==CH_RUNTIME_PAUSED?"R or A: exit pause":"L+R: pause");
        row(h,white,"Start+Up: show/hide HUD");
        return;
    }
    if(r->encounter_started&&!forecast)
        row(h,yellow,r->encounter_forecast_reason==RAW_ENCOUNTER_FORECAST_OFF_TARGET?
            "A pressed outside target Advance [R11]":"Encounter started without a forecast [R23]");
    else if(!forecast&&!target&&r->query_status!=MANUAL_QUERY_OK)
        row(h,yellow,query_text(r));
    controls(r,h,white,!r->encounter_started&&target&&distance==0u);
}
