#ifndef CELEBI_MANUAL_INPUT_PLAN_H
#define CELEBI_MANUAL_INPUT_PLAN_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(_WIN32)
#define MP_API __declspec(dllexport)
#else
#define MP_API
#endif
#define MP_ABI 1u
#define MP_LOG_CAPACITY 1536u
#define MP_A_BIT 1u

typedef struct { uint8_t bytes[32]; } MpDigest;
typedef enum {
    MP_OK = 0, MP_BAD_ARGUMENT = 1, MP_UNSUPPORTED_SOURCE = 2,
    MP_STALE_SOURCE = 3, MP_RAW_TARGET_NOT_FUTURE = 4,
    MP_INPUT_MISMATCH = 5, MP_COUNTER_MISMATCH = 6,
    MP_LOG_FULL = 7, MP_OUT_OF_ORDER = 8
} MpError;
typedef enum {
    MP_CONDITIONAL_WAIT_RAW_PRESS = 1,
    MP_CONDITIONAL_WAIT_SECOND_SCAN = 2,
    MP_CONDITIONAL_HOLD = 3,
    MP_CONDITIONAL_WAIT_RELEASE_PROVIDER = 4,
    MP_CONDITIONAL_INPUT_OBSERVED = 5,
    MP_INVALID = 6
} MpState;
typedef enum { MP_LOG_SCAN = 1, MP_LOG_PROVIDER = 2, MP_LOG_COUNTER = 3 } MpEventKind;
typedef enum {
    MP_POINT_PROVIDER_RETURN_1044DC = 1,
    MP_POINT_APPLIED_MASK_MARKER_1A82DC = 2
} MpMaskObservationPoint;

/* This names actual original code evidence. It is not a caller boolean and
 * does not certify hardware timing, environment state or a model's whole N
 * domain. All resulting plans remain conditional.
 */
typedef struct {
    uint32_t abi, raw_gate_address, raw_gate_word;
    uint32_t and_address, and_word, store_address, store_word;
    uint32_t increment_address, increment_word;
    MpDigest native_code_identity;
} MpOriginalInputEvidence;

typedef struct {
    uint32_t abi, counter, engine_type, host_batch_count, host_phase;
    uint32_t input_enable, original_host_held, original_host_intersection;
    uint32_t actual_applied_mask;
    uint64_t source_epoch;
    MpDigest source_identity;
} MpSourceConditions;

typedef struct {
    uint32_t abi, effective_a_counter, expected_release_counter;
    uint64_t source_epoch;
    MpDigest source_identity, model_certificate_identity, required_context_identity;
} MpConditionalTarget;

typedef struct {
    uint32_t kind, counter, previous_held, current_held, intersection;
    uint32_t provider_mask, next_counter, observation_point;
} MpObservation;

typedef struct {
    uint32_t abi, state, error;
    uint64_t source_epoch;
    MpDigest source_identity, model_certificate_identity, required_context_identity;
    uint32_t origin_counter, current_counter;
    uint32_t player_raw_press_counter, effective_a_counter;
    uint32_t player_raw_release_counter, expected_release_counter;
    uint32_t original_engine_type, original_host_batch_count, original_host_phase;
    uint32_t last_held, last_intersection, last_provider_mask;
    uint32_t observed_raw_press_counter, observed_effective_a_counter;
    uint32_t observed_raw_release_counter, observed_effective_release_counter;
    uint32_t raw_press_seen, effective_a_seen, raw_release_seen, effective_release_seen;
    uint32_t scan_seen_at_counter, provider_seen_at_counter, scan_recorded, mask_recorded;
    uint32_t log_count;
    MpObservation log[MP_LOG_CAPACITY];
    /* No player input or manual hardware claim may be fabricated by this API. */
    uint32_t manual_hardware_verified;
} MpInputPlan;

MP_API const MpOriginalInputEvidence *mp_original_input_evidence(void);
MP_API uint32_t mp_input_plan_bytes(void);
MP_API uint32_t mp_source_conditions_bytes(void);
MP_API uint32_t mp_conditional_target_bytes(void);
MP_API uint32_t mp_original_input_evidence_bytes(void);

/* Released baseline plus ordinary B=1 maps player raw press to guest A-1.
 * A new plan requires that PLAYER raw press is strictly after current counter.
 * No controller, pause, input delivery or environment operations are performed.
 */
MP_API int mp_plan_create(const MpSourceConditions *actual,
    const MpConditionalTarget *target, const MpOriginalInputEvidence *code,
    MpInputPlan *plan);

/* Call only after an original HID read + original AND/store completed; never
 * supply a UI key edge as a scan. Values must be actual before/after host words.
 * 'actual.counter' is the native call-entry counter, before the tail increment.
 */
MP_API int mp_plan_observe_scan(MpInputPlan *plan, const MpSourceConditions *actual,
    uint32_t previous_held, uint32_t current_held, uint32_t intersection);

/* Observe actual original 195DF4 output at its observed return. This receipt
 * indicates a leaf callback actually occurred. It is optional because unchanged
 * input can skip the leaf. Each engine call still needs an applied-mask receipt.
 */
MP_API int mp_plan_observe_provider(MpInputPlan *plan,
    const MpSourceConditions *actual, uint32_t actual_mask);

/* Read actual 22F766 at original marker 1A82DC after host processing. This
 * supports the unchanged-input path and never claims the leaf ran this call.
 */
MP_API int mp_plan_observe_applied_mask(MpInputPlan *plan,
    const MpSourceConditions *actual, uint32_t actual_mask);

/* After original 1A84FC performed counter++ and the engine returned, record
 * actual before and after. Each call must have an original scan in B=1 mode.
 */
MP_API int mp_plan_observe_counter(MpInputPlan *plan,
    const MpSourceConditions *after, uint32_t actual_before_counter);

/* Recheck guards at a physical-input-before-scan gate. No scan is performed.
 * After a missed raw target an old plan is invalid, even if guest A is future.
 */
MP_API int mp_plan_state(MpInputPlan *plan, const MpSourceConditions *actual,
    uint32_t *distance_to_player_press);
#ifdef __cplusplus
}
#endif
#endif
