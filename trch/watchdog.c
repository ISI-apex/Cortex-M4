#include <stdint.h>
#include <stdbool.h>

#include "wdt.h"
#include "nvic.h"
#include "panic.h"
#include "reset.h"
#include "hwinfo.h"
#include "boot.h"

#include "watchdog.h"

#define WDT_FREQ_HZ WDT_MIN_FREQ_HZ // our choice

#define MS_TO_WDT_CYCLES(ms) ((ms) * (WDT_FREQ_HZ / 1000))

// Must be global for the standalone tests for WDTs in test-wdt.c
struct wdt *wdts[NUM_CORES] = {0};

#define NUM_STAGES 2 // fixed in HW, but flexible in driver

struct wdt_group {
    unsigned index; // in wdts array
    unsigned num; // num wdts in sequence
    uintptr_t base;
    unsigned as_size;
    unsigned irq_start;
    const char *names[16];
    unsigned timeouts[NUM_STAGES];
};

// Starting index, number given by group->num_cores
static const struct wdt_group wdt_groups[] = {
    [CPU_GROUP_TRCH]       = { 0, TRCH_NUM_CORES,
                               WDT_TRCH_BASE, WDT_TRCH_SIZE,
                               TRCH_IRQ__WDT_TRCH_ST1, { "TRCH" },
                               /* timeouts ms */ { 2000, 5000 } },
    [CPU_GROUP_RTPS_R52]   = { 1, RTPS_R52_NUM_CORES,
                               WDT_RTPS_R52_0_TRCH_BASE, WDT_RTPS_R52_SIZE,
                               TRCH_IRQ__WDT_RTPS_R52_0_ST2,
                               { "RTPS_R52_0", "RTPS_R52_1" },
                               /* timeouts ms */ { 5000, 10000} },
    [CPU_GROUP_RTPS_R52_0] = { 1, /* num cores */ 1,
                               WDT_RTPS_R52_0_TRCH_BASE, WDT_RTPS_R52_SIZE,
                               TRCH_IRQ__WDT_RTPS_R52_0_ST2,
                               { "RTPS_R52_0" },
                               /* timeouts ms */ { 5000, 10000} },
    [CPU_GROUP_RTPS_R52_1] = { 2, /* num cores */ 1,
                               WDT_RTPS_R52_1_TRCH_BASE, WDT_RTPS_R52_SIZE,
                               TRCH_IRQ__WDT_RTPS_R52_1_ST2,
                               { "RTPS_R52_1" },
                               /* timeouts ms */ { 5000, 10000} },
    [CPU_GROUP_RTPS_A53]   = { 3, RTPS_A53_NUM_CORES,
                               WDT_RTPS_A53_TRCH_BASE, WDT_RTPS_A53_SIZE,
                               TRCH_IRQ__WDT_RTPS_A53_ST2,
                               { "RTPS_A53" },
                               /* timeouts ms */ { 5000, 10000} },
    [CPU_GROUP_HPPS]       = { 4, HPPS_NUM_CORES,
                               WDT_HPPS_TRCH_BASE, WDT_HPPS_SIZE,
                               TRCH_IRQ__WDT_HPPS0_ST2,
                               { "HPPS0", "HPPS1", "HPPS2", "HPPS3",
                                 "HPPS4", "HPPS5", "HPPS6", "HPPS7" },
                               /* HPPS Linux clean shutdown (with NAND) takes 360 sec */
                               /* timeouts ms */ { 5000, 400000} },
};

static void handle_timeout(struct wdt *wdt, unsigned stage, void *arg)
{
    enum cpu_group_id gid = (unsigned)arg;
    const struct cpu_group *cpu_group = subsys_cpu_group(gid);
    int rc;

    printf("watchdog: cpu %u: stage %u: expired\r\n", gid, stage);

    if (gid == CPU_GROUP_TRCH) {
            ASSERT(stage == 0); // no last stage interrupt, because wired to hw reset
#if !CONFIG_SYSTICK // If SysTick is enabled, it will kick
            // Note: main loop will return from WFI/WFE and kick too, but in case we
            // are busy with a long-running operation (e.g. loading data from SMC), then
            // main loop won't get a chance to run, so we kick it from here.
            //
            // Kicking on ST1 expiration is somewhat unsatisfactory: if we had
            // a scheduler and a systick, the systick would return control flow
            // to the scheduler loop, kick the WDT from that loop, and then
            // return to task (or context switch to another task). Even so, the
            // WDT would then be monitoring the scheduler, not whether any task
            // is stuck.
            wdt_kick(wdt);
#endif // !CONFIG_SYSTICK
    } else {
        ASSERT(stage == NUM_STAGES - 1); // first stage is handled by the target CPU
        ASSERT(!wdt_is_enabled(wdt)); // HW disables the timer on expiration

        // The purpose of the following logic is to make the reset happen only
        // once, upon the expiration of any WDT timer from the set of timers of
        // a subsystem, instead of initiating a reset in response to the expiration
        // of every timer in the set. The only-once logic is somewhat subtle, because we
        // cannot know when the timers that were going to expire have expired and
        // thus we could start the reboot. We cannot wait for all timers in the
        // set to expire, becsause (1) the subsystem might not have enabled all
        // timers, and (2) the subsystem might be in a half-broken state where
        // it continues to kick some but not all timers.

        rc = reset_assert(cpu_group->cpu_set); // will prevent all CPUs from kicking
        if (rc) {
            printf("ERROR: WATCHDOG: failed to assert reset for cpu set: %x\r\n",
                   cpu_group->cpu_set);
            return;
        }

        // Disable the WDTs for all other cores, so that they don't fire.  We
        // might still get an ISR from timers that already expired before we
        // disabled them, but that's ok: all of those ISRs (as well as the
        // first one), will collectively enqueue only one reboot request. By
        // deiniting the rest of the WDTs in the group, we also disable their
        // ISRs at the NVIC level, so they won't fire. WDTs are re-initialized
        // as part of the boot sequence.
        watchdog_deinit_group(gid);

        // Enqueue the request for the main loop
        // Note: we cannot use the command queue because we want all ISRs
        // in a sequence to collectively generate one request (see above).
        //
        // NOTE: A potentially better alternative design is to reboot
        // the cpu group here instead of the subsystem. Booting the subsystem
        // means that the cpu group (i.e. subsystem's boot mode) is determined
        // by boot config at boot time. This means that a reboot triggered
        // by WDT would switch the mode if boot config changes between the
        // preceding boot and the WDT triggered-reboot. We might want that.
        // If we don't want it, then the boot interface should be changed to
        // only allow booting cpu groups (i.e. booting an OS, i.e. booting
        // into a mode given by the boot command instead of being pulled
        // from the boot config) -- it's a subtle interface difference.
        boot_request(cpu_group->subsys);
    }
}

static struct wdt *create_wdt(const char *name, uintptr_t base, unsigned irq,
                              const unsigned *timeouts, unsigned cpuid)
{
    struct wdt *wdt = wdt_create_monitor(name, base,
                                         handle_timeout, (void *)cpuid,
                                         WDT_CLK_FREQ_HZ, WDT_MAX_DIVIDER);
    if (!wdt)
        return NULL;

    // Might be already enabled if TRCH reboot's while other subsys is running
    // TODO: reset semantics not known, perhpas all WDTs are reset as TRCH
    // peripherals on the reset signal from the TRCH WDT ST2 expiration. For
    // now, assume that this is not the case.
    bool enabled = wdt_is_enabled(wdt);

    if (enabled)
        wdt_disable(wdt);

    uint64_t timeouts_cycles[NUM_STAGES];
    printf("WATCHDOG: create: timeouts ");
    for (int i = 0; i < NUM_STAGES; ++i) {
        timeouts_cycles[i] = MS_TO_WDT_CYCLES(timeouts[i]);
        printf("%u ms (%u cyc) ", timeouts[i], timeouts_cycles[i]);
    }
    printf("\r\n");

    int rc = wdt_configure(wdt, WDT_FREQ_HZ, NUM_STAGES, timeouts_cycles);
    if (rc) {
	wdt_destroy(wdt);
	return NULL;
    }

    if (enabled)
        wdt_enable(wdt);

    nvic_int_enable(irq);
    return wdt;
}

static void destroy_wdt(struct wdt *wdt, unsigned irq)
{
    // NOTE: order: ISR might be called during destroy if IRQ is not disabled
    nvic_int_disable(irq);
    wdt_disable(wdt);
    wdt_destroy(wdt);
}

void watchdog_init_group(enum cpu_group_id gid)
{
    const struct wdt_group *wdtg = &wdt_groups[gid];
    for (unsigned i = 0; i < wdtg->num;  ++i) {
        struct wdt *wdt = create_wdt(wdtg->names[i],
                                     (wdtg->base + i * wdtg->as_size),
                                     wdtg->irq_start + i, wdtg->timeouts, gid);
        wdts[wdtg->index + i] = wdt;
    }
}

void watchdog_deinit_group(enum cpu_group_id gid)
{
    const struct wdt_group *wdtg = &wdt_groups[gid];
    for (unsigned i = 0; i < wdtg->num;  ++i) {
        wdt_disable(wdts[wdtg->index + i]);
        destroy_wdt(wdts[wdtg->index + i], wdtg->irq_start + i);
        wdts[wdtg->index + i] = NULL;
    }
}

static void watchdog_do(comp_t cpus, void (*do_cb)(struct wdt *))
{
    if (cpus & COMP_CPU_TRCH)
        do_cb(wdts[wdt_groups[CPU_GROUP_TRCH].index]);

    if (cpus & COMP_CPUS_RTPS_R52) {
        for (unsigned i = 0; i < RTPS_R52_NUM_CORES;  ++i)
            if (cpus & (1 << (COMP_CPUS_SHIFT_RTPS_R52 + i)))
                do_cb(wdts[wdt_groups[CPU_GROUP_RTPS_R52].index + i]);
    }

    if (cpus & COMP_CPUS_RTPS_A53) {
        for (unsigned i = 0; i < RTPS_A53_NUM_CORES;  ++i)
            if (cpus & (1 << (COMP_CPUS_SHIFT_RTPS_A53 + i)))
                do_cb(wdts[wdt_groups[CPU_GROUP_RTPS_A53].index + i]);
    }

    if (cpus & COMP_CPUS_HPPS) {
        for (unsigned i = 0; i < HPPS_NUM_CORES;  ++i)
            if (cpus & (1 << (COMP_CPUS_SHIFT_HPPS + i)))
                do_cb(wdts[wdt_groups[CPU_GROUP_HPPS].index + i]);
    }
}

void watchdog_start(comp_t cpus)
{
    watchdog_do(cpus, wdt_enable);
}
void watchdog_stop(comp_t cpus)
{
    watchdog_do(cpus, wdt_disable);
}
void watchdog_kick(comp_t cpus)
{
    watchdog_do(cpus, wdt_kick);
}
void watchdog_start_group(enum cpu_group_id gid)
{
    watchdog_start(subsys_cpu_group(CPU_GROUP_TRCH)->cpu_set);
}
void watchdog_stop_group(enum cpu_group_id gid)
{
    watchdog_stop(subsys_cpu_group(CPU_GROUP_TRCH)->cpu_set);
}
void watchdog_kick_group(enum cpu_group_id gid)
{
    watchdog_kick(subsys_cpu_group(CPU_GROUP_TRCH)->cpu_set);
}

void wdt_trch_st1_isr()
{
    wdt_isr(wdts[wdt_groups[CPU_GROUP_TRCH].index], /* stage */ 0);
}

#define WDT_ISR(idx) void wdt_ ## idx ## _st2_isr() { wdt_isr(wdts[idx], 1); }

WDT_ISR(1)
WDT_ISR(2)
WDT_ISR(3)
WDT_ISR(4)
WDT_ISR(5)
WDT_ISR(6)
WDT_ISR(7)
WDT_ISR(8)
WDT_ISR(9)
WDT_ISR(10)
WDT_ISR(11)

#if 12 != NUM_CORES
#error WDT ISRs not defined for all cores
#endif // NUM_CORES
