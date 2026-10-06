#ifndef CH_HUD_SINK_3DS_H
#define CH_HUD_SINK_3DS_H
#include <3ds.h>
#include "framebuffer_hud.h"
#define RAW_HUD_UNDERLAY_BYTES ((2u*RAW_HUD_PADDING+RAW_HUD_COLUMNS*RAW_HUD_GLYPH_PITCH)*(2u*RAW_HUD_PADDING+RAW_HUD_ROWS*RAW_HUD_ROW_PITCH)*3u)
#define RAW_HUD_UNDERLAY_MASK_BYTES ((RAW_HUD_UNDERLAY_BYTES/3u+7u)/8u)
typedef struct {
    LightLock lock;
    RawHud published;
    RawHudLayout layout;
    uint32_t initialized,screen_id;
    uint32_t background_valid,background_a,background_b,background_stride,background_format;
    uint32_t background_x,background_y,background_width,background_height,background_bpp;
    uint32_t presentation_sequence,background_sequence;
    uint8_t background[RAW_HUD_UNDERLAY_BYTES];
    uint8_t background_mask[RAW_HUD_UNDERLAY_MASK_BYTES];
    uint32_t background_saved_pixels;
    uint32_t background_unpainted;
} RawHud3dsSink;
enum { RAW_HUD_SINK_ERROR=-1,RAW_HUD_SINK_SKIPPED=0,RAW_HUD_SINK_PAINTED=1 };
/* Startup setup only; no service initialization or hook installation. */
int raw_hud_3ds_sink_init(RawHud3dsSink *,uint32_t screen_id,const RawHudLayout *);
/* Assign to RawDeviceHostOps.publish_hud with sink as the matching user.
 * Copies only. Never draws from the scan/service publisher. */
void raw_hud_3ds_publish(void *,const RawHud *);
/* Forward CURRENT parameters from the proven owner present callback only.
 * For a paused redraw, the owner must separately establish that these same
 * displayed buffers are writable and still owned for this whole call.
 * This function does not retain pointers or submit/swap a framebuffer. */
int raw_hud_3ds_present(RawHud3dsSink *,uint32_t screen_id,uint32_t swap,
    uint8_t *fb_a,uint8_t *fb_b,uint32_t stride,uint32_t format);
/* Receipt only for an admitted current original top-present callback where
 * this sink paints nothing. Replaces the previous generation and records the
 * exact surface identity. Never reads pixels, queries mappings, draws, flushes
 * or publishes. The caller still chains the original framebuffer writer. */
int raw_hud_3ds_original_unpainted_present(RawHud3dsSink *,uint32_t screen_id,uint32_t swap,
    uint8_t *fb_a,uint8_t *fb_b,uint32_t stride,uint32_t format);
/* Restore only the copied HUD rectangle into an owned pause-image clone.
   An exact unpainted receipt leaves the clone unchanged. Never writes a game
   framebuffer; requires the current generation's exact surface identity. */
int raw_hud_3ds_restore_background(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint8_t *,uint32_t);
#endif
