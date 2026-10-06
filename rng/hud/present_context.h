#ifndef CH_RAW_PRESENT_CONTEXT_H
#define CH_RAW_PRESENT_CONTEXT_H
#include "framebuffer_hud.h"
#include "../platform/platform.h"
#define RAW_PRESENT_SITE UINT32_C(0x0014547c)
#define RAW_PRESENT_ORIGINAL_BL UINT32_C(0xeb001568)
#define RAW_PRESENT_RETURN UINT32_C(0x00145480)
#define RAW_PRESENT_TARGET UINT32_C(0x0014aa24)
typedef struct {
    ChReadOps read;
    uint32_t prepared,active,site_word;
} RawPresentBinding;
typedef struct {
    uint32_t screen_id,swap,fb_a,fb_b,stride,format,display_select;
    uint32_t width,height,surface_bytes,caller_sp,caller_lr;
} RawPresentArgs;
enum {RAW_PRESENT_INVALID=0,RAW_PRESENT_ADMITTED=1};
/* Owned startup, BEFORE installing a BL. Reads exact pristine wrapper,
 * present callee, get-GSP accessor and upstream argument-building evidence.
 * This local evidence is additional to the installer's executable identity. */
int raw_present_prepare(RawPresentBinding *,const ChReadOps *);
/* Owned startup AFTER installer publication/readback. expected_site_word is
 * the installer's actual published word. No write or installation occurs.
 * The pristine BL is also accepted for offline baseline fixtures. */
int raw_present_activate(RawPresentBinding *,uint32_t expected_site_word);
/* Read-only revalidation for an independently owned display-only call. */
int raw_present_contract_current(const RawPresentBinding *);
/* Read-only native-context decoder. Returns copied current present args only;
 * admission does not retain pointers or prove future displayed ownership.
 * Rendering/error handling can never change the original 14AA24 call chain. */
int raw_present_decode(const RawPresentBinding *,const ChNativeContext *,RawPresentArgs *);
#endif
