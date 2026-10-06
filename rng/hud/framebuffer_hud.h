#ifndef CH_FRAMEBUFFER_HUD_H
#define CH_FRAMEBUFFER_HUD_H
#include <stddef.h>
#include <stdint.h>
#include "../raw_service.h"

/* Display coordinates are landscape. Memory is column-major, bottom first:
 * offset = x * stride + (239 - y) * bytes_per_pixel. The caller owns the
 * currently submitted framebuffer for the complete duration of this call. */
typedef struct {
    uint8_t *pixels;
    size_t bytes;
    uint32_t width,height,stride,format;
} RawHudSurface;
typedef struct { int32_t x,y; uint32_t columns; } RawHudLayout;
typedef struct {
    uint32_t x,y,width,height,glyphs;
    size_t first_byte,end_byte;
} RawHudPaint;
/* Sparse underlay: only pixels actually changed by glyphs/shadows are read.
 * Pixels use local column-major indices and a one-bit ownership mask. */
typedef struct {
    uint8_t *pixels,*mask;
    size_t bytes,mask_bytes;
    uint32_t x,y,width,height,saved_pixels;
} RawHudCapture;
enum { RAW_HUD_INVALID=-1,RAW_HUD_SKIPPED=0,RAW_HUD_PAINTED=1 };
#define RAW_HUD_COLUMNS 50u
#define RAW_HUD_ROWS 12u
#define RAW_HUD_ROW_PITCH 10u
#define RAW_HUD_GLYPH_PITCH 6u
#define RAW_HUD_PADDING 4u
#define RAW_HUD_BACKGROUND 0x101820u
/* No game state, input, graphics service, heap, or retained framebuffer. */
int raw_hud_surface_span(uint32_t width,uint32_t height,uint32_t stride,
    uint32_t format,size_t *span);
int raw_hud_paint(const RawHud *,const RawHudSurface *,const RawHudLayout *,RawHudPaint *);
/* Running overlay: glyphs plus one-pixel black shadow, no rectangle fill.
 * Uncovered current-frame pixels are untouched. No retained background. */
int raw_hud_paint_transparent(const RawHud *,const RawHudSurface *,const RawHudLayout *,RawHudPaint *);
int raw_hud_paint_capture(const RawHud *,const RawHudSurface *,const RawHudLayout *,RawHudPaint *,RawHudCapture *);
#endif
