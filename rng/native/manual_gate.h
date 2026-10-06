#ifndef CELEBI_MANUAL_GATE_H
#define CELEBI_MANUAL_GATE_H
#include <stdint.h>
#include "../controller/manual_controller.h"

/* Runs only on the native emulator thread. A render callback is not a boundary.
 * No HID write/filter, provider call, target chasing, or timer-based advance.
 */
typedef struct {
    uint32_t r[13], lr, original_sp, cpsr, fpscr, reserved;
    uint32_t d_words[32], chain_pc, chain_scratch;
} ChNativeContext;
typedef struct {
    ch_runtime_state runtime;
    uint32_t counter, completed_request_id, last_request_id, step_goal;
    uint64_t scene_epoch;
    uint32_t pending_marker, marker_calls, scheduler_calls, boundary_calls;
    uint32_t fault, has_boundary, pause_request, counter_valid;
} ChManualGate;
typedef struct {
    uint32_t chain_pc, is_boundary, hold_game_thread, fault;
} ChGateRoute;
enum {
    CH_GATE_OK=0, CH_GATE_BAD_ROUTE=1, CH_GATE_NESTED_MARKER=2,
    CH_GATE_COUNTER_DISCONTINUITY=3, CH_GATE_STEP_OVERSHOT=4,
    CH_GATE_STALE_COMMAND=5, CH_GATE_REPEATED_COMMAND=6
};
void ch_gate_init(ChManualGate *gate);
ChGateRoute ch_gate_route(ChManualGate *gate, uint32_t saved_lr,
                         uint32_t native_counter, uint64_t scene_epoch);
/* Only dispatch the physical-edge command returned by ch_controller_update.
 * 1 accepted, 0 rejected. A rejected request performs no control action.
 */
int ch_gate_command(ChManualGate *gate, const ch_command *command);
void ch_gate_input(const ChManualGate *gate, ch_input *input);

/* ARM bridge is read-only with respect to the saved native context. Callback
 * returns the exact original callee. It may wait only for a player-requested
 * pause; its polling/UI/worker services must keep running while game is held.
 */
typedef uint32_t (*ChNativeCallback)(const ChNativeContext *);
extern ChNativeCallback ch_native_callback;
void ch_native_bridge(void);
#endif
