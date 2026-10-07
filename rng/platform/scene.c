#include "scene.h"
#include <string.h>
static const uint8_t joywait[]={0xcd,0x5a,0x04,0xcd,0x84,0x09,0xf0,0xa7,0xe6,0x03,0xc0,0xcd,0x6f,0x04,0x18,0xf0};
static const uint8_t waitbutton[]={0xf0,0xd8,0xf5,0x3e,0x01,0xe0,0xd8,0xcd,0xf6,0x31,0xcd,0x36,0x0a,0xf1,0xe0,0xd8,0xc9};
static const uint8_t getscript[]={0xe5,0xc5,0xf0,0x9d,0xf5,0xfa,0x39,0xd4,0xd7,0x21,0x3a,0xd4,0x4e,0x23,0x46,0x0a,0x03,0x70,0x2b,0x71,0x47,0xf1,0xd7,0x78,0xc1,0xe1,0xc9};
static int read(const ChReadOps *o,uint32_t a,void *out,uint32_t n) {
    return a&&n&&(uint64_t)a+n<=UINT64_C(0x100000000)&&o->read_bytes(o->user,a,out,n);
}
static int pointer(const ChReadOps *o,uint32_t a,uint32_t *v) {
    uint8_t b[4];if(!read(o,a,b,4u))return 0;
    *v=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
    return *v!=0u&&(*v&3u)==0u?1:-1;
}
static int required_pointer(const ChReadOps *o,uint32_t address,uint32_t *value,
    ChFinalPrompt *s,uint32_t detail){
    int status=pointer(o,address,value);if(status==1)return 1;
    s->rejection=status==0?CH_SCENE_POINTER_READ:CH_SCENE_POINTER_INVALID;
    s->rejection_detail=detail;return 0;
}
int ch_scene_final_prompt(const ChReadOps *o,ChFinalPrompt *s) {
    uint8_t code[33],script[5],map[2],cpu[4],stack[254],bank=0,init[3];
    uint32_t n,i,has_wait=0u,has_joy=0u;
    if(!s)return 0;
    memset(s,0,sizeof(*s));
    if(!o||!o->read_bytes){s->rejection=CH_SCENE_POINTER_READ;return 0;}
    if(!required_pointer(o,0x22f6c4u,&s->rom_pointer,s,1u)||
       !required_pointer(o,0x22f6c8u,&s->wram0_pointer,s,2u)||
       !required_pointer(o,0x22f768u,&s->wram1_pointer,s,3u)||
       !required_pointer(o,0x22f6d8u,&s->hram_pointer,s,4u)||
       !required_pointer(o,0x22f6dcu,&s->io_pointer,s,5u))return 0;
    if(s->wram0_pointer>UINT32_MAX-32768u||s->wram1_pointer>UINT32_MAX-4096u||
        s->rom_pointer>UINT32_MAX-0x26efu||s->hram_pointer>UINT32_MAX-128u||
        s->io_pointer>UINT32_MAX-256u){s->rejection=CH_SCENE_POINTER_LAYOUT;s->rejection_detail=1u;return 0;}
    if(s->wram1_pointer!=s->wram0_pointer+4096u){
        s->rejection=CH_SCENE_POINTER_LAYOUT;s->rejection_detail=2u;return 0;}
    if(s->hram_pointer!=s->io_pointer+128u){
        s->rejection=CH_SCENE_POINTER_LAYOUT;s->rejection_detail=3u;return 0;}
    if(!read(o,s->rom_pointer+0xa36u,code,33u)){
        s->rejection=CH_SCENE_ROM_READ;s->rejection_detail=1u;return 0;}
    if(memcmp(code,joywait,16u)||memcmp(code+16u,waitbutton,17u)){
        s->rejection=CH_SCENE_ROM_SIGNATURE;s->rejection_detail=1u;return 0;}
    if(!read(o,s->rom_pointer+0x26d4u,code,27u)){
        s->rejection=CH_SCENE_ROM_READ;s->rejection_detail=2u;return 0;}
    if(memcmp(code,getscript,27u)){
        s->rejection=CH_SCENE_ROM_SIGNATURE;s->rejection_detail=2u;return 0;}
    if(!read(o,s->rom_pointer+0x1b9u,init,3u)){
        s->rejection=CH_SCENE_ROM_READ;s->rejection_detail=3u;return 0;}
    if(init[0]!=0x31u||init[1]!=0xffu||init[2]!=0xc0u){
        s->rejection=CH_SCENE_ROM_SIGNATURE;s->rejection_detail=3u;return 0;}
    if(!read(o,s->io_pointer+0x70u,&bank,1u)){s->rejection=CH_SCENE_BANK_READ;return 0;}
    s->rom_bank=bank;
    if((bank&7u)>1u){s->rejection=CH_SCENE_BANK_UNSUPPORTED;s->rejection_detail=bank;return 0;}
    if(!read(o,s->wram1_pointer+0x437u,script,5u)){
        s->rejection=CH_SCENE_STATE_READ;s->rejection_detail=1u;return 0;}
    if(!read(o,s->wram1_pointer+0xcb5u,map,2u)){
        s->rejection=CH_SCENE_STATE_READ;s->rejection_detail=2u;return 0;}
    if(!read(o,0x22f5fcu,cpu,4u)){
        s->rejection=CH_SCENE_STATE_READ;s->rejection_detail=3u;return 0;}
    s->script_mode=script[0];s->script_running=script[1];s->script_bank=script[2];
    s->script_next=(uint16_t)((uint16_t)script[3]|(uint16_t)script[4]<<8);
    s->map_group=map[0];s->map_number=map[1];
    s->guest_pc=(uint16_t)((uint16_t)cpu[0]|(uint16_t)cpu[1]<<8);
    s->guest_sp=(uint16_t)((uint16_t)cpu[2]|(uint16_t)cpu[3]<<8);
    if(s->script_mode!=1u){s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=1u;return 0;}
    if(s->script_bank!=0x1bu){s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=2u;return 0;}
    if(s->script_next!=0x6e54u){s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=3u;return 0;}
    if(s->map_group!=3u){s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=4u;return 0;}
    if(s->map_number!=52u){s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=5u;return 0;}
    if(s->guest_sp<0xc001u||s->guest_sp>0xc0fdu||!(s->guest_sp&1u)){
        s->rejection=CH_SCENE_SCRIPT_STATE;s->rejection_detail=6u;return 0;}
    n=0xc0ffu-s->guest_sp;
    if(!read(o,s->wram0_pointer+s->guest_sp-0xc000u,stack,n)){s->rejection=CH_SCENE_STACK_READ;return 0;}
    for(i=0;i<n;i+=2u) {
        uint16_t v=(uint16_t)((uint16_t)stack[i]|(uint16_t)stack[i+1u]<<8);
        if(v==0x0a53u)has_wait=1u;
        if(v==0x0a39u||v==0x0a3cu||v==0x0a44u)has_joy=1u;
    }
    s->stack_wait_seen=(uint8_t)has_wait;s->stack_joy_seen=(uint8_t)has_joy;
    if(!has_wait){s->rejection=CH_SCENE_STACK_PATH;s->rejection_detail=1u;return 0;}
    if(!((s->guest_pc>=0xa36u&&s->guest_pc<0xa46u)||has_joy)){
        s->rejection=CH_SCENE_STACK_PATH;s->rejection_detail=2u;return 0;}
    if(!read(o,s->hram_pointer+0x22u,s->joy_mirrors,8u)){
        s->rejection=CH_SCENE_STATE_READ;s->rejection_detail=4u;return 0;}
    if(!read(o,s->io_pointer,&s->joyp,1u)){
        s->rejection=CH_SCENE_STATE_READ;s->rejection_detail=5u;return 0;}
    s->complete=1u;
    if((s->joy_mirrors[1]|s->joy_mirrors[2]|s->joy_mirrors[5]|s->joy_mirrors[6])&3u){
        s->rejection=CH_SCENE_INPUT_HELD;s->rejection_detail=1u;return 0;}
    if(!(s->joyp&0x20u)&&(s->joyp&3u)!=3u){
        s->rejection=CH_SCENE_INPUT_HELD;s->rejection_detail=2u;return 0;}
    s->match=1u;return 1;
}
