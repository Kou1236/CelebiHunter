#ifndef CANDIDATE_WAITING_QUERY21_H
#define CANDIDATE_WAITING_QUERY21_H
#include <stdint.h>
#include "terminal_eval.h"
#include "../candidate-clock/released_div_prediction.h"

#define CQ_QUERY_HORIZON 65536u
typedef enum {
    CQ_QUERY_OK = 0,
    CQ_QUERY_INVALID = 1,
    CQ_QUERY_CLOCK_UNSUPPORTED = 2,
    CQ_QUERY_NO_FUTURE_SHINY = 3,
    CQ_QUERY_OUTSIDE_HORIZON = 4
} CQQueryStatus;

/* Actual copied state at one aligned released scheduler. Caller binds the
 * identity and counter of this seed to the current job. No state is written
 * back to native memory. Query seed can lag its query counter by at most one
 * original unit; current_elapsed is query_counter-seed_counter modulo 2^32.
 */
typedef struct CQWaitingState {
    uint32_t a, s, bg;
    ReleasedDivClock21 clock;
} CQWaitingState;

typedef struct CQQueryResult {
    uint32_t status, found, effective_n, raw_press_n;
    uint32_t div_x, branch, predicted_dv;
    uint32_t expected_wait_add, expected_wait_sub, expected_bg;
    uint32_t min_n, max_n, checked_candidates, projected_units;
    ReleasedDivClock21 target_clock;
} CQQueryResult;

/* Project exactly one original released unit, with its actual Normal pair.
 * Return 1 on success. Failure leaves state unchanged.
 */
int cq_waiting_next(CQWaitingState *state);

/* Search the first actual projected shiny in
 * [max(2,current_elapsed+player_lead+1), current_elapsed+65536].
 * effective_n is relative to the actual seed; raw_press_n=effective_n-1.
 * Caller adds these to seed_counter, then binds them to its origin identity.
 * Each candidate uses the ONE clock-projected DIV and ONE actual item branch.
 * No free DIV selection, encounter replay, allocation, or native I/O.
 */
int cq_find(const CQWaitingState *source, uint32_t current_elapsed,
    uint32_t player_lead, CQQueryResult *result);
int cq_find_prepared(const CQWaitingState *source, uint32_t current_elapsed,
    uint32_t player_lead, const CQTerminalPrepared *prepared,
    CQQueryResult *result);
#endif
