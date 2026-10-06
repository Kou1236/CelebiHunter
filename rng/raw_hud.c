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
    row(h,white,"CelebiHunter v1.2.1");
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
        int off_target=r->fault==RAW_FAULT_ENVIRONMENT&&!r->display_error&&
            r->encounter_started&&r->encounter_candidate.status!=MANUAL_QUERY_OK&&
            r->actual_raw_press_seen&&r->candidate.abi==MANUAL_PREDICTION_ABI&&
            r->candidate.status==MANUAL_QUERY_OK&&r->actual_raw_press!=r->raw_target&&
            r->plan_failed&&r->input_plan.error==MP_INPUT_MISMATCH&&
            r->controls.fault==CH_FAULT_RUNTIME_FAILED;
        row(h,red,off_target?"A pressed outside target Advance":
            r->fault==RAW_FAULT_DISPLAY?"Display fault; no forecast":
            r->fault?"State check failed; no forecast":"Display not ready");
        row(h,white,"");
        row(h,white,r->runtime==CH_RUNTIME_PAUSED?"R or A: exit pause":"L+R: pause");
        row(h,white,"Start+Up: show/hide HUD");
        return;
    }
    controls(r,h,white,!r->encounter_started&&target&&distance==0u);
}
