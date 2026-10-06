#ifndef RELEASED_DIV_PREDICTION21_H
#define RELEASED_DIV_PREDICTION21_H
#include <stdint.h>

/* Snapshot at a released, aligned scheduler; these are native cost units.
 * Captured DIV remains real. Native no-reset regular settlements are required.
 * The released loop has154 lines *114 cost units =17556 per completed unit.
 * An arbitrary partial source or encounter does not satisfy this contract.
 * Also bind/check source TAC=4,TMA=0,IME=1,mode=HALT,hVBlank&7=0,
 * (IF&IE&1)!=0, LCD type1/subphase0/LY144/STATmode1.
 * Normal VBlank wins the IRQ priority even when timer IF4 is pending;
 * its IME=0 prefix cannot be preempted by a TIMA overflow. TIMA changes
 * the pending timer flag but not the timer deadline or DIV read reducer. */
typedef struct {
    uint32_t div,div_countdown,timer_phase,budget,lcd_countdown;
} ReleasedDivClock21;

static inline int released_div_boundary21(const ReleasedDivClock21 *origin,
    uint32_t elapsed_units,ReleasedDivClock21 *out) {
    uint64_t cost,ticks;
    uint32_t phase,countdown,budget;
    if(!origin||!out||origin->div>255u||origin->div_countdown<1u||
       origin->div_countdown>64u||origin->timer_phase>255u||
       origin->lcd_countdown!=113u||origin->budget!=
       (origin->div_countdown<origin->timer_phase+1u?
        origin->div_countdown:origin->timer_phase+1u))return 0;
    cost=17556ull*elapsed_units;
    ticks=(cost+64u-origin->div_countdown)/64u;
    countdown=(uint32_t)((origin->div_countdown-1u+64u-(cost&63u))&63u)+1u;
    phase=(uint32_t)((origin->timer_phase+256u-(cost&255u))&255u);
    budget=countdown<phase+1u?countdown:phase+1u;
    out->div=(origin->div+(uint32_t)(ticks&255u))&255u;
    out->div_countdown=countdown;out->timer_phase=phase;out->budget=budget;
    out->lcd_countdown=origin->lcd_countdown;
    return 1;
}

/* Literal fixed Normal VBlank ROM prefix. DIV is read before this LDH's own
 * three-cost writeback. Charged opcodes settle only when pending>=budget.
 * No observed read-to-IO offset is used. This short domain excludes LCD edges. */
static inline int released_normal_pair21(const ReleasedDivClock21 *boundary,
    uint8_t out[2]) {
    static const uint8_t costs[25]={4,4,4,4,4,3,2,1,2,3,2,2,2,2,1,6,1,3,3,3,1,3,1,3,3};
    uint32_t div,countdown,phase,budget,lcd,pending=0u,k,reads=0u;
    if(!boundary||!out||boundary->div>255u||boundary->div_countdown<1u||
       boundary->div_countdown>64u||boundary->timer_phase>255u||
       boundary->lcd_countdown<65u||boundary->lcd_countdown>114u)return 0;
    div=boundary->div;countdown=boundary->div_countdown;phase=boundary->timer_phase;
    lcd=boundary->lcd_countdown;
    budget=countdown<phase+1u?countdown:phase+1u;
    if(lcd<budget)budget=lcd;
    if(boundary->budget!=budget)return 0;
    for(k=0u;k<25u;k++){
        if(k==19u||k==24u){out[reads++]=(uint8_t)div;if(reads==2u)return 1;}
        pending+=costs[k];
        if(pending>=budget){
            if(pending>=lcd||pending>=countdown+64u)return 0;
            if(pending>=countdown){div=(div+1u)&255u;countdown=countdown+64u-pending;}
            else countdown-=pending;
            phase=(phase+256u-pending)&255u;lcd-=pending;
            budget=countdown<phase+1u?countdown:phase+1u;
            if(lcd<budget)budget=lcd;
            pending=0u;
        }
    }
    return 0;
}

/* Current seed is immediately before a complete released unit. The returned
 * pair updates RNG once; next is the scheduler seed after that same unit.
 * Thus n steps produce effective-A boundary origin_counter+n, with its real
 * IO DIV. A later terminal normalization is outside this waiting projection. */
static inline int released_div_step21(const ReleasedDivClock21 *current,
    uint8_t pair[2],ReleasedDivClock21 *next) {
    ReleasedDivClock21 after;
    if(!next||!released_normal_pair21(current,pair)||
       !released_div_boundary21(current,1u,&after))return 0;
    *next=after;
    return 1;
}
#endif
