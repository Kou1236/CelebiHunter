#include "query.h"

static int valid_waiting(const CQWaitingState *p)
{
    ReleasedDivClock21 checked;
    return p && (p->a | p->s) <= 255u && p->bg < 3u &&
        released_div_boundary21(&p->clock,0u,&checked);
}

int cq_waiting_next(CQWaitingState *state)
{
    CQWaitingState next;
    uint8_t pair[2];
    uint32_t wide;
    if (!valid_waiting(state)) return 0;
    next = *state;
    if (!released_div_step21(&state->clock,pair,&next.clock)) return 0;
    wide = state->a + pair[0];
    next.a = wide & 255u;
    next.s = (state->s - pair[1] - (wide >> 8)) & 255u;
    next.bg = (state->bg + 1u) % 3u;
    *state = next;
    return 1;
}

int cq_find_prepared(const CQWaitingState *source, uint32_t current_elapsed,
    uint32_t player_lead, const CQTerminalPrepared *prepared,
    CQQueryResult *result)
{
    CQQueryResult out = {0};
    CQWaitingState state;
    uint32_t n, min_n, max_n;
    if (!result) return CQ_QUERY_INVALID;
    out.status = CQ_QUERY_INVALID;
    *result = out;
    if (!source || !prepared || prepared->ready != 611u ||
        current_elapsed > 1u || !player_lead) return (int)out.status;
    if (!valid_waiting(source)) {
        result->status = CQ_QUERY_CLOCK_UNSUPPORTED;
        return (int)result->status;
    }
    max_n = current_elapsed + CQ_QUERY_HORIZON;
    if (player_lead >= CQ_QUERY_HORIZON) {
        result->status = CQ_QUERY_OUTSIDE_HORIZON;
        return (int)result->status;
    }
    min_n = current_elapsed + player_lead + 1u;
    if (min_n < 2u) min_n = 2u;
    out.min_n = min_n; out.max_n = max_n;
    state = *source;
    for (n = 0u; ; ++n) {
        if (n >= min_n) {
            uint32_t dv = cq_terminal_eval_prepared(prepared,state.a,state.s,
                state.clock.div,state.bg);
            ++out.checked_candidates;
            if (dv == CQ_TERMINAL_INVALID) {
                out.status = CQ_QUERY_INVALID;
                break;
            }
            if (cq_terminal_is_shiny(dv)) {
                CQTerminalDetail detail;
                if (!cq_terminal_eval_detail(prepared,state.a,state.s,
                    state.clock.div,state.bg,&detail)) {
                    out.status = CQ_QUERY_INVALID;
                    break;
                }
                out.status = CQ_QUERY_OK; out.found = 1u;
                out.effective_n = n; out.raw_press_n = n-1u;
                out.div_x = state.clock.div; out.branch = detail.branch;
                out.predicted_dv = dv;
                out.expected_wait_add = state.a; out.expected_wait_sub = state.s;
                out.expected_bg = state.bg; out.target_clock = state.clock;
                break;
            }
        }
        if (n == max_n) {
            out.status = CQ_QUERY_NO_FUTURE_SHINY;
            break;
        }
        if (!cq_waiting_next(&state)) {
            out.status = CQ_QUERY_CLOCK_UNSUPPORTED;
            break;
        }
        ++out.projected_units;
    }
    *result = out;
    return (int)out.status;
}

int cq_find(const CQWaitingState *source, uint32_t current_elapsed,
    uint32_t player_lead, CQQueryResult *result)
{
    CQTerminalPrepared prepared;
    if (!cq_terminal_prepare(&prepared)) {
        CQQueryResult out = {0}; out.status = CQ_QUERY_INVALID;
        if (result) *result = out;
        return CQ_QUERY_INVALID;
    }
    return cq_find_prepared(source,current_elapsed,player_lead,&prepared,result);
}
