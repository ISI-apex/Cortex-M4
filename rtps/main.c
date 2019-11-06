#define DEBUG 0

#include <stdbool.h>
#include <stdint.h>

#include "arm.h"
#include "command.h"
#include "console.h"
#include "dma.h"
#include "float.h"
#include "gic.h"
#include "gtimer.h"
#include "hwinfo.h"
#include "intc.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "mailbox.h"
#include "panic.h"
#include "printf.h"
#include "rti-timer.h"
#include "server.h"
#include "sleep.h"
#include "test.h"
#include "watchdog.h"
#include "mutex.h"
#include "psci.h"

extern void enable_caches(void);

#define SYS_TICK_INTERVAL_MS 500
#define MAIN_LOOP_SILENT_ITERS 16

static enum gtimer sys_timer = GTIMER_PHYS;
static uint32_t sys_timer_interval; // in cycles

// Main is the owner of these pointers because the ISR accesses them
static struct rti_timer *rti_timer; // only one since this BM code is not SMP
#if TEST_RTPS_DMA
static struct dma *rtps_dma;
#endif // TEST_RTPS_DMA
#if CONFIG_WDT || TEST_WDT
static struct wdt *wdt;
#endif // {CONFIG,TEST}_WDT

// inferred CONFIG settings
#define CONFIG_MBOX_DEV_HPPS (CONFIG_HPPS_RTPS_MAILBOX)
#define CONFIG_MBOX_DEV_LSIO 0 // TODO: not currently used

void enable_interrupts (void)
{
	unsigned long temp;
	__asm__ __volatile__("mrs %0, cpsr\n"
			     "bic %0, %0, #0x80\n"
			     "msr cpsr_c, %0"
			     : "=r" (temp)
			     :
			     : "memory");
}

void soft_reset (void) 
{
	unsigned long temp;
	__asm__ __volatile__("mov r1, #2\n"
			     "mcr p15, 4, r1, c12, c0, 2\n"); 
}

#if CONFIG_GTIMER
static void sys_tick(void *arg)
{
    int32_t tval = gtimer_get_tval(sys_timer); // negative value, time since last tick
    gtimer_set_tval(sys_timer, sys_timer_interval); // schedule the next tick

#if CONFIG_SLEEP_TIMER
    sleep_tick(sys_timer_interval + (-tval));
#endif // CONFIG_SLEEP_TIMER
}
#endif // CONFIG_GTIMER

#if CONFIG_SMP
static unsigned int smp_core1_awake = 0;
static uint32_t smp_mutex = unlocked; /* TODO: necessary? */

static int bringup_secondary_cores(struct link *psci_link)
{
    int rc;

    printf("RTPS-R52 core 0: asking TRCH to reset core 1...\r\n");
    rc = psci_release_reset(psci_link, COMP_CPU_RTPS_R52_0,
                            COMP_CPU_RTPS_R52_1);
    if (rc) {
        printf("ERROR: PSCI request to release core 1 failed\r\n");
        return 1;
    }

    printf("RTPS-R52 core 0: waiting for core 1...\r\n");
    while (1) {
        asm volatile ("dmb "); /* TODO: necessary? */
        lock_mutex(&smp_mutex); /* TODO: necessary? */
        if (smp_core1_awake)
            break;
        unlock_mutex(&smp_mutex);
    }
    unlock_mutex(&smp_mutex);
    printf("RTPS-R52 core 0: core 1 is awake\r\n");
    return 0;
}

static int main_secondary(void)
{
    /* Don't use UART (printf/panic/etc) from here because conflicts Core 0 */

    /* RTPS-1 is up and about to wake up RTPS-0 */
    lock_mutex(&smp_mutex); /* TODO: necessary/ */
    smp_core1_awake = 1;
    asm volatile ("dmb "); /* TODO: necessary? */
    unlock_mutex(&smp_mutex);

    while(1) {
        asm volatile ("wfi");
    };
    return 0;
}
#endif /* CONFIG_SMP */

static int main_primary(void)
{
    unsigned core = self_core_id();
    ASSERT(core < RTPS_R52_NUM_CORES);

    console_init();
    printf("\r\n\r\nRTPS (on CPU %u)\r\n", core);

    enable_caches();
    enable_interrupts();

    /* Not clear what happens to GIC interface to core 1 in lockstep mode */
    gic_init(RTPS_GIC_BASE, RTPS_R52_NUM_CORES);

    sleep_set_busyloop_factor(RTPS_R52_BUSYLOOP_FACTOR);

#if TEST_GTIMER
    if (test_gtimer())
        panic("gtimer test");
#endif // TEST_GTIMER

#if CONFIG_GTIMER
    uint32_t sys_timer_clk = gtimer_get_frq();
    if (sys_timer_clk == 0)
        panic("system counter freq register was not initialized by bootloader");
    sys_timer_interval = SYS_TICK_INTERVAL_MS * (sys_timer_clk / 1000);
    gtimer_set_tval(sys_timer, sys_timer_interval);
    gtimer_subscribe(sys_timer, sys_tick, NULL);
    gic_int_enable(PPI_IRQ__TIMER_PHYS, GIC_IRQ_TYPE_PPI, GIC_IRQ_CFG_LEVEL);
    gtimer_start(sys_timer);

#if CONFIG_SLEEP_TIMER
    sleep_set_clock(sys_timer_clk);
#endif // CONFIG_SLEEP_TIMER
#endif // CONFIG_GTIMER

#if TEST_RTI_TIMER
    // Test only one timer, because each timer can only be be tested from its
    // associated core, and the SMP mode doesn't support running tests yet.
    if (test_core_rti_timer(&rti_timer))
        panic("RTI Timer test");
#endif // TEST_RTI_TIMER

#if TEST_FLOAT
    if (test_float())
        panic("float test");
#endif // TEST_FLOAT

#if TEST_SORT
    if (test_sort())
        panic("sort test");
#endif // TEST_SORT

#if TEST_RT_MMU
    if (test_rt_mmu())
        panic("TRCH/RTPS->HPPS MMU test");
#endif // TEST_RT_MMU

#if TEST_RTPS_MMU
    if (test_rtps_mmu())
        panic("RTPS MMU test");
#endif // TEST_RT_MMU

#if TEST_RTPS_DMA
    if (test_rtps_dma(&rtps_dma))
        panic("RTPS DMA test");
#endif // TEST_RTPS_DMA

#if TEST_RTPS_TRCH_MAILBOX
    if (test_rtps_trch_mailbox())
        panic("RTPS->TRCH mailbox test");
#endif // TEST_RTPS_TRCH_MAILBOX

#if CONFIG_RTPS_TRCH_MAILBOX
#define LSIO_RCV_IRQ_IDX  MBOX_LSIO__RTPS_RCV_INT
#define LSIO_ACK_IRQ_IDX  MBOX_LSIO__RTPS_ACK_INT
    struct mbox_link_dev mldev_trch;
    mldev_trch.base = MBOX_LSIO__BASE;
    mldev_trch.rcv_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + LSIO_RCV_IRQ_IDX,
                               GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_trch.rcv_int_idx = LSIO_RCV_IRQ_IDX;
    mldev_trch.ack_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + LSIO_ACK_IRQ_IDX,
                               GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_trch.ack_int_idx = LSIO_ACK_IRQ_IDX;

    struct link *trch_link;
    switch (core) {
        case 0:
            trch_link = mbox_link_connect("RTPS_TRCH_MBOX_LINK",
                &mldev_trch,
                MBOX_LSIO__TRCH_RTPS_R52_0, MBOX_LSIO__RTPS_R52_0_TRCH,
                /* server */ 0, /* client */ MASTER_ID_RTPS_CPU0);
            break;
        case 1:
            trch_link = mbox_link_connect("RTPS_TRCH_MBOX_LINK",
                &mldev_trch,
                MBOX_LSIO__TRCH_RTPS_R52_1, MBOX_LSIO__RTPS_R52_1_TRCH,
                /* server */ 0, /* client */ MASTER_ID_RTPS_CPU1);
            break;
        default:
            panic("invalid RTPS R52 core ID");
    }
    if (!trch_link)
        panic("RTPS->TRCH mailbox");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */

#if CONFIG_MBOX_DEV_HPPS
    struct mbox_link_dev mldev_hpps;
    mldev_hpps.base = MBOX_HPPS_RTPS__BASE;
    mldev_hpps.rcv_irq =
        gic_request(RTPS_IRQ__HR_MBOX_0 + MBOX_HPPS_RTPS__RTPS_RCV_INT,
                    GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_hpps.rcv_int_idx = MBOX_HPPS_RTPS__RTPS_RCV_INT;
    mldev_hpps.ack_irq =
        gic_request(RTPS_IRQ__HR_MBOX_0 + MBOX_HPPS_RTPS__RTPS_ACK_INT,
                    GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_hpps.ack_int_idx = MBOX_HPPS_RTPS__RTPS_ACK_INT;
    mbox_link_dev_add(MBOX_DEV_HPPS, &mldev_hpps);
#endif

#if CONFIG_HPPS_RTPS_MAILBOX
    struct link *hpps_link = mbox_link_connect("HPPS_MBOX_LINK", &mldev_hpps,
                    MBOX_HPPS_RTPS__HPPS_RTPS, MBOX_HPPS_RTPS__RTPS_HPPS,
                    /* server */ MASTER_ID_RTPS_CPU0,
                    /* client */ MASTER_ID_HPPS_CPU0);
    if (!hpps_link)
        panic("HPPS link");
    // Never release the link, because we listen on it in main loop
#endif // CONFIG_HPPS_RTPS_MAILBOX

#if TEST_SOFT_RESET
    printf("Resetting...\r\n");
    /* this will generate "Undefined Instruction exception because HRMR is accessible only at EL2 */
    soft_reset();
    printf("ERROR: reached unrechable code: soft reset failed\r\n");
#endif // TEST_SOFT_RESET

#if TEST_WDT
    if (test_wdt(&wdt))
        panic("wdt test");
    // NOTE: watchdog remains enabled after this test, not allowed to disable
#endif // TEST_WDT

#if CONFIG_WDT
    watchdog_init(&wdt);
#endif // CONFIG_WDT

    cmd_handler_register(server_process);

#if CONFIG_SMP
    int rc = bringup_secondary_cores(trch_link);
    if (rc)
        panic("failed to bring up secondary cores");
#endif /* CONFIG_SMP */

    unsigned iter = 0;
    while (1) {
        bool verbose = iter++ % MAIN_LOOP_SILENT_ITERS == 0;
        if (verbose)
            printf("RTPS: main loop\r\n");

#if CONFIG_WDT
        // Kicking from here is insufficient, because we sleep. There are two
        // ways to complete:
        //     (A) have TRCH disable the watchdog in response to the WFI output
        //     signal from the core,
        //     (B) have a scheduler (with a tick interval shorter than the
        //     watchdog timeout interval) and kick from the scheuduler tick, or
        //     (C) kick on return from WFI/WFI (which could be as a result of
        //     either first stage timeout IRQ or the system timer tick IRQ).
        // At this time, we can do either (B) or (C): (B) has the disadvantage
        // that what is being monitored is the systick ISR, and not the main
        // loop proper (so if any ISRs starve the main loop, that won't be
        // detected), and (C) has the disadvantage that if the main loop
        // performs long actions, those actions need to kick. We go with (C).
        watchdog_kick();
#endif // CONFIG_WDT

        struct cmd cmd;
        while (!cmd_dequeue(&cmd)) {
            cmd_handle(&cmd);
            verbose = true; // to end log with 'waiting' msg
        }

        int_disable(); // the check and the WFI must be atomic
        if (!cmd_pending()) {
            if (verbose)
                printf("[%u] Waiting for interrupt...\r\n", iter);
            asm("wfi"); // ignores PRIMASK set by int_disable
        }
        int_enable();
    }
    
    return 0;
}

int main(void)
{
#if CONFIG_SMP
    if (self_core_id() > 0)
        return main_secondary();
#endif /* CONFIG_SMP */
    return main_primary();
}

void irq_handler(unsigned intid) {
    DPRINTF("INTID #%u\r\n", intid);
    if (intid < GIC_NR_SGIS) { // SGI
        unsigned sgi = intid;
        switch (sgi) {
            default:
                printf("WARN: no ISR for SGI IRQ #%u\r\n", sgi);
        }
    } else if (intid < GIC_INTERNAL) { // PPI
        unsigned ppi = intid - GIC_NR_SGIS;
        switch (ppi) {
#if TEST_GTIMER || CONFIG_GTIMER
            case PPI_IRQ__TIMER_HYP:
                    gtimer_isr(GTIMER_HYP);
                    break;
            case PPI_IRQ__TIMER_PHYS:
                    gtimer_isr(GTIMER_PHYS);
                    break;
            case PPI_IRQ__TIMER_VIRT:
                    gtimer_isr(GTIMER_VIRT);
                    break;
#endif // TEST_GTIMER || CONFIG_GTIMER
#if TEST_WDT || CONFIG_WDT
            case PPI_IRQ__WDT:
                wdt_isr(wdt, /* stage */ 0);
                break;
#endif // TEST_WDT || CONFIG_WDT
#if TEST_RTI_TIMER
            case PPI_IRQ__RTI_TIMER: {
                rti_timer_isr(rti_timer);
                break;
            }
#endif // TEST_RTI_TIMER
            default:
                printf("WARN: no ISR for PPI IRQ #%u\r\n", ppi);
        }
    } else { // SPI
        unsigned irq = intid - GIC_INTERNAL;
        DPRINTF("IRQ #%u\r\n", irq);
        switch (irq) {
            // Only register the ISRs for mailbox ints that are used (see mailbox-map.h)
            // NOTE: we multiplex all mboxes (in one IP block) onto one pair of IRQs
#if CONFIG_HPPS_RTPS_MAILBOX
            case RTPS_IRQ__HR_MBOX_0 + MBOX_HPPS_RTPS__RTPS_RCV_INT:
                    mbox_rcv_isr(MBOX_HPPS_RTPS__RTPS_RCV_INT);
                    break;
            case RTPS_IRQ__HR_MBOX_0 + MBOX_HPPS_RTPS__RTPS_ACK_INT:
                    mbox_ack_isr(MBOX_HPPS_RTPS__RTPS_ACK_INT);
                    break;
#endif // CONFIG_HPPS_RTPS_MAILBOX
#if CONFIG_RTPS_TRCH_MAILBOX || TEST_RTPS_TRCH_MAILBOX
            case RTPS_IRQ__TR_MBOX_0 + MBOX_LSIO__RTPS_RCV_INT:
                    mbox_rcv_isr(MBOX_LSIO__RTPS_RCV_INT);
                    break;
            case RTPS_IRQ__TR_MBOX_0 + MBOX_LSIO__RTPS_ACK_INT:
                    mbox_ack_isr(MBOX_LSIO__RTPS_ACK_INT);
                    break;
#endif /* CONFIG_RTPS_TRCH_MAILBOX || TEST_RTPS_TRCH_MAILBOX */
#if TEST_RTPS_DMA
            case RTPS_IRQ__RTPS_DMA_ABORT:
                    dma_abort_isr(rtps_dma);
                    break;
            case RTPS_IRQ__RTPS_DMA_EV0:
                    dma_event_isr(rtps_dma, 0);
                    break;
#endif
            default:
                    printf("WARN: no ISR for IRQ #%u\r\n", irq);
        }
    }
}
