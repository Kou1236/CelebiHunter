#ifndef CANDIDATE_TERMINAL_EVAL_H
#define CANDIDATE_TERMINAL_EVAL_H
#include <stdint.h>

/* Actual copied input immediately before original A, under the existing
 * admitted non-DIV clock profile: byte RNG a/s, actual terminal DIV x,
 * retained BGThird bg in 0..2. No inputs or clocks are chosen or written.
 */
#define CQ_TERMINAL_INVALID UINT32_MAX
typedef struct {
    uint32_t sums[2][256];
    uint32_t ready;
} CQTerminalPrepared;
typedef struct {
    uint32_t dv, branch, prefix_sums[2], before_a, before_s;
    uint32_t ordinary_count, ordinary_div[4][2], ordinary_sub[4];
} CQTerminalDetail;

/* One-time histogram translation for fast repeated evaluation of known x.
 * Caller owns the 2052-byte context; no allocation or mutable global state.
 */
int cq_terminal_prepare(CQTerminalPrepared *prepared);
uint32_t terminal_eval(uint32_t a, uint32_t s, uint32_t x, uint32_t bg);
uint32_t cq_terminal_eval_prepared(const CQTerminalPrepared *prepared,
    uint32_t a, uint32_t s, uint32_t x, uint32_t bg);
int cq_terminal_eval_detail(const CQTerminalPrepared *prepared,
    uint32_t a, uint32_t s, uint32_t x, uint32_t bg, CQTerminalDetail *detail);
int cq_terminal_is_shiny(uint32_t dv);
const uint32_t *cq_terminal_profile_id(void);
#endif
