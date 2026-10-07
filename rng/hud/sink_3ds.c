#include "sink_3ds.h"
#include <string.h>
extern uint32_t svcConvertVAToPA(const void *,bool);

/* Pinned Luma maps PA|bit31 as shared, strongly ordered, user RW memory.
 * Only the canonical, current VRAM surface may use that mapping. Logical
 * QueryMemory permissions stay READ for Crystal's static VRAM mapping. */
static int vram_surface(uint8_t *p,size_t bytes,const MemInfo *m,uint8_t **draw){
    uint32_t a=(uint32_t)(uintptr_t)p,last,pa,alias;
    uint64_t end=(uint64_t)a+bytes;
    if(m->state!=MEMSTATE_STATIC||m->perm!=MEMPERM_READ||
        m->base_addr!=OS_VRAM_VADDR||m->size!=OS_VRAM_SIZE||
        a<OS_VRAM_VADDR||end>(uint64_t)OS_VRAM_VADDR+OS_VRAM_SIZE)return 0;
    last=(uint32_t)(end-1u);pa=OS_VRAM_PADDR+(a-OS_VRAM_VADDR);alias=pa|0x80000000u;
    if(svcConvertVAToPA(p,false)!=pa||
        svcConvertVAToPA((const void *)(uintptr_t)last,false)!=OS_VRAM_PADDR+(last-OS_VRAM_VADDR)||
        svcConvertVAToPA((const void *)(uintptr_t)alias,true)!=pa||
        svcConvertVAToPA((const void *)(uintptr_t)(alias+bytes-1u),true)!=pa+bytes-1u)return 0;
    *draw=(uint8_t *)(uintptr_t)alias;return 1;
}

int raw_hud_3ds_sink_init(RawHud3dsSink *s,uint32_t screen_id,const RawHudLayout *layout){
    RawHudLayout l={8,10,RAW_HUD_COLUMNS};
    if(!s||screen_id>1)return 0;
    if(layout)l=*layout;
    if(l.columns<1||l.columns>RAW_HUD_COLUMNS||l.x< -1024||l.x>1024||l.y< -1024||l.y>1024)return 0;
    memset(s,0,sizeof(*s));LightLock_Init(&s->lock);LightLock_Init(&s->background_lock);
    s->screen_id=screen_id;s->layout=l;s->initialized=1;return 1;
}
void raw_hud_3ds_publish(void *u,const RawHud *h){RawHud3dsSink *s=u;
    if(!s||!s->initialized||!h)return;
    LightLock_Lock(&s->lock);s->published=*h;LightLock_Unlock(&s->lock);
}
static int original_present(RawHud3dsSink *s,uint32_t screen_id,uint32_t swap,
    uint8_t *fb_a,uint8_t *fb_b,uint32_t stride,uint32_t format,RawHud *h){size_t span;
    uint32_t width=screen_id==0?400u:320u;
    if(!s||!s->initialized||screen_id>1)return RAW_HUD_SINK_ERROR;
    if(screen_id!=s->screen_id)return RAW_HUD_SINK_SKIPPED;
    /* Publication only copies a small value. Wait for that snapshot rather
       than dropping the HUD from a fresh game frame during a target update.
       Release it before taking the independent pixel-receipt lock. */
    if(h){LightLock_Lock(&s->lock);*h=s->published;LightLock_Unlock(&s->lock);}
    LightLock_Lock(&s->background_lock);
    /* The pixel-receipt lock excludes pause-clone restoration. Invalidate
       first, including on malformed calls, so no older glyph
       receipt can be mistaken for this new game image. */
    s->background_valid=0;s->background_unpainted=0;s->presentation_sequence++;
    if(swap>1||!fb_a||!raw_hud_surface_span(width,240,stride,format,&span)||
       span>UINT32_MAX-(uint32_t)(uintptr_t)fb_a||
       (screen_id==0&&fb_b&&span>UINT32_MAX-(uint32_t)(uintptr_t)fb_b)){
        LightLock_Unlock(&s->background_lock);return RAW_HUD_SINK_ERROR;
    }
    if(screen_id==0){
        s->background_a=(uint32_t)(uintptr_t)fb_a;s->background_b=(uint32_t)(uintptr_t)fb_b;
        s->background_stride=stride;s->background_format=format;
        s->background_sequence=s->presentation_sequence;s->background_saved_pixels=0;
        s->background_unpainted=1;s->background_valid=1;
    }
    LightLock_Unlock(&s->background_lock);
    return 1;
}
int raw_hud_3ds_original_unpainted_present(RawHud3dsSink *s,uint32_t screen_id,uint32_t swap,
    uint8_t *fb_a,uint8_t *fb_b,uint32_t stride,uint32_t format){
    if(screen_id!=0)return RAW_HUD_SINK_SKIPPED;
    return original_present(s,screen_id,swap,fb_a,fb_b,stride,format,0);
}
static int writable(uint8_t *p,size_t bytes,uint8_t **draw,int *uncached){
    uint64_t end=(uint64_t)(uintptr_t)p+bytes,cursor=(uintptr_t)p;
    if(!p||!bytes||end>0x100000000ull)return 0;
    while(cursor<end){MemInfo m;PageInfo page;uint64_t next;
        if(R_FAILED(svcQueryMemory(&m,&page,(uint32_t)cursor))||m.state==MEMSTATE_FREE)return 0;
        next=(uint64_t)m.base_addr+m.size;
        if(cursor<m.base_addr||next<=cursor)return 0;
        if((m.perm&(MEMPERM_READ|MEMPERM_WRITE))!=(MEMPERM_READ|MEMPERM_WRITE)){
            if(cursor!=(uintptr_t)p||!vram_surface(p,bytes,&m,draw))return 0;
            *uncached=1;return 1;
        }
        cursor=next;
    }
    *draw=p;*uncached=0;return 1;
}
int raw_hud_3ds_restore_background(void *user,uint32_t a,uint32_t b,uint32_t stride,uint32_t format,
    uint8_t *clone,uint32_t bytes){RawHud3dsSink *s=user;size_t span;uint32_t x,y,index,bpp;int ok=-1;
    if(!s||!s->initialized||!clone||!raw_hud_surface_span(400,240,stride,format,&span)||span!=bytes)return 0;
    LightLock_Lock(&s->background_lock);
    if(s->background_valid&&s->background_sequence==s->presentation_sequence&&s->background_a==a&&s->background_b==b&&
       s->background_stride==stride&&s->background_format==format){
        if(!s->background_unpainted){
            bpp=s->background_bpp;
            for(x=0;x<s->background_width;x++)for(y=0;y<s->background_height;y++){
                index=x*s->background_height+y;
                if(s->background_mask[index>>3]&(1u<<(index&7)))
                    memcpy(clone+(size_t)(s->background_x+x)*stride+
                        (239u-s->background_y-y)*bpp,s->background+index*bpp,bpp);
            }
        }
        ok=1;
    }
    LightLock_Unlock(&s->background_lock);return ok;
}
int raw_hud_3ds_present(RawHud3dsSink *s,uint32_t screen_id,uint32_t swap,
    uint8_t *fb_a,uint8_t *fb_b,uint32_t stride,uint32_t format){
    RawHud h;RawHudSurface surfaces[2];RawHudPaint paint[2];size_t span;
    uint32_t n=1,i,width=screen_id==0?400u:320u;int result,uncached[2];
    result=original_present(s,screen_id,swap,fb_a,fb_b,stride,format,&h);
    if(result!=1)return result;
    if(!h.visible||!h.count)return RAW_HUD_SINK_SKIPPED;
    if(!raw_hud_surface_span(width,240,stride,format,&span))return RAW_HUD_SINK_ERROR;
    if(screen_id==0&&fb_b&&fb_b!=fb_a)n=2;
    surfaces[0]=(RawHudSurface){fb_a,span,width,240,stride,format};
    surfaces[1]=(RawHudSurface){fb_b,span,width,240,stride,format};
    /* Reject invalid/overlapping stereo surfaces before writing either one. */
    if(n==2){uint64_t a=(uintptr_t)fb_a,b=(uintptr_t)fb_b;
        if(a<b+span&&b<a+span)return RAW_HUD_SINK_ERROR;}
    for(i=0;i<n;i++)if(!writable(surfaces[i].pixels,span,&surfaces[i].pixels,&uncached[i]))return RAW_HUD_SINK_ERROR;
    /* Reject overlapping normalized drawing destinations as well as input VA overlap. */
    if(n==2){uint64_t a=(uintptr_t)surfaces[0].pixels,b=(uintptr_t)surfaces[1].pixels;
        if(a<b+span&&b<a+span)return RAW_HUD_SINK_ERROR;}
    for(i=0;i<n;i++){
        if(i==0&&screen_id==0){RawHudCapture capture={s->background,s->background_mask,
            sizeof(s->background),sizeof(s->background_mask),0,0,0,0,0};
            LightLock_Lock(&s->background_lock);
            result=raw_hud_paint_capture(&h,&surfaces[i],&s->layout,&paint[i],&capture);
            if(result==RAW_HUD_PAINTED){
                s->background_a=(uint32_t)(uintptr_t)fb_a;s->background_b=(uint32_t)(uintptr_t)fb_b;
                s->background_stride=stride;s->background_format=format;
                s->background_x=capture.x;s->background_y=capture.y;
                s->background_width=capture.width;s->background_height=capture.height;
                s->background_bpp=(format&15u)==1u?3u:2u;
                s->background_saved_pixels=capture.saved_pixels;
                s->background_sequence=s->presentation_sequence;s->background_unpainted=0;s->background_valid=1;
            }
            LightLock_Unlock(&s->background_lock);
        }else result=raw_hud_paint_transparent(&h,&surfaces[i],&s->layout,&paint[i]);
        if(result==RAW_HUD_INVALID)return RAW_HUD_SINK_ERROR;
        if(result==RAW_HUD_PAINTED&&uncached[i])__dsb();
        if(result==RAW_HUD_PAINTED&&!uncached[i]&&R_FAILED(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
            (uint32_t)(uintptr_t)(surfaces[i].pixels+paint[i].first_byte),
            (uint32_t)(paint[i].end_byte-paint[i].first_byte))))return RAW_HUD_SINK_ERROR;
    }
    return result==RAW_HUD_PAINTED?RAW_HUD_SINK_PAINTED:RAW_HUD_SINK_SKIPPED;
}
