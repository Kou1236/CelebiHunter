#ifndef CELEBI_RAW_RUNTIME_H
#define CELEBI_RAW_RUNTIME_H
#include "controller/manual_controller.h"
#include "native/manual_gate.h"
#include "prediction/manual_prediction.h"
#include "input-plan/manual_input_plan.h"
#include "candidate-query/query.h"

/* Actual original physical-scan boundary, not a rendered frame or scheduler
 * visit. Platform collects all fields read-only from its checked mappings. */
typedef struct {
    uint32_t counter, counter_read, sample_complete, ordinary_supported, batch, host_phase, input_type;
    uint32_t host_held, host_down, host_up, host_intersection;
    uint32_t input_enable, guest_mask, script_final_prompt;
    uint64_t scene_epoch;
} RawSample;
enum { RAW_EVENT_BEFORE_SCAN=1,RAW_EVENT_AFTER_SCAN=2,
       RAW_EVENT_ENGINE_ENTRY=3,RAW_EVENT_SCHEDULER=4 };
enum { RAW_FAULT_NONE=0,RAW_FAULT_READ=1,RAW_FAULT_UNIT=2,
       RAW_FAULT_FILTER=3,RAW_FAULT_ORDER=4,RAW_FAULT_COMMAND=5,RAW_FAULT_ENVIRONMENT=6,
       RAW_FAULT_DISPLAY=7 };
#define RAW_LOG_CAPACITY 128u
#define RAW_INPUT_PLAN_WINDOW 64u
typedef struct {
    uint32_t event,scan_id,counter,previous_held,held,intersection,mask,batch,phase;
} RawObservation;
typedef struct {
    ch_controller controls;
    ch_output view;
    ch_runtime_state runtime;
    uint32_t counter,counter_valid,at_gate,last_request,completed_request,step_target;
    uint64_t scene_epoch,source_generation;
    uint32_t unit_supported,fault,scan_id,scan_open,scan_before_counter,scan_before_held;
    uint32_t engine_entries,previous_unit_supported,have_previous_scan;
    uint32_t observation_count,observation_overflow;
    RawObservation observations[RAW_LOG_CAPACITY];
    ManualSourceBinding source;
    ManualPrediction candidate,encounter_candidate;
    uint32_t source_bound,raw_target,effective_target,release_target;
    uint32_t guest_a_previous,guest_a_initialized,encounter_started;
    uint32_t effective_press,effective_release,release_seen,actual_dv,actual_seen;
    uint32_t input_condition_matches;
    MpInputPlan input_plan;
    uint32_t plan_active,plan_failed,plan_pending;
    RawSample latest;
    uint32_t source_input_type,source_phase,source_batch;
    RawSample observed;
    uint32_t original_raw_a_seen;
    uint32_t actual_raw_press_seen,actual_raw_press,actual_raw_release_seen,actual_raw_release;
    uint32_t actual_press_mask,actual_release_mask;
    uint32_t display_pending,display_error;
    uint32_t query_status,refresh_needed,step_by_physical_a;
    uint32_t search_started,search_requested;
    uint32_t query_failure_detail,query_submitted_counter,query_returned_counter,query_result_target;
    uint32_t source_bg;
    /* Actual aligned first-scheduler timing, copied into read-only jobs. */
    CQWaitingState waiting;
    uint32_t waiting_counter,waiting_valid;
} RawRuntime;

void raw_runtime_init(RawRuntime *,uint32_t physical_already_held);
/* Called on emulator thread at the original BL 1042F0, before HID refresh.
 * No scan is logged until the player has actually let this call continue.
 */
void raw_runtime_before_scan(RawRuntime *,const RawSample *);
void raw_runtime_poll_player(RawRuntime *,uint32_t physical_keys,const RawSample *);
void raw_runtime_begin_scan(RawRuntime *,const RawSample *);
void raw_runtime_after_scan(RawRuntime *,const RawSample *);
void raw_runtime_engine_entry(RawRuntime *,const RawSample *);
int raw_runtime_command(RawRuntime *,const ch_command *);
int raw_runtime_bind_source(RawRuntime *,const ManualSourceBinding *,uint64_t generation);
/* Candidate's effective-A counter remains untouched. Only this conditional
 * B1/two-original-scan view displays the earlier raw-press counter. */
int raw_runtime_accept_candidate(RawRuntime *,const ch_query *,const ManualPrediction *);
/* Immutable result proof at its original query boundary. Receipt time and
   future-window expiry are classified separately by the service. */
int raw_runtime_candidate_bound(const RawRuntime *,const ch_query *,const ManualPrediction *);
/* Retry only a correctly bound result whose physical target passed in flight.
   This never pauses, advances or supplies input. */
int raw_runtime_retry_missed_candidate(RawRuntime *,const ch_query *,const ManualPrediction *);
int raw_runtime_retry_near_candidate(RawRuntime *,const ch_query *,const ManualPrediction *,uint32_t lead);
int raw_runtime_query_failed(RawRuntime *,const ch_query *,uint32_t status);
void raw_runtime_actual_dv(RawRuntime *,uint16_t);
uint32_t raw_original_target(uint32_t saved_lr);
void raw_runtime_sample_failed(RawRuntime *);
/* Withdraw the conditional forecast; do not pause, advance or fabricate input. */
void raw_runtime_environment_failed(RawRuntime *);
void raw_runtime_display_failed(RawRuntime *);
#endif
