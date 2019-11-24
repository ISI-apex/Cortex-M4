#define DEBUG 0

#include <stdint.h>
#include <stdbool.h>

#include "console.h"
#include "list.h"
#include "mem.h"
#include "oop.h"
#include "panic.h"

#include "swtimer.h"

static uint32_t clk; /* Hz */
static uint32_t time; /* cycles @ clk Hz */

/* Sorted by next event time */
/* for efficiency, could be a min-heap; for simplicity: sorted linked list */
static struct lnode timers;

void sw_timer_schedule(struct sw_timer *tmr, uint32_t interval_ms,
                       enum sw_timer_type type, sw_timer_cb_t *cb, void *arg)
{
    ASSERT(tmr);
    ASSERT(cb);

    tmr->cb = cb;
    tmr->arg = arg;
    tmr->periodic = (type == SW_TIMER_PERIODIC);
    tmr->interval = interval_ms * (clk / 1000);
    tmr->next = time + tmr->interval;
    printf("SW TMR: sched @%p interval %u periodic %u: next %u\r\n",
           tmr, tmr->interval, tmr->periodic, tmr->next);
    list_insert(&tmr->node, &timers);
}

void sw_timer_cancel(struct sw_timer *tmr)
{
    printf("SW TMR: cancel @%p\r\n", tmr);
    list_remove(&tmr->node);
}

void sw_timer_init(uint32_t freq)
{
    printf("SW TMR: init clk freq %u\r\n", freq);
    bzero(&timers, sizeof(timers));
    clk = freq;
    time = 0;
}

void sw_timer_tick(uint32_t delta_cycles)
{
    time += delta_cycles;
    DPRINTF("SW TMR: tick: time += %u -> %u\r\n", delta_cycles, time);
}

void sw_timer_run()
{
    struct lnode *node = &timers;
    while ((node = node->next)) {
        struct sw_timer *tmr = container_of(struct sw_timer, node, node);
        ASSERT(tmr);
        if (tmr->next <= time) {
            DPRINTF("SW TMR: expired @%p\r\n", tmr);
            tmr->cb(tmr->arg);
            if (tmr->periodic)
                tmr->next = time + tmr->interval;
            else
                sw_timer_cancel(tmr);
        }
    }
}
