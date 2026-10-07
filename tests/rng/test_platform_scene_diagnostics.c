#include "platform/scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROM_SIZE=0x2800,WRAM0_SIZE=0x8000,WRAM1_SIZE=0x1000,IO_SIZE=0x100 };
enum { ROM_BASE=0x10000000u,WRAM0_BASE=0x11000000u,WRAM1_BASE=0x11001000u,
       IO_BASE=0x12000000u,HRAM_BASE=0x12000080u };
typedef struct {
    uint8_t rom[ROM_SIZE],wram0[WRAM0_SIZE],wram1[WRAM1_SIZE],io[IO_SIZE],cpu[4];
    uint32_t fail_address,rom_pointer,wram0_pointer,wram1_pointer,hram_pointer,io_pointer;
} Fixture;
static uint32_t checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"failed line %u: %s\n",(unsigned)__LINE__,#v);exit(1);}}while(0)
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int copy_range(uint32_t address,uint32_t base,const uint8_t *bytes,uint32_t length,void *out,uint32_t size){
    if(address<base||address-base>length||size>length-(address-base))return 0;
    memcpy(out,bytes+(address-base),size);return 1;
}
static int read_bytes(void *user,uint32_t address,void *out,uint32_t size){Fixture *f=user;uint8_t word[4];
    if(address==f->fail_address)return 0;
    if(address==0x22f6c4u){put32(word,f->rom_pointer);if(size!=4u)return 0;memcpy(out,word,4);return 1;}
    if(address==0x22f6c8u){put32(word,f->wram0_pointer);if(size!=4u)return 0;memcpy(out,word,4);return 1;}
    if(address==0x22f768u){put32(word,f->wram1_pointer);if(size!=4u)return 0;memcpy(out,word,4);return 1;}
    if(address==0x22f6d8u){put32(word,f->hram_pointer);if(size!=4u)return 0;memcpy(out,word,4);return 1;}
    if(address==0x22f6dcu){put32(word,f->io_pointer);if(size!=4u)return 0;memcpy(out,word,4);return 1;}
    if(address==0x22f5fcu){if(size!=4u)return 0;memcpy(out,f->cpu,4);return 1;}
    return copy_range(address,ROM_BASE,f->rom,ROM_SIZE,out,size)||
        copy_range(address,WRAM1_BASE,f->wram1,WRAM1_SIZE,out,size)||
        copy_range(address,WRAM0_BASE,f->wram0,WRAM0_SIZE,out,size)||
        copy_range(address,IO_BASE,f->io,IO_SIZE,out,size);
}
static void fixture(Fixture *f){
    static const uint8_t joywait[]={0xcd,0x5a,0x04,0xcd,0x84,0x09,0xf0,0xa7,0xe6,0x03,0xc0,0xcd,0x6f,0x04,0x18,0xf0};
    static const uint8_t waitbutton[]={0xf0,0xd8,0xf5,0x3e,0x01,0xe0,0xd8,0xcd,0xf6,0x31,0xcd,0x36,0x0a,0xf1,0xe0,0xd8,0xc9};
    static const uint8_t getscript[]={0xe5,0xc5,0xf0,0x9d,0xf5,0xfa,0x39,0xd4,0xd7,0x21,0x3a,0xd4,0x4e,0x23,0x46,0x0a,0x03,0x70,0x2b,0x71,0x47,0xf1,0xd7,0x78,0xc1,0xe1,0xc9};
    memset(f,0,sizeof(*f));f->fail_address=UINT32_MAX;f->rom_pointer=ROM_BASE;
    f->wram0_pointer=WRAM0_BASE;f->wram1_pointer=WRAM1_BASE;f->io_pointer=IO_BASE;f->hram_pointer=HRAM_BASE;
    memcpy(f->rom+0xa36,joywait,sizeof(joywait));memcpy(f->rom+0xa36+sizeof(joywait),waitbutton,sizeof(waitbutton));
    memcpy(f->rom+0x26d4,getscript,sizeof(getscript));f->rom[0x1b9]=0x31;f->rom[0x1ba]=0xff;f->rom[0x1bb]=0xc0;
    f->io[0x70]=1;f->io[0]=0xff;
    f->wram1[0x437]=1;f->wram1[0x438]=0xff;f->wram1[0x439]=0x1b;
    f->wram1[0x43a]=0x54;f->wram1[0x43b]=0x6e;f->wram1[0xcb5]=3;f->wram1[0xcb6]=52;
    put16(f->cpu,0x0460);put16(f->cpu+2,0xc0d5);
    put16(f->wram0+0xd5,0x0a53);put16(f->wram0+0xd7,0x0a39);
}
static int detect(Fixture *f,ChFinalPrompt *prompt){ChReadOps read={f,read_bytes,0};return ch_scene_final_prompt(&read,prompt);}
int main(void){Fixture f;ChFinalPrompt p;
    fixture(&f);CHECK(detect(&f,&p));CHECK(p.match&&p.rejection==CH_SCENE_OK&&p.rejection_detail==0u);
    fixture(&f);f.fail_address=0x22f6c8u;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_POINTER_READ&&p.rejection_detail==2u);
    fixture(&f);f.wram0_pointer=0;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_POINTER_INVALID&&p.rejection_detail==2u);
    fixture(&f);f.fail_address=ROM_BASE+0xa36u;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_ROM_READ&&p.rejection_detail==1u);
    fixture(&f);f.rom[0x26d4]^=1u;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_ROM_SIGNATURE&&p.rejection_detail==2u);
    fixture(&f);f.io[0x70]=2;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_BANK_UNSUPPORTED&&p.rom_bank==2u&&p.rejection_detail==2u);
    fixture(&f);f.wram1[0x43a]^=1u;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_SCRIPT_STATE&&p.rejection_detail==3u);
    fixture(&f);put16(f.wram0+0xd5,0x1111);CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_STACK_PATH&&p.rejection_detail==1u&&!p.stack_wait_seen&&p.stack_joy_seen);
    fixture(&f);f.io[0xa3]=1;CHECK(!detect(&f,&p));
    CHECK(p.rejection==CH_SCENE_INPUT_HELD&&p.rejection_detail==1u&&p.joy_mirrors[1]==1u);
    printf("passed: %u scene rejection diagnostics\n",checks);return 0;
}
