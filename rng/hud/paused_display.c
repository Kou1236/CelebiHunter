#include "paused_display.h"
#include <string.h>
static int failed(RawPausedDisplay *p,uint32_t stage,uint32_t code){
    p->diagnostic_stage=stage;p->diagnostic_error_stage=stage;
    p->diagnostic_error_code=code;p->diagnostic_errors++;return RAW_PAUSE_ERROR;
}
static int record_valid(const RawPauseRecord *r){size_t span;
    /* Exclude the special 0x20 top mode whose geometry was not proven here. */
    return r&&r->swap<=1&&r->display_select==r->swap&&r->fb_a&&!r->zero&&
        !(r->format&~UINT32_C(0x34f))&&!(r->format&0x20)&&
        raw_hud_surface_span(400,240,r->stride,r->format,&span)&&
        span<=UINT32_MAX-r->fb_a&&(!r->fb_b||span<=UINT32_MAX-r->fb_b);
}
static int snapshot(RawPausedDisplay *p,RawPauseSnapshot *s){int r;
    p->diagnostic_stage=RAW_PAUSE_DIAG_SNAPSHOT;
    memset(s,0,sizeof(*s));r=p->ops.snapshot(p->ops.user,s);
    if(r!=1)return r<0?failed(p,RAW_PAUSE_DIAG_SNAPSHOT,1):RAW_PAUSE_WAIT;
    if(!s->context||!s->descriptor||(s->descriptor&3)||(s->control&0xff)>1||
       ((s->control>>8)&0xff)>1||!s->shared_base||(s->shared_base&3)||
       s->shared_base>UINT32_MAX-0x400u||s->thread_id>3||
       s->event_queue!=s->shared_base+64u*s->thread_id||
       s->descriptor!=s->shared_base+0x200u+128u*s->thread_id||
       (s->queue_control&0xffu)>=52u||((s->queue_control>>8)&0xffu)>52u||
       ((s->queue_control>>16)&0xffu)>1u)return failed(p,RAW_PAUSE_DIAG_SNAPSHOT,2);
    if(s->graphics_busy||((s->control>>8)&0xff)||(s->queue_control&0x00ffff00u))return RAW_PAUSE_WAIT;
    return RAW_PAUSE_READY;
}
static int identity(const RawPausedDisplay *p,const RawPauseSnapshot *s){
    return s->context==p->original.context&&s->descriptor==p->original.descriptor;
}
static int entry_event(RawPausedDisplay *p,const RawPauseSnapshot *s){uint32_t delta;
    /* Pinned 126720 increments modulo INT32_MAX, not modulo UINT32_MAX.
       A forward distance below half that ring excludes a stale/backward epoch
       while admitting its actual INT32_MAX-1 -> 0 wrap. */
    if(s->top_epoch>=UINT32_C(0x7fffffff))return failed(p,RAW_PAUSE_DIAG_ENTRY_EVENT,1);
    if(s->queue_control&0x00ffff00u){p->entry_seen=0;p->entry_clear_seen=0;return RAW_PAUSE_WAIT;}
    if(!p->entry_seen||p->entry.context!=s->context||p->entry.descriptor!=s->descriptor||
       p->entry.control!=s->control||p->entry.event_queue!=s->event_queue||
       p->entry.shared_base!=s->shared_base||p->entry.thread_id!=s->thread_id||
       memcmp(&p->entry.record,&s->record,sizeof(s->record))){
        p->entry=*s;p->entry_seen=1;p->entry_clear_seen=0;p->entry_clear_epoch=0;return RAW_PAUSE_WAIT;
    }
    delta=s->top_epoch>=p->entry.top_epoch?s->top_epoch-p->entry.top_epoch:
        UINT32_C(0x7fffffff)-p->entry.top_epoch+s->top_epoch;
    if(!delta)return RAW_PAUSE_WAIT;
    if(delta>UINT32_C(0x3fffffff)){p->entry=*s;p->entry_clear_seen=0;return RAW_PAUSE_WAIT;}
    /* The single native relay worker dequeues before invoking its callback.
       Empty alone can therefore leave one stale callback in flight. First
       settle that possibility; only a second later top event admits copying. */
    if(!p->entry_clear_seen){p->entry_clear_seen=1;p->entry_clear_epoch=s->top_epoch;return RAW_PAUSE_WAIT;}
    delta=s->top_epoch>=p->entry_clear_epoch?s->top_epoch-p->entry_clear_epoch:
        UINT32_C(0x7fffffff)-p->entry_clear_epoch+s->top_epoch;
    if(!delta)return RAW_PAUSE_WAIT;
    if(delta>UINT32_C(0x3fffffff)){p->entry=*s;p->entry_clear_seen=0;return RAW_PAUSE_WAIT;}
    return RAW_PAUSE_READY;
}
static int ack(RawPausedDisplay *p,RawPauseSnapshot *s){int r=snapshot(p,s);
    if(r!=RAW_PAUSE_READY){p->ack_seen=0;p->ack_clear_epoch=0;return r;}
    if(!identity(p,s)||memcmp(&s->record,&p->submitted,sizeof(s->record))){
        p->ack_seen=0;p->ack_clear_epoch=0;return failed(p,RAW_PAUSE_DIAG_ACK,1);
    }
    /* First observe the original pending-clear + new-event discipline, then
     * retain both mappings through an additional top display event. A pending
     * clear alone is not claimed to prove instantaneous LCD scanout release.
     * No callback/event processing is performed here. */
    if(s->top_epoch==p->submitted_epoch)return RAW_PAUSE_WAIT;
    if(!p->ack_seen){p->ack_seen=1;p->ack_clear_epoch=s->top_epoch;return RAW_PAUSE_WAIT;}
    if(s->top_epoch==p->ack_clear_epoch)return RAW_PAUSE_WAIT;
    return RAW_PAUSE_READY;
}
static int cleanup(RawPausedDisplay *p){uint32_t i;int good=1;
    for(i=0;i<2;i++)if(p->buffers[i].pixels){
        if(p->ops.release(p->ops.user,&p->buffers[i])!=1){failed(p,RAW_PAUSE_DIAG_RELEASE,i+1);good=0;}
        else memset(&p->buffers[i],0,sizeof(p->buffers[i]));
    }
    if(!good)return RAW_PAUSE_ERROR;
    p->state=RAW_PAUSE_OFF;p->published=0;p->leaving=0;p->ack_seen=0;p->ack_clear_epoch=0;
    p->allocation_failed=0;
    p->entry_seen=0;p->entry_clear_seen=0;p->entry_clear_epoch=0;memset(&p->entry,0,sizeof(p->entry));
    p->step_present_seen=0;p->step_clear_seen=0;p->step_clear_epoch=0;
    memset(&p->original,0,sizeof(p->original));memset(&p->rendered,0,sizeof(p->rendered));
    return RAW_PAUSE_READY;
}
int raw_paused_display_init(RawPausedDisplay *p,const RawPauseOps *o,const RawHudLayout *l){
    if(!p||!o||!o->snapshot||!o->allocate||!o->release||!o->flush||!o->publish||!o->background)return 0;
    memset(p,0,sizeof(*p));p->ops=*o;p->layout=l?*l:(RawHudLayout){8,10,50};p->initialized=1;return 1;
}
static int allocate_buffer(RawPausedDisplay *p,uint32_t index){
    RawPauseBuffer *b=&p->buffers[index],*other=&p->buffers[1-index];int r;
    if(p->allocation_failed)return RAW_PAUSE_ERROR;
    if(b->pixels)return RAW_PAUSE_READY;
    p->diagnostic_stage=RAW_PAUSE_DIAG_ALLOC0+index;
    r=p->ops.allocate(p->ops.user,RAW_PAUSE_ALLOC_BYTES,b);
    if(r!=1||!b->pixels||b->bytes!=RAW_PAUSE_ALLOC_BYTES||
       !b->vaddr||!b->paddr||!b->publish_vaddr||(b->vaddr&4095)||
       (b->paddr&4095)||b->vaddr>UINT32_MAX-RAW_PAUSE_ALLOC_BYTES||
       (b->publish_vaddr&4095)||b->publish_vaddr>UINT32_MAX-RAW_PAUSE_ALLOC_BYTES||
       b->paddr<UINT32_C(0x20000000)||b->paddr>UINT32_C(0x28000000)-RAW_PAUSE_ALLOC_BYTES||
       b->publish_vaddr!=b->paddr-UINT32_C(0x0c000000)){
        p->allocation_failed=1;return failed(p,RAW_PAUSE_DIAG_ALLOC0+index,r!=1?1:2);
    }
    if(other->pixels&&((b->vaddr<other->vaddr+RAW_PAUSE_ALLOC_BYTES&&
        other->vaddr<b->vaddr+RAW_PAUSE_ALLOC_BYTES)||
        (b->paddr<other->paddr+RAW_PAUSE_ALLOC_BYTES&&
        other->paddr<b->paddr+RAW_PAUSE_ALLOC_BYTES))){
        p->allocation_failed=1;return failed(p,RAW_PAUSE_DIAG_OVERLAP,1);
    }
    return RAW_PAUSE_READY;
}
static int draw_publish(RawPausedDisplay *p,const RawHud *h,const RawPauseSnapshot *s,uint32_t index){
    RawPauseBuffer *b=&p->buffers[index];RawPauseRecord r;RawPauseSnapshot ready;
    RawHudSurface surface;RawHudPaint paint;uint32_t submitted_epoch;int result;
    /* Entry publishes one fully validated slot. The unused alternate is
       leased only after the existing display acknowledgement permits an
       actual HUD change or fresh player-step image to be drawn into it. */
    if(allocate_buffer(p,index)!=RAW_PAUSE_READY)return RAW_PAUSE_ERROR;
    if(!b->pixels||b->bytes<RAW_PAUSE_ALLOC_BYTES||!b->vaddr||!b->paddr||!b->publish_vaddr)
        return failed(p,RAW_PAUSE_DIAG_BUFFER,index+1);
    p->prepared_attempts++;
    p->diagnostic_stage=RAW_PAUSE_DIAG_PAINT;
    if(!p->background_bytes||p->background_bytes>RAW_PAUSE_PIXEL_BYTES)return failed(p,RAW_PAUSE_DIAG_BACKGROUND,1);
    memcpy(b->pixels,p->background,p->background_bytes);
    surface=(RawHudSurface){b->pixels,p->background_bytes,400,240,p->original.record.stride,p->original.record.format};
    if(raw_hud_paint_transparent(h,&surface,&p->layout,&paint)!=RAW_HUD_PAINTED)return failed(p,RAW_PAUSE_DIAG_PAINT,1);
    p->diagnostic_stage=RAW_PAUSE_DIAG_FLUSH;
    if(p->ops.flush(p->ops.user,b,p->background_bytes)!=1)return failed(p,RAW_PAUSE_DIAG_FLUSH,1);
    /* Copying a paused image may span display events without changing that
       image. Refresh only its publication timestamp after preparation, rather
       than rejecting and repeating the entire copy for each such event.
       An empty relay ring may consume events without changing the image.
       Image, ownership, pending and queue-error checks remain mandatory. */
    result=snapshot(p,&ready);if(result!=RAW_PAUSE_READY){p->prepared_snapshot_retries++;return result;}
    if(!raw_pause_snapshot_same_image(s,&ready)){
        p->prepared_source_retries++;p->rejected_expected=*s;p->rejected_current=ready;return RAW_PAUSE_WAIT;
    }
    r=p->original.record;r.swap=1-ready.record.swap;r.display_select=r.swap;
    r.fb_a=b->publish_vaddr;r.fb_b=p->original.record.fb_b?b->publish_vaddr:0;
    /* FCRAM BGR8 top buffer. Bits 8/9 name VRAM addresses and must never
       survive from the original VRAM image (for example format 0x343). */
    r.format=p->original.record.format&~0x300u;r.zero=0;
    p->diagnostic_stage=RAW_PAUSE_DIAG_PUBLISH;
    result=p->ops.publish(p->ops.user,&ready,&r,&submitted_epoch);
    if(result!=1){p->native_publish_retries++;return result<0?failed(p,RAW_PAUSE_DIAG_PUBLISH,1):RAW_PAUSE_WAIT;}
    p->submitted=r;p->submitted_epoch=submitted_epoch;p->current=index;p->ack_seen=0;p->ack_clear_epoch=0;
    p->rendered=*h;p->published=1;p->state=RAW_PAUSE_PANEL_PENDING;return RAW_PAUSE_WAIT;
}
static int step_snapshot(RawPausedDisplay *p,RawPauseSnapshot *s){int r;
    r=snapshot(p,s);if(r!=RAW_PAUSE_READY){p->step_clear_seen=0;p->step_clear_epoch=0;return r;}
    if(!identity(p,s)||memcmp(&s->record,&p->step_record,sizeof(s->record))){
        p->step_clear_seen=0;p->step_clear_epoch=0;return RAW_PAUSE_WAIT;
    }
    /* The receipt proves an original top call, not its completion. Matching
       stable shared record + pending clear proves the write; retain every old
       private slot through another display epoch before painting either. */
    if(!p->step_clear_seen){p->step_clear_seen=1;p->step_clear_epoch=s->top_epoch;return RAW_PAUSE_WAIT;}
    if(s->top_epoch==p->step_clear_epoch)return RAW_PAUSE_WAIT;
    return RAW_PAUSE_READY;
}
int raw_paused_display_step(RawPausedDisplay *p,int paused){
    if(!p||!p->initialized||!paused)return RAW_PAUSE_ERROR;
    if(p->state==RAW_PAUSE_OFF){p->entry_seen=0;p->entry_clear_seen=0;p->entry_clear_epoch=0;
        memset(&p->entry,0,sizeof(p->entry));return RAW_PAUSE_READY;}
    if(p->allocation_failed||p->leaving||p->state==RAW_PAUSE_RETAINED||p->state==RAW_PAUSE_CLEANUP||
       p->state==RAW_PAUSE_RESTORE_PENDING||p->state==RAW_PAUSE_CAPTURING)return RAW_PAUSE_ERROR;
    p->state=RAW_PAUSE_STEP_WAIT;
    /* Existing receipt remains useful if an original present preceded this
       next player step; the later original callback replaces it with latest. */
    return RAW_PAUSE_READY;
}
int raw_paused_display_game_present(RawPausedDisplay *p,const RawPauseRecord *r){uint32_t i;
    if(!p||!p->initialized||p->allocation_failed||p->state!=RAW_PAUSE_STEP_WAIT||!record_valid(r))return 0;
    for(i=0;i<2;i++){uint32_t va=p->buffers[i].publish_vaddr;size_t span;
        if(!raw_hud_surface_span(400,240,r->stride,r->format,&span))return 0;
        if(va&&((r->fb_a<va+RAW_PAUSE_ALLOC_BYTES&&va<r->fb_a+span)||
           (r->fb_b&&r->fb_b<va+RAW_PAUSE_ALLOC_BYTES&&va<r->fb_b+span)))return 0;
    }
    p->step_record=*r;p->step_present_seen=1;p->step_clear_seen=0;p->step_clear_epoch=0;
    return 1;
}
int raw_paused_display_leave(RawPausedDisplay *p,int paused){RawPauseSnapshot s;uint32_t submitted_epoch;int r;
    if(!p||!p->initialized||!paused)return RAW_PAUSE_ERROR;
    if(p->state==RAW_PAUSE_RETAINED)return RAW_PAUSE_ERROR;
    p->leaving=1;
    if(p->state==RAW_PAUSE_OFF||p->state==RAW_PAUSE_CLEANUP)return cleanup(p);
    if(p->state==RAW_PAUSE_STEP_WAIT&&p->step_present_seen){
        r=step_snapshot(p,&s);if(r!=RAW_PAUSE_READY)return r;
        /* Original game already took over. Republishing pause-entry image
           here would rewind the visible frame; only release acknowledged slots. */
        p->published=0;p->state=RAW_PAUSE_CLEANUP;return cleanup(p);
    }
    r=ack(p,&s);if(r!=RAW_PAUSE_READY)return r;
    if(p->state==RAW_PAUSE_RESTORE_PENDING){p->published=0;p->state=RAW_PAUSE_CLEANUP;return cleanup(p);}
    p->diagnostic_stage=RAW_PAUSE_DIAG_RESTORE;
    r=p->ops.publish(p->ops.user,&s,&p->original.record,&submitted_epoch);
    if(r!=1)return r<0?failed(p,RAW_PAUSE_DIAG_RESTORE,1):RAW_PAUSE_WAIT;
    p->submitted=p->original.record;p->submitted_epoch=submitted_epoch;p->state=RAW_PAUSE_RESTORE_PENDING;
    p->ack_seen=0;p->ack_clear_epoch=0;
    return RAW_PAUSE_WAIT;
}
int raw_paused_display_update(RawPausedDisplay *p,const RawHud *h,int paused){RawPauseSnapshot s;int r;
    if(!p||!p->initialized||!h||!paused)return RAW_PAUSE_ERROR;
    if(p->state==RAW_PAUSE_RETAINED)return RAW_PAUSE_ERROR;
    if(!h->visible||p->leaving)return raw_paused_display_leave(p,paused);
    if(p->allocation_failed)return RAW_PAUSE_ERROR;
    if(p->state==RAW_PAUSE_CLEANUP)return RAW_PAUSE_ERROR;
    /* An unchanged paused HUD has no publication or reuse to acknowledge.
       Keep leases without repeatedly rereading executable/GSP contracts. */
    if(p->state==RAW_PAUSE_ACTIVE&&!memcmp(&p->rendered,h,sizeof(*h)))return RAW_PAUSE_READY;
    if(p->state==RAW_PAUSE_STEP_WAIT){
        if(p->step_present_seen){size_t span;int copy;
            r=step_snapshot(p,&s);if(r!=RAW_PAUSE_READY)return r;
            if(!record_valid(&s.record)||!raw_hud_surface_span(400,240,s.record.stride,s.record.format,&span)||
               span>RAW_PAUSE_PIXEL_BYTES)return failed(p,RAW_PAUSE_DIAG_BACKGROUND,4);
            p->diagnostic_stage=RAW_PAUSE_DIAG_BACKGROUND;
            copy=p->ops.background(p->ops.user,&s,p->background,(uint32_t)span);
            if(copy!=1)return copy<0?failed(p,RAW_PAUSE_DIAG_BACKGROUND,5):RAW_PAUSE_WAIT;
            p->background_bytes=(uint32_t)span;p->original=s;
            r=draw_publish(p,h,&s,1-p->current);
            if(p->state==RAW_PAUSE_PANEL_PENDING){
                p->step_present_seen=0;p->step_clear_seen=0;p->step_clear_epoch=0;
            }
            return r;
        }
        /* Some original units do not publish a new game frame. Update only
           the HUD over the retained image; never run an extra unit for display. */
        r=ack(p,&s);if(r!=RAW_PAUSE_READY)return r;
        if(!memcmp(&p->rendered,h,sizeof(*h)))return RAW_PAUSE_READY;
        r=draw_publish(p,h,&s,1-p->current);p->state=RAW_PAUSE_STEP_WAIT;return r;
    }
    if(p->state==RAW_PAUSE_OFF){
        r=snapshot(p,&s);if(r!=RAW_PAUSE_READY){p->entry_seen=0;p->entry_clear_seen=0;return r;}
        if(!record_valid(&s.record))return failed(p,RAW_PAUSE_DIAG_RECORD,1);
        r=entry_event(p,&s);if(r!=RAW_PAUSE_READY)return r;
        {size_t span;int copy;
        if(!raw_hud_surface_span(400,240,s.record.stride,s.record.format,&span)||span>RAW_PAUSE_PIXEL_BYTES)
            return failed(p,RAW_PAUSE_DIAG_BACKGROUND,2);
        p->diagnostic_stage=RAW_PAUSE_DIAG_BACKGROUND;
        p->state=RAW_PAUSE_CAPTURING;
        copy=p->ops.background(p->ops.user,&s,p->background,(uint32_t)span);
        p->state=RAW_PAUSE_OFF;
        if(copy!=1)return copy<0?failed(p,RAW_PAUSE_DIAG_BACKGROUND,3):RAW_PAUSE_WAIT;
        p->background_bytes=(uint32_t)span;p->original=s;}
        if(allocate_buffer(p,0)!=RAW_PAUSE_READY){
            p->state=RAW_PAUSE_CLEANUP;(void)cleanup(p);return RAW_PAUSE_ERROR;
        }
        r=draw_publish(p,h,&s,0);
        if(p->state==RAW_PAUSE_OFF){p->state=RAW_PAUSE_CLEANUP;(void)cleanup(p);}
        return r;
    }
    if(p->state==RAW_PAUSE_RESTORE_PENDING)return raw_paused_display_leave(p,paused);
    r=ack(p,&s);if(r!=RAW_PAUSE_READY)return r;
    p->state=RAW_PAUSE_ACTIVE;
    if(!memcmp(&p->rendered,h,sizeof(*h)))return RAW_PAUSE_READY;
    return draw_publish(p,h,&s,1-p->current);
}
int raw_paused_display_enter(RawPausedDisplay *p,const RawHud *h,int paused){
    return raw_paused_display_update(p,h,paused);
}
int raw_paused_display_cancel(RawPausedDisplay *p,int paused){
    if(!p||!p->initialized||!paused)return RAW_PAUSE_ERROR;
    if(p->state==RAW_PAUSE_OFF&&!p->published&&!p->buffers[0].pixels&&!p->buffers[1].pixels)return cleanup(p);
    /* This is a lifetime fence, not a claim that LCD restoration succeeded.
       Even unpublished buffers are kept to avoid a failed release loop. */
    p->state=RAW_PAUSE_RETAINED;p->leaving=1;return RAW_PAUSE_READY;
}
