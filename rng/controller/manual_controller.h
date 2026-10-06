#ifndef CELEBI_MANUAL_CONTROLLER_H
#define CELEBI_MANUAL_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches the libctru key bits. These are physical levels, never synthetic input. */
#define CH_KEY_A       (1u << 0)
#define CH_KEY_START   (1u << 3)
#define CH_KEY_UP      (1u << 6)
#define CH_KEY_R       (1u << 8)
#define CH_KEY_L       (1u << 9)
#define CH_KEY_X       (1u << 10)
#define CH_KEY_Y       (1u << 11)

typedef enum {
    CH_RUNTIME_RUNNING = 0,
    CH_RUNTIME_PAUSE_PENDING,
    CH_RUNTIME_PAUSED,
    CH_RUNTIME_STEPPING,
    CH_RUNTIME_FAILED
} ch_runtime_state;

typedef enum {
    CH_COMMAND_NONE = 0,
    CH_COMMAND_PAUSE_AT_BOUNDARY,
    CH_COMMAND_STEP_ONE_BOUNDARY,
    CH_COMMAND_RESUME
} ch_command_kind;

typedef enum {
    CH_PREDICTION_UNAVAILABLE = 0,
    CH_PREDICTION_QUERYING,
    CH_PREDICTION_FUTURE,
    CH_PREDICTION_READY,
    CH_PREDICTION_MISSED,
    CH_PREDICTION_QUERY_FAILED
} ch_prediction_state;

typedef enum {
    CH_FAULT_NONE = 0,
    CH_FAULT_RUNTIME_FAILED,
    CH_FAULT_BAD_ACK,
    CH_FAULT_STEP_OVERSHOT,
    CH_FAULT_SCENE_CHANGED_DURING_COMMAND,
    CH_FAULT_COUNTER_DISCONTINUITY
} ch_control_fault;

typedef struct {
    uint32_t physical_keys;
    uint32_t counter;
    uint64_t scene_epoch;
    /* Increase when prediction inputs are replaced, calibrated, or invalidated;
       do not increment for ordinary progress already described by counter. */
    uint64_t source_generation;
    ch_runtime_state runtime;
    uint32_t completed_request_id;
    uint8_t counter_valid;
    uint8_t at_native_boundary;
    uint8_t scene_eligible;
} ch_input;

typedef struct {
    ch_command_kind kind;
    uint32_t request_id;
    uint32_t expected_counter;
    uint32_t target_counter; /* STEP must stop exactly here, including wrap. */
    uint64_t scene_epoch;
    uint8_t initiated_by_physical_a;
} ch_command;

typedef struct {
    uint32_t query_id;
    uint32_t source_counter;
    uint32_t minimum_target_counter;
    uint64_t scene_epoch;
    uint64_t source_generation;
} ch_query;

typedef struct {
    ch_command command; /* An edge pulse. Execute each request_id at most once. */
    ch_query query;     /* Read-only request. Never pause in order to service it. */
    uint32_t ui_consumed_keys; /* UI arbitration only; NEVER a guest key mask. */
    uint32_t remaining_advances;
    uint32_t target_counter;
    uint16_t predicted_dv;
    ch_prediction_state prediction;
    ch_control_fault fault;
    uint8_t overlay_visible;
    uint8_t interface_locked;
    uint8_t candidate_valid;
} ch_output;

typedef struct {
    uint32_t previous_keys;
    uint32_t blocked_keys;
    uint32_t next_request_id;
    uint32_t next_query_id;
    uint32_t last_counter;
    uint64_t scene_epoch;
    uint64_t source_generation;
    ch_command pending;
    ch_query active_query;
    uint32_t target_counter;
    uint16_t predicted_dv;
    ch_prediction_state prediction;
    ch_control_fault fault;
    ch_runtime_state runtime;
    uint8_t initialized_scene;
    uint8_t counter_valid;
    uint8_t at_native_boundary;
    uint8_t scene_eligible;
    uint8_t pending_l_key;
    uint8_t pending_r_key;
    uint8_t overlay_visible;
    uint8_t interface_locked;
    uint8_t candidate_valid;
    uint8_t need_query;
} ch_controller;

/* Supply the already-held levels at startup so opening a plugin does not turn
   an existing held key into a newly pressed command. */
void ch_controller_init(ch_controller *controller, uint32_t initial_keys);
void ch_controller_update(ch_controller *controller, const ch_input *input,
                          ch_output *output);

/* Bind an asynchronous result to the exact query token. target must still be
   at/after the current counter, within the unambiguous half of uint32 range,
   and at least the query's minimum target. Return 1 on acceptance, else 0.
   This function does not execute, pause, step, resume, or supply a game key. */
int ch_controller_accept_candidate(ch_controller *controller,
                                   const ch_query *query,
                                   uint32_t target_counter,
                                   uint16_t predicted_dv);
int ch_controller_query_failed(ch_controller *controller, const ch_query *query);
/* A menu action may request another read-only query. */
void ch_controller_request_query(ch_controller *controller);

#ifdef __cplusplus
}
#endif

#endif
