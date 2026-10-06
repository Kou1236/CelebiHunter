#include <3ds.h>
#include <string.h>
#include "paused_audio_3ds.h"
#include "pinned_audio_contract.h"
extern void raw_audio_native_master_gain_bits(uint32_t);
enum {MASTER=0x63946c,MIX=0x63dd68,DSP=0x6394bc,
      ACTIVE=0x231f55,LOCK=0x231f64,ARBITER=0x2323c8};
typedef struct {uint32_t master[2],mix[3],active,lock,arbiter,config;uint16_t bank;} AudioSnapshot;
static int failed(RawAudio3ds *b,uint32_t code){b->error=code;return -1;}
static int read_bytes(RawAudio3ds *b,uint32_t a,void *out,uint32_t n){
    return b->read.read_bytes(b->read.user,a,out,n);
}
static int rw(uint32_t a,uint32_t n){MemInfo m;PageInfo p;
    return a&&n&&a<=UINT32_MAX-n&&R_SUCCEEDED(svcQueryMemory(&m,&p,a))&&
        m.state!=MEMSTATE_FREE&&(m.perm&MEMPERM_READWRITE)==MEMPERM_READWRITE&&
        m.base_addr<=a&&m.size>=n&&a-m.base_addr<=m.size-n;
}
static int contract(RawAudio3ds *b){uint32_t i,words[28];
    for(i=0;i<RAW_AUDIO_PIN_COUNT;i++){
        const RawAudioCodePin *p=&raw_audio_code_pins[i];
        if(p->count>28||!read_bytes(b,p->address,words,p->count*4)||
           memcmp(words,p->words,p->count*4))return failed(b,1);
    }
    b->contract_checked=1;return 1;
}
static int snapshot(RawAudio3ds *b,AudioSnapshot *s){
    memset(s,0,sizeof(*s));
    if(!read_bytes(b,MASTER,s->master,8)||!read_bytes(b,MIX,s->mix,12)||
       !read_bytes(b,ACTIVE,&s->active,1)||!read_bytes(b,LOCK,&s->lock,4)||
       !read_bytes(b,ARBITER,&s->arbiter,4)||!read_bytes(b,DSP+0x1322,&s->bank,2))return failed(b,2);
    /* Native code uses these nonzero initialized flags. Accept the observed
       initialized1 only, finite gains, and a valid aligned configuration. */
    if((s->master[0]&255)!=1||(s->mix[0]&255)!=1||s->active!=1||
       (s->master[1]&0x7f800000u)==0x7f800000u||
       (s->mix[2]&0x7f800000u)==0x7f800000u||!s->arbiter||s->bank>1)return failed(b,3);
    if(!read_bytes(b,DSP+0x10a4+4u*s->bank,&s->config,4)||
       (s->config&3)||!rw(s->config,8)||!rw(MASTER,8)||!rw(MIX,12)||
       !rw(LOCK,4))return failed(b,4);
    if((int32_t)s->lock<=0||s->lock==0x7fffffffu){b->busy++;return 0;}
    return 1;
}
static int observe(RawAudio3ds *b,AudioSnapshot *s){AudioSnapshot t;int r;
    b->stage=2;
    if(contract(b)!=1)return -1;
    r=snapshot(b,s);if(r!=1)return r;
    r=snapshot(b,&t);if(r!=1)return r;
    /* Lock reader-count changes are ordinary contention, so retry. */
    if(memcmp(s,&t,sizeof(t))){b->busy++;return 0;}
    return 1;
}
static int gain(void *u,uint32_t *out){RawAudio3ds *b=u;AudioSnapshot s;int r;
    if(!b||!b->initialized||!out)return -1;
    r=observe(b,&s);if(r==1)*out=s.master[1];return r;
}
static int set_gain(void *u,uint32_t bits){RawAudio3ds *b=u;AudioSnapshot s,t;int r;
    if(!b||!b->initialized||(bits&0x7f800000u)==0x7f800000u)return -1;
    r=observe(b,&s);if(r!=1)return r;
    b->stage=3;
    /* Calls the pinned API, using its own shared lock and wake protocol.
       A race after this positive-lock snapshot can enter its native wait.
       That wait has no proven bound; this wrapper invents no lock state. */
    raw_audio_native_master_gain_bits(bits);b->sets++;
    b->stage=4;r=snapshot(b,&t);
    if(r!=1||t.master[1]!=bits||t.mix[1]!=bits||t.active!=s.active)
        return failed(b,5);
    return 1;
}
int raw_paused_audio_3ds_init(RawAudio3ds *b,const ChReadOps *read,RawPauseAudioOps *ops){
    if(!b||!read||!read->read_bytes||!ops)return 0;
    memset(b,0,sizeof(*b));b->read=*read;b->initialized=1;b->stage=1;
    if(contract(b)!=1){b->initialized=0;return 0;}
    *ops=(RawPauseAudioOps){b,gain,set_gain};return 1;
}
