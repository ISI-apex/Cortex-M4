#ifndef LIB_SWTIMER_H
#define LIB_SWTIMER_H

#include <stdint.h>
#include <stdbool.h>

#include "list.h"

typedef void (sw_timer_cb_t)(void *arg);

enum sw_timer_type {
    SW_TIMER_PERIODIC,
    SW_TIMER_ONESHOT,
};

struct sw_timer {
    sw_timer_cb_t *cb;
    void *arg;
    bool periodic;
    uint32_t interval; /* cycles @ clk */
    uint32_t next;
    struct lnode node;
};

void sw_timer_init(uint32_t freq);
void sw_timer_tick(uint32_t delta_cycles);
void sw_timer_run(); /* call this periodically from main loop */

void sw_timer_schedule(struct sw_timer *tmr, uint32_t interval_ms,
                       enum sw_timer_type type, sw_timer_cb_t *cb, void *arg);
void sw_timer_cancel(struct sw_timer *tmr);

#endif /* LIB_SWTIMER_H */
