#include "terminal_eval.h"
#include "../data/profile_data.h"

/* Original controlled ordinary Random operands, BG2 versus BG0/1.
 * Source: existing inverse_frontier.c/test_bg_inverse.c, unchanged.
 */
static const uint8_t ordinary[2][3][4][2] = {
    {{{254,254},{3,4},{6,6}},
     {{254,254},{0,0},{6,6},{8,8}},
     {{254,254},{0,0},{6,6},{8,8}}},
    {{{254,255},{4,4},{6,6}},
     {{254,255},{1,1},{6,6},{8,8}},
     {{254,255},{1,1},{6,6},{8,8}}}
};

static uint32_t scalar_sum(uint32_t x, uint32_t side)
{
    uint32_t k, total = 0u;
    for (k = 0; k < 256u; ++k)
        total += sol61_prefix_hist[side][k] * ((x + k) & 255u);
    return total;
}

int cq_terminal_prepare(CQTerminalPrepared *p)
{
    uint32_t side, k;
    if (!p) return 0;
    p->ready = 0u;
    for (side = 0; side < 2u; ++side) {
        uint32_t total = 0u, count = 0u;
        for (k = 0; k < 256u; ++k) {
            total += sol61_prefix_hist[side][k] * k;
            count += sol61_prefix_hist[side][k];
        }
        if (count != 611u) return 0;
        for (k = 0; k < 256u; ++k) {
            p->sums[side][k] = total;
            total += count - 256u * sol61_prefix_hist[side][255u-k];
        }
    }
    p->ready = 611u;
    return 1;
}

static uint32_t random_pair(uint32_t *a, uint32_t *s,
    uint32_t first, uint32_t second)
{
    uint32_t wide = *a + first;
    *a = wide & 255u;
    *s = (*s - second - (wide >> 8)) & 255u;
    return *s;
}

static uint32_t evaluate(uint32_t a, uint32_t s, uint32_t x, uint32_t bg,
    uint32_t sum_add, uint32_t sum_sub, CQTerminalDetail *detail)
{
    uint32_t axis = bg == 2u ? 0u : 1u, branch = 0u;
    uint32_t wide = a + sum_add, index = 0u, count, first, second, dv1, dv2;
    CQTerminalDetail d = {0};
    d.prefix_sums[0] = sum_add; d.prefix_sums[1] = sum_sub;
    a = wide & 255u; s = (s - sum_sub - (wide >> 8)) & 255u;
    d.before_a = a; d.before_s = s;
    first = (x + ordinary[axis][0][0][0]) & 255u;
    second = (x + ordinary[axis][0][0][1]) & 255u;
    d.ordinary_div[0][0] = first; d.ordinary_div[0][1] = second;
    d.ordinary_sub[0] = random_pair(&a,&s,first,second);
    index = 1u;
    if (s >= 192u) {
        first = (x + ordinary[axis][1][1][0]) & 255u;
        second = (x + ordinary[axis][1][1][1]) & 255u;
        d.ordinary_div[index][0] = first;
        d.ordinary_div[index][1] = second;
        d.ordinary_sub[index] = random_pair(&a,&s,first,second);
        ++index;
        branch = s < 20u ? 2u : 1u;
    }
    count = sol61_ordinary_count[branch];
    first = (x + ordinary[axis][branch][index][0]) & 255u;
    second = (x + ordinary[axis][branch][index][1]) & 255u;
    d.ordinary_div[index][0] = first; d.ordinary_div[index][1] = second;
    dv1 = random_pair(&a,&s,first,second); d.ordinary_sub[index] = dv1;
    ++index;
    first = (x + ordinary[axis][branch][index][0]) & 255u;
    second = (x + ordinary[axis][branch][index][1]) & 255u;
    d.ordinary_div[index][0] = first; d.ordinary_div[index][1] = second;
    dv2 = random_pair(&a,&s,first,second); d.ordinary_sub[index] = dv2;
    d.dv = (dv1 << 8) | dv2; d.branch = branch; d.ordinary_count = count;
    if (detail) *detail = d;
    return d.dv;
}

uint32_t terminal_eval(uint32_t a, uint32_t s, uint32_t x, uint32_t bg)
{
    if ((a | s | x) > 255u || bg > 2u) return CQ_TERMINAL_INVALID;
    return evaluate(a,s,x,bg,scalar_sum(x,0u),scalar_sum(x,1u),0);
}

uint32_t cq_terminal_eval_prepared(const CQTerminalPrepared *p,
    uint32_t a, uint32_t s, uint32_t x, uint32_t bg)
{
    if (!p || p->ready != 611u || (a | s | x) > 255u || bg > 2u)
        return CQ_TERMINAL_INVALID;
    return evaluate(a,s,x,bg,p->sums[0][x],p->sums[1][x],0);
}

int cq_terminal_eval_detail(const CQTerminalPrepared *p,
    uint32_t a, uint32_t s, uint32_t x, uint32_t bg, CQTerminalDetail *detail)
{
    if (!detail || !p || p->ready != 611u || (a | s | x) > 255u || bg > 2u)
        return 0;
    (void)evaluate(a,s,x,bg,p->sums[0][x],p->sums[1][x],detail);
    return 1;
}

int cq_terminal_is_shiny(uint32_t dv)
{
    return dv <= 65535u && (dv & 0xfffu) == 0xaaau && (dv & 0x2000u) != 0u;
}

const uint32_t *cq_terminal_profile_id(void)
{
    return sol61_profile_id;
}
