#ifndef CH_RAW_RESTART_LIFECYCLE_H
#define CH_RAW_RESTART_LIFECYCLE_H
#include "platform/platform.h"
#include "native/manual_gate.h"
#define RAW_RESTART_RECEIPT_LR UINT32_C(0x0010ed60)
#define RAW_MENU_RESET_RECEIPT_LR UINT32_C(0x001858e8)
#define RAW_GUI_RESET_CALLER_LR UINT32_C(0x00144258)
#define RAW_GUI_RESET_OWNER_ADDRESS UINT32_C(0x002da1a0)
#define RAW_GUI_RESET_CALLBACK_ADDRESS UINT32_C(0x002db738)
#define RAW_FRONTEND_MODE_ADDRESS UINT32_C(0x0022fdd4)
#define RAW_FRONTEND_SCAN_GATE_ADDRESS UINT32_C(0x0022fe70)
#define RAW_FRONTEND_MENU_STATE_ADDRESS UINT32_C(0x0022fed0)
typedef struct {uint8_t mode,scan_gate;} RawFrontendState;
/* Captured while the native callback stack still exists. No stack pointer is
   retained: consumption belongs to the next main-thread pre-HID boundary. */
typedef struct {uint64_t source_epoch,target_epoch;uint32_t caller_lr,valid;} RawRestartNotice;
int raw_frontend_read(const ChReadOps *,RawFrontendState *);
/* 10ED5C follows successful Interruption Load. 1858E4 follows native Reset's
   counter clear. Its saved caller identifies either the internal debug menu
   (10EBAC/state21) or the confirmed touchscreen worker (144258/GUI callback).
   Neither menu entry nor counter regression grants a session. This is a
   receipt only: the touchscreen worker must hand it to the main thread before
   runtime state is reset. The original guest reset continues afterward. */
int raw_restart_receipt(const ChReadOps *,const ChNativeContext *);
int raw_restart_notice_capture(const ChReadOps *,const ChNativeContext *,uint64_t,RawRestartNotice *);
#endif
