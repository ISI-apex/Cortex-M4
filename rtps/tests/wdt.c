#include <stdint.h>

#include "console.h"
#include "arm.h"
#include "gic.h"
#include "hwinfo.h"
#include "sleep.h"
#include "watchdog.h"
#include "panic.h"
#include "subsys.h"
#include "wdt.h"

#include "test.h"

#define NUM_STAGES 2

#define WDT_FREQ_HZ WDT_MIN_FREQ_HZ // this has to match choice in TRCH

#define WDT_CYCLES_TO_MS(c) ((c) / (WDT_FREQ_HZ / 1000))

static void wdt_tick(struct wdt *wdt, unsigned stage, void *arg)
{
    volatile unsigned *expired = arg;
    printf("wdt test: expired\r\n");

    // convention in driver is zero-based, but here is one-based, because we
    // want 0 to indicate no stage expired
    *expired = stage + 1;
}

static bool check_expiration(unsigned expired_stage, unsigned expected)
{
    if (expired_stage != expected) {
        printf("wdt test: unexpected expired stage: %u != %u\r\n",
                expired_stage, expected);
        return false;
    }
    printf("wdt test: expected expired stage: %u == %u\r\n",
           expired_stage, expected);
    return true;
}

int test_wdt(struct wdt **wdt_ptr)
{
    struct wdt *wdt;
    int rc = 1;

    // Each timer can be tested only from its associated core
    unsigned core = self_core_id();
    ASSERT(core < RTPS_R52_NUM_CORES);

    uintptr_t base = core ?
        WDT_RTPS_R52_1_RTPS_BASE : WDT_RTPS_R52_0_RTPS_BASE;

    volatile unsigned expired_stage = 0;
    wdt = wdt_create_target("RTPS_R52", base, wdt_tick, (void *)&expired_stage);
    if (!wdt)
        return 1;
    *wdt_ptr = wdt;

    wdt_enable(wdt);

    gic_int_enable(PPI_IRQ__WDT, GIC_IRQ_TYPE_PPI, GIC_IRQ_CFG_LEVEL);

    unsigned timeouts[] = { wdt_timeout(wdt, 0), wdt_timeout(wdt, 1) };
    unsigned interval = WDT_CYCLES_TO_MS(timeouts[0]);
    printf("TEST WDT: interval %u ms\r\n", interval);

    msleep(interval + interval / 4);
    if (!check_expiration(expired_stage, 1)) goto cleanup;
    wdt_kick(wdt);

    expired_stage = 0;

    unsigned total_timeout =
        WDT_CYCLES_TO_MS(timeouts[0]) + WDT_CYCLES_TO_MS(timeouts[1]);
    unsigned kick_interval = interval / 2;
    unsigned runtime = 0;
    while (runtime < total_timeout) {
        wdt_kick(wdt);
        msleep(kick_interval);
        if (!check_expiration(expired_stage, 0)) goto cleanup;
        runtime += kick_interval;
    }
    if (!check_expiration(expired_stage, 0)) goto cleanup;
    rc = 0;

cleanup:
    // NOTE: order is important, since ISR may be called during destroy
    gic_int_disable(PPI_IRQ__WDT, GIC_IRQ_TYPE_PPI);
    wdt_destroy(wdt);
    *wdt_ptr = NULL;
    // NOTE: timer is still running! target subsystem not allowed to disable it
    return rc;
}
