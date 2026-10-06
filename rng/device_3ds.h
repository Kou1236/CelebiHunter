#ifndef CH_RAW_DEVICE_3DS_H
#define CH_RAW_DEVICE_3DS_H
#include "raw_service.h"
#include "source_capture.h"
#include "environment_session.h"
#include "result/result_gate.h"
typedef struct {
    void *user;
    /* Optional copied UI snapshot. Native present sink is provided internally;
       this callback must never pause/step/sample or bind a model on its own. */
    void (*publish_hud)(void *,const RawHud *);
} RawDeviceHostOps;
/* Only call from the English Crystal pinned Luma loader startup entry, with
   game startup still owned. Builds are research objects, not installed 3GX. */
int raw_device_startup(const RawDeviceHostOps *);
/* Copied into the loader's current stack before a poisoned startup panic so
 * a standard Luma dump retains the failing phase and actual installed words.
 * Read-only diagnostics; this function never attempts installation/recovery. */
typedef struct {
    uint32_t magic,version,phase,caller_status;
    int32_t install_status;
    uint32_t source_page,alias_page,point_count,attempted_count,alias_mapped,poisoned;
    uint32_t sites[5],original_words[5],installed_words[5],current_words[5],readable_bits;
} RawDeviceStartupDiagnostic;
void raw_device_startup_diagnostic(RawDeviceStartupDiagnostic *,int32_t status);
int raw_device_hud_copy(RawHud *);
int raw_device_source_copy(RawSourceCapture *);
int raw_device_environment_copy(RawEnvironmentState *);
int raw_device_result_copy(ChResultObservation *);
enum {
    RAW_SOURCE_WAIT_PROMPT=0,RAW_SOURCE_WAIT_UNIT,RAW_SOURCE_WAIT_INPUT,
    RAW_SOURCE_WAIT_CPU,RAW_SOURCE_WAIT_READ,RAW_SOURCE_WAIT_ENEMY,
    RAW_SOURCE_WAIT_RESULT,RAW_SOURCE_WAIT_PREFLIGHT,RAW_SOURCE_ADMITTED,
    RAW_SOURCE_STOP_OWNERSHIP,RAW_SOURCE_STOP_PATH,RAW_SOURCE_STOP_STORE,
    RAW_SOURCE_STOP_POISON,RAW_SOURCE_STOP_APPLIED,RAW_SOURCE_STOP_VERIFY,
    RAW_SOURCE_WAIT_CPU_CONTEXT,RAW_SOURCE_WAIT_CPU_READ,RAW_SOURCE_PREPARED
};
typedef struct {
    uint32_t version,phase,closed,attempts,last_attempt_counter;
    uint32_t counter,guest_pc,guest_sp,guest_mask;
    uint32_t sample_status,scene_status,environment_status,result_reason;
    uint32_t environment_rejection;
    uint32_t cpu_check_status,full_attempts;
} RawDeviceSourceDiagnostic;
/* Copied diagnostics only. No extra native/process reads or retry request. */
int raw_device_source_diagnostic_copy(RawDeviceSourceDiagnostic *);
/* Gameplay acquisition is armed by released final GS BALL scene arrival. This
   optional diagnostic request cannot reopen a completed/failed attempt.
   No input/control is supplied by this request. */
void raw_device_request_source(void);
#endif
