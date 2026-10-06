#ifndef CH_PAUSED_DISPLAY_H
#define CH_PAUSED_DISPLAY_H
#include "framebuffer_hud.h"
#define RAW_PAUSE_PIXEL_BYTES UINT32_C(288000)
#define RAW_PAUSE_ALLOC_BYTES UINT32_C(0x47000)
/* The original GSP shared record contains virtual addresses, not physical. */
typedef struct {uint32_t swap,fb_a,fb_b,stride,format,display_select,zero;} RawPauseRecord;
typedef struct {
    uint32_t context,descriptor,control,graphics_busy,top_epoch;
    RawPauseRecord record;
    /* The exact current native GSP relay-ring mapping, never a guessed alias.
       queue_control packs head/count/error; the snapshot checks both reads. */
    uint32_t event_queue,shared_base,thread_id,queue_control;
} RawPauseSnapshot;
typedef struct {
    uint8_t *pixels;
    uint32_t vaddr,paddr,bytes;
    /* Drawing/cache-flush VA and GSP publication VA can differ for Luma's
     * reserved plugin heap. publish_vaddr is the canonical PA-0x0C000000. */
    uint32_t publish_vaddr;
} RawPauseBuffer;
typedef struct {
    void *user;
    /* Read only, coherent fresh top descriptor plus original display-event
     * epoch. 1=read, 0=retry, -1=unavailable. No original event pump. */
    int (*snapshot)(void *,RawPauseSnapshot *);
    /* A failing alloc leaves zero output unless a failed rollback leaves an
     * owned slot requiring release. Failed release retains its slot.
     * Only exclusive reserved loader-heap slots may be returned here. */
    int (*allocate)(void *,uint32_t,RawPauseBuffer *);
    int (*release)(void *,RawPauseBuffer *);
    int (*flush)(void *,const RawPauseBuffer *,uint32_t);
    /* Exact 14AA24 seven-argument top publication. Returns 1 only after its
     * record write. 0/-1 MUST mean no publication occurred. Never engine/HID. */
    int (*publish)(void *,const RawPauseSnapshot *,const RawPauseRecord *);
    /* Owned scan boundary: coherent, read-only original image. Never a
       currently leased display slot. 0 retries without any publication. */
    int (*background)(void *,const RawPauseSnapshot *,uint8_t *,uint32_t);
} RawPauseOps;
enum {RAW_PAUSE_ERROR=-1,RAW_PAUSE_WAIT=0,RAW_PAUSE_READY=1};
enum {RAW_PAUSE_OFF=0,RAW_PAUSE_PANEL_PENDING=1,RAW_PAUSE_ACTIVE=2,
      RAW_PAUSE_RESTORE_PENDING=3,RAW_PAUSE_CLEANUP=4,RAW_PAUSE_RETAINED=5,RAW_PAUSE_CAPTURING=6,
      RAW_PAUSE_STEP_WAIT=7};
enum {RAW_PAUSE_DIAG_SNAPSHOT=1,RAW_PAUSE_DIAG_RECORD=2,RAW_PAUSE_DIAG_ALLOC0=3,
      RAW_PAUSE_DIAG_ALLOC1=4,RAW_PAUSE_DIAG_BUFFER=5,RAW_PAUSE_DIAG_OVERLAP=6,
      RAW_PAUSE_DIAG_PAINT=7,RAW_PAUSE_DIAG_FLUSH=8,RAW_PAUSE_DIAG_PUBLISH=9,
      RAW_PAUSE_DIAG_ACK=10,RAW_PAUSE_DIAG_RESTORE=11,RAW_PAUSE_DIAG_RELEASE=12,
      RAW_PAUSE_DIAG_BACKGROUND=13,RAW_PAUSE_DIAG_ENTRY_EVENT=14};
typedef struct {
    RawPauseOps ops;
    RawHudLayout layout;
    RawHud rendered;
    RawPauseBuffer buffers[2];
    RawPauseSnapshot original;
    /* The first ordinary pending-clear record is not a display-event receipt.
       Entry pins an empty relay queue, then waits for two later native top
       events before copying or publishing; one stale callback cannot admit it. */
    RawPauseSnapshot entry;
    uint32_t entry_seen,entry_clear_seen,entry_clear_epoch;
    RawPauseRecord submitted;
    uint32_t initialized,state,current,submitted_epoch,published,leaving;
    /* A partial or rejected deferred lease may be released after restoration,
       but must never be retried, painted, or used for another player step. */
    uint32_t allocation_failed;
    uint32_t ack_seen,ack_clear_epoch;
    RawPauseRecord step_record;
    uint32_t step_present_seen,step_clear_seen,step_clear_epoch;
    /* Sticky failure record: cleanup must not erase the cause of OFF state. */
    uint32_t diagnostic_stage,diagnostic_error_stage,diagnostic_error_code,diagnostic_errors;
    /* Immutable for this pause. BSS storage, never the game-thread stack. */
    uint32_t background_bytes;
    uint8_t background[RAW_PAUSE_PIXEL_BYTES];
} RawPausedDisplay;
/* No allocation, service initialization, rendering, or native writes here. */
int raw_paused_display_init(RawPausedDisplay *,const RawPauseOps *,const RawHudLayout *);
/* Caller must own the raw scan boundary AND already be manually paused.
 * One nonblocking poll each; parent performs its existing wait between polls.
 * READY from enter/update means panel acknowledged, or HUD was hidden.
 * Normal leave restores and confirms the display before returning READY.
 * After a player exit, the parent bounds this wait; cancel retains every
 * possibly displayed buffer while allowing the original game to render again.
 * A hidden HUD invokes restoration before reporting READY. No pointer to a
 * game framebuffer is painted, flushed, allocated, or released. The original
 * image is copied read-only once, before any private publication. Entry first
 * observes a stable ordinary record and empty native event queue, then waits
 * for two later top events with the same record and empty queue. The first
 * can be a previously dequeued callback. It never pumps an event or runs
 * another game unit. Entry leases
 * only its first private slot. The alternate remains reserved and is leased
 * with the same full validation only when an acknowledged update needs it. */
int raw_paused_display_enter(RawPausedDisplay *,const RawHud *,int already_paused);
int raw_paused_display_update(RawPausedDisplay *,const RawHud *,int already_paused);
int raw_paused_display_leave(RawPausedDisplay *,int already_paused);
/* Player L only: keep every private lease and the currently displayed image.
 * READY permits the original one-unit call to continue immediately. No old
 * framebuffer restoration, rendering, flush, publication or release occurs.
 * A subsequent paused update captures only an actual original top present. */
int raw_paused_display_step(RawPausedDisplay *,int already_paused);
/* Read-only copied receipt from the admitted original top-present call,
 * immediately before chaining its writer. Never publishes or changes pixels.
 * The later coherent shared record and two display epochs prove takeover. */
int raw_paused_display_game_present(RawPausedDisplay *,const RawPauseRecord *);
/* Only after a player's exit request exhausted bounded restoration polling.
 * Do not free, reuse, paint or publish a possibly scanned buffer. Retain its
 * lease until process teardown and let original game rendering take over. */
int raw_paused_display_cancel(RawPausedDisplay *,int already_paused);
#endif
