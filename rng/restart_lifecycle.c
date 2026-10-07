#include "restart_lifecycle.h"
#include <string.h>
static int word(const ChReadOps *o,uint32_t address,uint32_t *out){uint8_t b[4];
    if(!o->read_bytes(o->user,address,b,4u))return 0;
    *out=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;return 1;
}
int raw_frontend_read(const ChReadOps *o,RawFrontendState *out){
    return o&&o->read_bytes&&out&&
        o->read_bytes(o->user,RAW_FRONTEND_MODE_ADDRESS,&out->mode,1u)&&
        o->read_bytes(o->user,RAW_FRONTEND_SCAN_GATE_ADDRESS,&out->scan_gate,1u);
}
typedef struct {uint32_t engine,reset_words[4],reset_flag,caller_lr,menu_state,
    caller_r4,gui_callback;RawFrontendState frontend;} Receipt;
static int once(const ChReadOps *o,const ChNativeContext *c,Receipt *r){uint32_t i;
    memset(r,0,sizeof(*r));
    if(!word(o,CH_ENGINE_POINTER_ADDRESS,&r->engine)||r->engine!=0x0027be7cu||
       !raw_frontend_read(o,&r->frontend)||r->frontend.mode>=13u)return 0;
    if(c->lr==RAW_MENU_RESET_RECEIPT_LR){
        if(!c->original_sp||(c->original_sp&3u)||c->original_sp>UINT32_MAX-7u||
           !word(o,c->original_sp+4u,&r->caller_lr))return 0;
        if(r->caller_lr==0x0010ebacu){
            if(!word(o,RAW_FRONTEND_MENU_STATE_ADDRESS,&r->menu_state)||r->menu_state!=21u)return 0;
        }else if(r->caller_lr==RAW_GUI_RESET_CALLER_LR){
            if(!word(o,c->original_sp,&r->caller_r4)||r->caller_r4!=RAW_GUI_RESET_OWNER_ADDRESS||
               !word(o,RAW_GUI_RESET_CALLBACK_ADDRESS,&r->gui_callback)||r->gui_callback!=0x0018588cu)return 0;
        }else return 0;
    }else if(r->frontend.mode==1u&&r->frontend.scan_gate!=2u)return 0;
    for(i=0;i<4u;i++)if(!word(o,r->engine+0x140u+4u*i,&r->reset_words[i])||r->reset_words[i])return 0;
    return word(o,r->engine+0x170u,&r->reset_flag)&&!r->reset_flag;
}
static int gui_code(const ChReadOps *o){uint32_t i,v;
    static const uint32_t code[][2]={
        {0x10347cu,0xe59f10b8u},{0x103480u,0xe59f0048u},{0x103484u,0xeb00224fu},
        {0x1034d0u,RAW_GUI_RESET_OWNER_ADDRESS},{0x10353cu,0x0018588cu},
        {0x10bdc8u,0xe2800b05u},{0x10bdccu,0xe2800f66u},{0x10bdd0u,0xe5801000u},
        {0x144238u,0xe92d4010u},{0x14423cu,0xe1a04000u},{0x144240u,0xe2800b05u},
        {0x144244u,0xe2800f66u},{0x144248u,0xe5900000u},{0x144254u,0xe12fff30u}
    };
    for(i=0;i<sizeof(code)/sizeof(code[0]);i++)
        if(!word(o,code[i][0],&v)||v!=code[i][1])return 0;
    return 1;
}
static int receipt(const ChReadOps *o,const ChNativeContext *c,Receipt *out){Receipt a,b;uint32_t v;
    if(!o||!o->read_bytes||!c)return 0;
    /* Startup pins the whole pristine native image. Check the unmodified
       confirmed call and counter store again beside this installed hook. */
    if(c->lr==RAW_MENU_RESET_RECEIPT_LR){
        /* 18588C pushes {r4,lr}; no stack change survives at 1858E4.
           The touchscreen callback is indirect through GUI+1598, so its
           saved LR differs from the internal debug menu's direct BL. */
        if(!word(o,0x0018588cu,&v)||v!=0xe92d4010u||
           !word(o,0x001858b4u,&v)||v!=0xe5801140u||
           !word(o,0x00185900u,&v)||v!=0x0027be7cu)return 0;
    }else if(c->lr==RAW_RESTART_RECEIPT_LR){
        if(!word(o,0x0010ed48u,&v)||v!=0xebfffb42u||
           !word(o,0x0010da78u,&v)||v!=0xe5801140u||
           !word(o,0x0010daa4u,&v)||v!=0x0027be7cu)return 0;
    }else return 0;
    if(!once(o,c,&a))return 0;
    if(c->lr==RAW_MENU_RESET_RECEIPT_LR){
        if(a.caller_lr==RAW_GUI_RESET_CALLER_LR){if(!gui_code(o))return 0;}
        else if(!word(o,0x0010eba8u,&v)||v!=0xeb01db37u)return 0;
    }
    if(!once(o,c,&b)||memcmp(&a,&b,sizeof(a)))return 0;
    if(out)*out=a;
    return 1;
}
int raw_restart_receipt(const ChReadOps *o,const ChNativeContext *c){return receipt(o,c,0);}
int raw_restart_notice_capture(const ChReadOps *o,const ChNativeContext *c,uint64_t epoch,RawRestartNotice *out){
    Receipt r;
    if(!out||!epoch||epoch==UINT64_MAX||!c||c->lr!=RAW_MENU_RESET_RECEIPT_LR||!receipt(o,c,&r))return 0;
    *out=(RawRestartNotice){epoch,epoch+1u,r.caller_lr,1u};return 1;
}
