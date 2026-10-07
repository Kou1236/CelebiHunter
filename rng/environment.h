#ifndef CH_MANUAL_ENVIRONMENT_H
#define CH_MANUAL_ENVIRONMENT_H
#include "source_capture.h"
enum { RAW_ENV_INITIAL=1,RAW_ENV_TERMINAL=2,RAW_ENV_PREPARATION=3,RAW_ENV_REBASE=4 };
enum { RAW_ENV_OK=0,RAW_ENV_REJECTED=1,RAW_ENV_STORE_FAILED=2,RAW_ENV_POISONED=3 };
enum {
    RAW_ENV_SOURCE_PREFLIGHT=0,RAW_ENV_SOURCE_CAPTURE=1,RAW_ENV_SOURCE_LAYOUT=2,
    RAW_ENV_SOURCE_STATE=3,RAW_ENV_SOURCE_RTC=4,RAW_ENV_SOURCE_CPU=5,
    RAW_ENV_SOURCE_SAVE_TIME=6
};
enum { RAW_ENV_CPU_UNREADABLE=-1,RAW_ENV_CPU_MISMATCH=0,RAW_ENV_CPU_MATCH=1 };
#define RAW_ENV_MAX_WRITES 4096u
typedef struct {uint32_t address;uint8_t before,value,reserved[2];} RawEnvironmentWrite;
typedef struct {
    uint32_t stage,count,div_x;
    RawSourceCapture before;
    /* The player's own StartTime is retained unchanged.  These bytes are the
       temporary RTC inputs derived from it for the model's canonical guest
       clock output. */
    uint8_t preparation_start_time[4];
    uint8_t preparation_rtc[5];
    RawEnvironmentWrite writes[RAW_ENV_MAX_WRITES];
    uint8_t state_identity[32];
    uint8_t identity[32];
} RawEnvironmentPlan;
typedef struct {
    uint32_t initial_applied,terminal_applied,rtc_owned,poisoned;
    uint64_t epoch;
    uint32_t origin_counter;
    uint8_t source_identity[32],nonbudget_CPU_identity[32];
    /* Preparation owns time settings, never a prediction origin. INITIAL
       must capture a later released source that passes semantic CPU guards;
       a per-session identity then binds later result observations. */
    uint32_t preparation_applied,preparation_counter;
    uint8_t preparation_start_time[4];
    uint8_t preparation_rtc[5];
    /* Original live refresh phase is part of the source identity. It is
       observed, never forced back to an old picture phase. */
    uint32_t source_bg;
} RawEnvironmentState;
typedef struct {
    ChReadOps read;
    /* Only called for a validated typed data byte, or exact RTC instruction.
       Actual platform must keep this callback on its owned engine thread. */
    int (*write_data_byte)(void *,uint32_t,uint8_t);
    int (*write_rtc_word)(void *,uint32_t);
    int (*publish_rtc)(void *);
    /* Optional owned contiguous data store. The planner submits only the
       already validated changed bytes, never the gaps between writes. The
       backend must validate the whole typed/RW range before its first store.
       Failure may be partial and uses the same readback rollback as bytes. */
    int (*write_data_span)(void *,uint32_t,const uint8_t *,uint32_t);
} RawEnvironmentOps;
/* Build from this caller's full actual before image. PREPARATION is released,
   with a guarded RTC input derived from that save's StartTime, without source admission.
   INITIAL is released; after preparation it requires a later
   original released source with valid semantic CPU context. TERMINAL additionally
   binds the raw/effective scan receipts and three-call conditional prediction,
   and rejects a predicted DIV that differs from the captured original DIV.
   The early-stage div_x=183 argument remains a compatibility check only.
   No DIV/CPU/RNG/gameplay or synthetic input/control stores. */
int raw_environment_prepare(const RawEnvironmentOps *,const ChNativeContext *,uint32_t tls,
    uint64_t epoch,uint32_t first_dispatch,uint32_t stage,uint32_t div_x,
    const RawEnvironmentState *,const ManualPrediction *,const MpInputPlan *,RawEnvironmentPlan *);
int raw_environment_apply(const RawEnvironmentOps *,RawEnvironmentState *,const RawEnvironmentPlan *);
/* Pure explanation from the retained rejected INITIAL/PREPARATION image. This
   neither rereads process state nor changes any admission/hash condition.
   PREFLIGHT covers guards whose input was not retained in that image. */
uint32_t raw_environment_initial_source_rejection(const RawEnvironmentPlan *,const RawEnvironmentState *);
/* Read-only semantic CPU/context filter. MATCH never establishes source
   admission: full capture, event, timing and prepare/apply guards still run. */
int raw_environment_initial_cpu_check(const ChReadOps *);
/* Pure conversion, preserving StartTime. Reject offsets that cannot produce
   the exact model clock through original FixDays followed by FixTime. */
int raw_environment_derive_preparation_rtc(const uint8_t start[4],uint8_t out[5]);
int raw_environment_data_address(const ChReadOps *read,uint32_t address);
int raw_environment_data_range(const ChReadOps *read,uint32_t address,uint32_t size);
/* Restore only our owned instruction attempt, never foreign bytes. This is
   available even after poison; cleanup cannot clear poison or reactivate a
   source. Data settings remain authorized choices, no saved gameplay restore. */
int raw_environment_restore_rtc(const RawEnvironmentOps *,RawEnvironmentState *);
#endif
