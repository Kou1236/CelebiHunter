#ifndef CH_RAW_DEVICE_3DS_H
#define CH_RAW_DEVICE_3DS_H
#include "raw_service.h"
#include "source_capture.h"
#include "environment_session.h"
#include "result/result_gate.h"
#include "source-observer/aligned_waiting.h"
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
    RAW_SOURCE_WAIT_PROMPT=0,RAW_SOURCE_WAIT_UNIT=1,RAW_SOURCE_WAIT_INPUT=2,
    RAW_SOURCE_WAIT_CPU=3,RAW_SOURCE_WAIT_READ=4,RAW_SOURCE_WAIT_ENEMY=5,
    RAW_SOURCE_WAIT_RESULT=6,RAW_SOURCE_WAIT_PREFLIGHT=7,RAW_SOURCE_ADMITTED=8,
    RAW_SOURCE_STOP_OWNERSHIP=9,RAW_SOURCE_STOP_PATH=10,RAW_SOURCE_STOP_STORE=11,
    RAW_SOURCE_STOP_POISON=12,RAW_SOURCE_STOP_APPLIED=13,RAW_SOURCE_STOP_VERIFY=14,
    RAW_SOURCE_WAIT_CPU_CONTEXT=15,RAW_SOURCE_WAIT_CPU_READ=16,RAW_SOURCE_PREPARED=17
};
enum {
    RAW_RESULT_READ_OK=0,RAW_RESULT_READ_FAILED=1,
    RAW_RESULT_READ_LAYOUT_REJECTED=2,RAW_RESULT_READS_DISAGREED=3
};
enum {
    RAW_WAIT_DIAG_ALIGNMENT=1,RAW_WAIT_DIAG_BINDING=2,RAW_WAIT_DIAG_COUNTER=3,
    RAW_WAIT_DIAG_PROJECTION=4,RAW_WAIT_DIAG_STATE=5,RAW_WAIT_DIAG_INFER=6,
    RAW_WAIT_DIAG_OPERANDS=7,RAW_WAIT_DIAG_RESEED=8
};
/* HUD code families are stable report keys: G scene gate, S source phase,
   T timing/domain guard, W projected-wait check, M failed memory read,
   Q search, R runtime. */
typedef struct {
    uint32_t version,phase,closed,attempts,last_attempt_counter;
    uint32_t counter,guest_pc,guest_sp,guest_mask;
    uint32_t sample_status,scene_status,environment_status,result_reason;
    uint32_t environment_rejection;
    uint32_t cpu_check_status,full_attempts;
    /* Appended versioned fields keep earlier offsets stable and make field
       reports actionable without reproducing the player's run. */
    uint32_t scene_rejection,scene_rejection_detail,waiting_stage,waiting_guard,waiting_read_ordinal;
    uint32_t waiting_read_address,waiting_read_size,waiting_failures;
    uint32_t waiting_alignment_stage,waiting_read_pass;
    /* v3 captures the live inputs behind each displayed S/W/G/Q/R code. */
    uint32_t engine_type,batch_count,host_phase,input_enable;
    uint32_t counter_valid,ordinary_supported;
    uint32_t host_held,host_down,host_up,host_intersection;
    uint32_t scene_script_mode,scene_script_running,scene_script_bank,scene_script_next;
    uint32_t scene_map_group,scene_map_number;
    uint32_t result_enemy_nonzero,result_enemy_first_index,result_enemy_first_value;
    uint32_t runtime_fault,query_status,query_detail,source_bound,candidate_valid,waiting_valid;
    uint32_t current_advance,target_advance,input_plan_error;
    /* v4 identifies the failed result snapshot read or paired-read mismatch. */
    uint32_t result_read_kind,result_read_calls,result_read_ordinal,result_read_address,result_read_size;
    uint32_t engine_pointer,recording_gate,queued_tasks,engine_mode,engine_flags;
    uint32_t selected_config_pointer,selected_config_mode,previous_provider_keys;
    uint32_t scene_rom_pointer,scene_wram0_pointer,scene_wram1_pointer,scene_hram_pointer,scene_io_pointer;
    uint32_t scene_rom_bank,scene_stack_wait_seen,scene_stack_joy_seen,scene_joyp,scene_joy_mirrors[8];
    SourceAlignedDiagnostic waiting_alignment;
    CQWaitingState waiting_expected,waiting_actual;
    /* v5 separates retryable observations from sustained unavailability. */
    uint32_t waiting_unavailable_failures,waiting_unavailable_since,waiting_unavailable_counter;
    uint32_t initial_readonly_retries,initial_retry_counter;
    uint32_t refresh_readonly_retries,refresh_retry_counter;
} RawDeviceSourceDiagnostic;
/* Copied diagnostics only. No extra native/process reads or retry request. */
int raw_device_source_diagnostic_copy(RawDeviceSourceDiagnostic *);
/* Gameplay acquisition is armed by released final GS BALL scene arrival. This
   optional diagnostic request cannot reopen a completed/failed attempt.
   No input/control is supplied by this request. */
void raw_device_request_source(void);
#endif
