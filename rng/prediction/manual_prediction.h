#ifndef CELEBI_MANUAL_PREDICTION_H
#define CELEBI_MANUAL_PREDICTION_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "../data/solver.h"

/* Pure prediction. No memory writes, device access, game control or input API.
 * One advance means one completed native engine call at the admitted boundary,
 * never one HUD redraw or one RNG call. All counters are uint32 modulo 2^32.
 */
#define MANUAL_PREDICTION_ABI 1u
#define MANUAL_MODEL_MIN_N 2u
#define MANUAL_MODEL_MAX_N 0x7fffffffu
#define MANUAL_MODEL_MAX_PRESS (MANUAL_MODEL_MAX_N - 2u)

typedef struct { uint8_t bytes[32]; } ManualDigest;
typedef enum {
    MANUAL_QUERY_OK = 0,
    MANUAL_QUERY_INVALID = 1,
    MANUAL_QUERY_STALE_SOURCE = 2,
    MANUAL_QUERY_UNSUPPORTED_CERTIFICATE = 3,
    MANUAL_QUERY_OUTSIDE_DOMAIN = 4,
    MANUAL_QUERY_NO_FUTURE_CANDIDATE = 5,
    MANUAL_QUERY_SOLVER_BOUND = 6
} ManualQueryStatus;
typedef enum {
    MANUAL_EVIDENCE_CONDITIONAL = 0,
    MANUAL_EVIDENCE_ORIGINAL_NATIVE_SELECTED_CASE = 1
} ManualEvidence;
typedef enum {
    /* This is the existing proof's input condition, not a command to send A. */
    MANUAL_INPUT_ORIGINAL_LEAF_HOLD_THREE_CALLS = 1
} ManualInputContract;
typedef enum {
    MANUAL_CERTIFICATE_CONDITIONAL_FIXED_CONTEXT = 1,
    MANUAL_CERTIFICATE_CONDITIONAL_RETAINED_DIV = 2
} ManualCertificateScope;

/* A model certificate names immutable derivation artifacts and its scope.
 * A caller's true/false claim cannot substitute for this record. These hashes
 * identify a REQUIRED context; they do not assert any live readback succeeded.
 */
typedef struct {
    uint32_t abi, scope, profile_id[8], input_contract;
    ManualDigest profile_artifact, clock_derivation, normalization_profile;
    ManualDigest derivation_source;
} ManualModelCertificate;

typedef struct {
    uint32_t abi, rng_add, rng_sub, origin_counter;
    uint64_t source_epoch;
    ManualDigest source_identity, required_context_identity;
} ManualSourceBinding;

typedef struct {
    uint32_t counter;
    uint64_t source_epoch;
    ManualDigest source_identity, required_context_identity;
} ManualCurrentBoundary;

typedef struct {
    uint32_t abi, status, evidence, input_contract;
    uint64_t source_epoch;
    ManualDigest source_identity, required_context_identity;
    ManualDigest native_proof_identity;
    uint32_t origin_counter, queried_at_counter, target_counter;
    uint32_t expected_release_counter, press_relative, release_relative;
    uint32_t remaining_advances, model_n, div_x, branch, predicted_dv;
    uint32_t expected_wait_add, expected_wait_sub;
    uint32_t algebraic_x_cases, target_pair_count;
    /* No manual-input proof exists yet. Always zero in this implementation. */
    uint32_t manual_input_validated;
} ManualPrediction;

/* Returned descriptor is conditional even on the historical source. */
SOL61_API const ManualModelCertificate *manual_model_certificate(void);

/* Legacy signature cannot express the actual source BG phase required by
 * this compound certificate. Valid bound arguments return UNSUPPORTED.
 * Live conditional jobs use the explicit BG adapter instead. */
SOL61_API int manual_query_future(const ManualSourceBinding *source,
    const ManualCurrentBoundary *current, const ManualModelCertificate *certificate,
    Sol61Workspace *workspace, ManualPrediction *prediction);

/* Rechecks binding and arithmetic at publication after a possibly long job.
 * Returns STALE_SOURCE on epoch/identity changes and NO_FUTURE on a passed target.
 * It changes no game state. A target equal to the current boundary is reached,
 * and therefore is valid for presentation, but not for a new future query.
 */
SOL61_API int manual_prediction_state(const ManualPrediction *prediction,
    const ManualCurrentBoundary *current, uint32_t *remaining);

/* Only the exact immutable selected-source original-native receipt can promote
 * evidence. This never proves manual physical input, other sources, or all N.
 * The receipt is compiled from hash-checked original artifacts by build_check.py.
 */
SOL61_API int manual_admit_original_native_case(const ManualSourceBinding *source,
    ManualPrediction *prediction);
SOL61_API uint32_t manual_prediction_bytes(void);
SOL61_API uint32_t manual_source_binding_bytes(void);
SOL61_API uint32_t manual_current_boundary_bytes(void);
SOL61_API uint32_t manual_certificate_bytes(void);
#ifdef __cplusplus
}
#endif
#endif
