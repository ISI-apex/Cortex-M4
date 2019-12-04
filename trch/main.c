#define DEBUG 0

#include <stdbool.h>
#include <stdint.h>

#include "arm.h"
#include "boot.h"
#include "command.h"
#include "board.h"
#include "console.h"
#include "dmas.h"
#include "event.h"
#include "hwinfo.h"
#include "llist.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "mailbox.h"
#include "mem-map.h"
#include "sfs.h"
#include "mmu.h"
#include "mmus.h"
#include "nvic.h"
#include "panic.h"
#include "console.h"
#include "reset.h"
#include "server.h"
#include "shmem-link.h"
#include "sleep.h"
#include "smc.h"
#include "swtimer.h"
#include "systick.h"
#include "test.h"
#include "watchdog.h"
#include "syscfg.h"

#define SYSTICK_INTERVAL_MS     500
#define SYSTICK_INTERVAL_CYCLES (SYSTICK_INTERVAL_MS * (SYSTICK_CLK_HZ / 1000))
#define MAIN_LOOP_SILENT_ITERS 16

// inferred CONFIG settings
#define CONFIG_MBOX_DEV_HPPS (CONFIG_HPPS_TRCH_MAILBOX_SSW || CONFIG_HPPS_TRCH_MAILBOX || CONFIG_HPPS_TRCH_MAILBOX_ATF)
#define CONFIG_MBOX_DEV_LSIO (CONFIG_RTPS_TRCH_MAILBOX)

// Default boot config (if not loaded from NV mem)
static struct syscfg syscfg = {
    .sfs_offset = 0x0,
    .subsystems = SUBSYS_INVALID,
    .rtps_mode = SYSCFG__RTPS_MODE__LOCKSTEP,
    .hpps_rootfs_loc = MEMDEV_HPPS_DRAM,
    .load_binaries = false,
};


static struct llist link_list = { 0 };

#if CONFIG_TRCH_WDT
static bool trch_wdt_started = false;
#endif // CONFIG_TRCH_WDT

static void trch_panic(const char *msg)
{
#if CONFIG_TRCH_WDT && !CONFIG_RELEASE
    if (trch_wdt_started)
        watchdog_stop(COMP_CPU_TRCH);
#endif
    panic(msg);
}

#if CONFIG_SYSTICK
static void systick_tick(void *arg)
{
    DPRINTF("MAIN: sys tick\r\n");

#if CONFIG_TRCH_WDT
    // Note: we kick here in the ISR instead of relying on the main loop
    // wakeing up from WFE as a result of ISR, because the main loop might not
    // be sleeping but might be performing a long operation, in which case it
    // might not get to the kick statement at the beginning of the main loop in
    // time.
    if (trch_wdt_started)
        watchdog_kick(COMP_CPU_TRCH);
#endif // CONFIG_TRCH_WDT

#if CONFIG_SLEEP_TIMER
    sleep_tick(SYSTICK_INTERVAL_CYCLES);
    sw_timer_tick(SYSTICK_INTERVAL_CYCLES);
#endif // CONFIG_SLEEP_TIMER
}
#endif // CONFIG_SYSTICK

int main ( void )
{
    console_init();
    printf("\r\n\r\nTRCH\r\n");

    printf("ENTER PRIVELEGED MODE: svc #0\r\n");
    asm("svc #0");

    nvic_init(TRCH_SCS_BASE);

    sleep_set_busyloop_factor(TRCH_M4_BUSYLOOP_FACTOR);

#if TEST_SYSTICK
    if (test_systick())
        panic("TRCH systick test");
#endif // TEST_SYSTICK

#if CONFIG_SYSTICK
    systick_config(SYSTICK_INTERVAL_CYCLES, systick_tick, NULL);
    systick_enable();

#if CONFIG_SLEEP_TIMER
    sleep_set_clock(SYSTICK_CLK_HZ);
    sw_timer_init(SYSTICK_CLK_HZ);
#endif // CONFIG_SLEEP_TIMER
#endif // CONFIG_SYSTICK

    struct ev_loop main_event_loop;
    ev_loop_init(&main_event_loop, "main");

#if TEST_ETIMER
    if (test_etimer())
        panic("Elapsed Timer test");
#endif // TEST_ETIMER

#if TEST_RTI_TIMER
    if (test_core_rti_timer())
        panic("RTI Timer test");
#endif // TEST_RTI_TIMER

#if TEST_WDTS
    if (test_wdts())
        panic("WDT test");
#endif // TEST_WDTS

#if TEST_FLOAT
    if (test_float())
        panic("float test");
#endif // TEST_FLOAT

#if TEST_TRCH_DMA
    if (test_trch_dma())
        panic("TRCH DMA test");
#endif // TEST_TRCH_DMA

#if TEST_SHMEM
    if (test_shmem())
        panic("shmem test");
#endif // TEST_SHMEM

#if CONFIG_TRCH_DMA
    struct dma *trch_dma = trch_dma_init();
    if (!trch_dma)
        panic("TRCH DMA");
    // never destroy, it is used by drivers
#else // !CONFIG_TRCH_DMA
    struct dma *trch_dma = NULL;
    (void)trch_dma; /* silence unused warning, depends on config flags */
#endif // !CONFIG_TRCH_DMA

#if CONFIG_SMC
    struct smc *lsio_smc = smc_init(SMC_LSIO_CSR_BASE, &lsio_smc_mem_cfg,
                                    SMC_IFACE_MASK_ALL, SMC_CHIP_MASK_ALL);
    if (!lsio_smc)
        panic("LSIO SMC");
    uint8_t *smc_sram_base = (uint8_t *)SMC_LSIO_SRAM_BASE0;
#endif // CONFIG_SMC

    uint8_t *syscfg_addr;
#if CONFIG_SYSCFG_MEM__TRCH_SRAM
    syscfg_addr = (uint8_t *)CONFIG_SYSCFG_ADDR;
#elif CONFIG_SYSCFG_MEM__LSIO_SMC_SRAM
    syscfg_addr = smc_sram_base + CONFIG_SYSCFG_ADDR;
#endif /* CONFIG_SYSCFG_MEM__* */

    if (syscfg_load(&syscfg, syscfg_addr))
        panic("SYS CFG");

    struct sfs *trch_fs = NULL;
#if CONFIG_SFS
    if (syscfg.have_sfs_offset) {
        trch_fs = sfs_mount(smc_sram_base + syscfg.sfs_offset, trch_dma);
        if (!trch_fs)
            panic("TRCH SMC SRAM FS mount");
    }
#endif /* CONFIG_SFS */

#if CONFIG_RT_MMU
    if (rt_mmu_init())
        panic("RTPS/TRCH-HPPS MMU setup");
    // Never de-init since need some of the mappings while running. We could
    // remove mappings for loading the boot image binaries, but we don't
    // bother, since then would have to recreate them when reseting HPPS/RTPS.
#endif // CONFIG_RT_MMU

#if TEST_RT_MMU
    if (test_rt_mmu())
        panic("RTPS/TRCH-HPPS MMU test");
#endif // TEST_RT_MMU

#if CONFIG_MBOX_DEV_HPPS
    struct mbox_link_dev mldev_hpps;
    mldev_hpps.base = MBOX_HPPS_TRCH__BASE;
    mldev_hpps.rcv_irq =
        nvic_request(TRCH_IRQ__HT_MBOX_0 + HPPS_MBOX0_INT_EVT0__TRCH_SSW);
    mldev_hpps.rcv_int_idx = HPPS_MBOX0_INT_EVT0__TRCH_SSW;
    mldev_hpps.ack_irq =
        nvic_request(TRCH_IRQ__HT_MBOX_0 + HPPS_MBOX0_INT_EVT1__TRCH_SSW);
    mldev_hpps.ack_int_idx = HPPS_MBOX0_INT_EVT1__TRCH_SSW;
    mbox_link_dev_add(MBOX_DEV_HPPS, &mldev_hpps);
#endif // CONFIG_MBOX_DEV_HPPS

#if CONFIG_MBOX_DEV_LSIO
    struct mbox_link_dev mldev_lsio;
    mldev_lsio.base = MBOX_LSIO__BASE;
    mldev_lsio.rcv_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + LSIO_MBOX0_INT_EVT0__TRCH_SSW);
    mldev_lsio.rcv_int_idx = LSIO_MBOX0_INT_EVT0__TRCH_SSW;
    mldev_lsio.ack_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + LSIO_MBOX0_INT_EVT1__TRCH_SSW);
    mldev_lsio.ack_int_idx = LSIO_MBOX0_INT_EVT1__TRCH_SSW;
    mbox_link_dev_add(MBOX_DEV_LSIO, &mldev_lsio);
#endif // CONFIG_MBOX_DEV_LSIO

    unsigned self_owner = OWNER(SW_SUBSYS_TRCH, SW_COMP_SSW);
    (void)self_owner; /* silence unused warning when ifdef'ed out */

#if CONFIG_HPPS_TRCH_MAILBOX_SSW
    struct link *hpps_link_ssw = mbox_link_connect("HPPS_MBOX_SSW_LINK",
        &mldev_hpps,
        HPPS_MBOX0_CHAN__HPPS_SMP_SSW__TRCH_SSW__RQST,
        HPPS_MBOX0_CHAN__HPPS_SMP_SSW__TRCH_SSW__RPLY,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_SSW));
    if (!hpps_link_ssw)
        panic("HPPS_MBOX_SSW_LINK");
    // Never release the link, because we listen on it in main loop
#endif

#if CONFIG_HPPS_TRCH_MAILBOX
    struct link *hpps_link = mbox_link_connect("HPPS_MBOX_LINK", &mldev_hpps,
        HPPS_MBOX0_CHAN__HPPS_SMP_APP__TRCH_SSW__RQST,
        HPPS_MBOX0_CHAN__HPPS_SMP_APP__TRCH_SSW__RPLY,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_APP));
    if (!hpps_link)
        panic("HPPS_MBOX_LINK");
    // Never release the link, because we listen on it in main loop
#endif

#if CONFIG_HPPS_TRCH_MAILBOX_ATF
    struct link *hpps_atf_link = mbox_link_connect("HPPS_MBOX_ATF_LINK",
            &mldev_hpps,
            HPPS_MBOX0_CHAN__HPPS_SMP_ATF__TRCH_SSW__RQST,
            HPPS_MBOX0_CHAN__HPPS_SMP_ATF__TRCH_SSW__RPLY,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_ATF));
    if (!hpps_atf_link)
        panic("HPPS_MBOX_ATF_LINK");

    // Never release the link, because we listen on it in main loop
#endif

    /* As many entries as maximum concurrent RTPS R52 (logical) subsystems */
    struct link *rtps_mb_links[RTPS_R52_NUM_CORES] = {0};
    struct link *rtps_shm_links[RTPS_R52_NUM_CORES] = {0};
    switch (syscfg.rtps_mode) {
    case SYSCFG__RTPS_MODE__LOCKSTEP:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_LOCKSTEP_MBOX_LINK",
            &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW__RQST,
            LSIO_MBOX0_CHAN__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW__RPLY,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_LOCKSTEP, SW_COMP_SSW));
        if (!rtps_mb_links[0])
            panic("RTPS_R52_LOCKSTEP_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect(
            "RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK",
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW__RPLY,
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW__RQST);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK");
        if (llist_insert(&link_list, rtps_shm_links[0]))
            panic("RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    case SYSCFG__RTPS_MODE__SMP:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_SMP_MBOX_LINK",
            &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SMP_SSW__TRCH_SSW__RQST,
            LSIO_MBOX0_CHAN__RTPS_R52_SMP_SSW__TRCH_SSW__RPLY,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SMP, SW_COMP_SSW));
        if (!rtps_mb_links[0])
            panic("RTPS_R52_SMP_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect("RTPS_R52_SMP_SSW_SHMEM_LINK",
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SMP_SSW__TRCH_SSW__RPLY,
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SMP_SSW__TRCH_SSW__RQST);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_SMP_SSW_SHMEM_LINK");
        if (llist_insert(&link_list, rtps_shm_links[0]))
            panic("RTPS_R52_SMP_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    case SYSCFG__RTPS_MODE__SPLIT:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_0_MBOX_LINK", &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_0_SSW__TRCH_SSW__RQST,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_0_SSW__TRCH_SSW__RPLY,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SPLIT_1, SW_COMP_SSW));
        if (!rtps_mb_links[0]) panic("RTPS_R52_SPLIT_0_MBOX_LINK");
        rtps_mb_links[1] = mbox_link_connect("RTPS_R52_1_MBOX_LINK", &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_1_SSW__TRCH_SSW__RQST,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_1_SSW__TRCH_SSW__RPLY,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SPLIT_1, SW_COMP_SSW));
        if (!rtps_mb_links[1])
            panic("RTPS_R52_SPLIT_1_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect(
            "RTPS_R52_SPLIT_0_SSW_SHMEM_LINK",
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0_SSW__TRCH_SSW__RPLY,
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0_SSW__TRCH_SSW__RQST);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_SPLIT_0_SSW_SHMEM_LINK");
        if (llist_insert(&link_list, rtps_shm_links[0]))
            panic("RTPS_R52_SPLIT_0_SSW_SHMEM_LINK: llist_insert");
        rtps_shm_links[1] = shmem_link_connect(
            "RTPS_R52_SPLIT_1_SSW_SHMEM_LINK",
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1_SSW__TRCH_SSW__RPLY,
            (void *)RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1_SSW__TRCH_SSW__RQST);
        if (!rtps_shm_links[1])
            panic("RTPS_R52_SPLIT_1_SSW_SHMEM_LINK");
        if (llist_insert(&link_list, rtps_shm_links[1]))
            panic("RTPS_R52_SPLIT_1_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    default: panic("invalid RTPS R52 mode in syscfg");
    }
    // Never disconnect the link, because we listen on it in main loop

#if CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI
    struct link *rtps_a53_psci_link =
        mbox_link_connect("RTPS_A53_PSCI_MBOX_LINK", &mldev_lsio,
                LSIO_MBOX0_CHAN__RTPS_A53_ATF__TRCH_SSW__RQST,
                LSIO_MBOX0_CHAN__RTPS_A53_ATF__TRCH_SSW__RPLY,
                /* server */ self_owner,
                /* client */ OWNER(SW_SUBSYS_RTPS_A53, SW_COMP_ATF));
    if (!rtps_a53_psci_link)
        panic("RTPS_A53_PSCI_MBOX_LINK");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI

    llist_init(&link_list);

#if CONFIG_HPPS_TRCH_SHMEM
    struct link *hpps_link_shmem = shmem_link_connect("HPPS_SHMEM_LINK",
            (void *)HPPS_SHM_ADDR__HPPS_SMP_APP__TRCH_SSW__RQST,
            (void *)HPPS_SHM_ADDR__HPPS_SMP_APP__TRCH_SSW__RPLY);
    if (!hpps_link_shmem)
        panic("HPPS_SHMEM_LINK");
    if (llist_insert(&link_list, hpps_link_shmem))
        panic("HPPS_SHMEM_LINK: llist_insert");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_HPPS_TRCH_SHMEM

#if CONFIG_HPPS_TRCH_SHMEM_SSW
    struct link *hpps_link_shmem_ssw = shmem_link_connect("HPPS_SHMEM_SSW_LINK",
            (void *)HPPS_SHM_ADDR__HPPS_SMP_SSW__TRCH_SSW__RQST,
            (void *)HPPS_SHM_ADDR__HPPS_SMP_SSW__TRCH_SSW__RPLY);
    if (!hpps_link_shmem_ssw)
        panic("HPPS_SHMEM_SSW_LINK");
    if (llist_insert(&link_list, hpps_link_shmem_ssw))
        panic("HPPS_SHMEM_SSW_LINK: llist_insert");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_HPPS_TRCH_SHMEM_SSW

    boot_request(syscfg.subsystems);

#if CONFIG_TRCH_WDT
    watchdog_init_group(CPU_GROUP_TRCH);
    watchdog_start(COMP_CPU_TRCH);
    trch_wdt_started = true;
#endif // CONFIG_TRCH_WDT

    cmd_handler_register(server_process);

    unsigned iter = 0;
    while (1) {
        bool verbose = iter++ % MAIN_LOOP_SILENT_ITERS == 0;
        if (verbose)
            printf("TRCH: main loop\r\n");

#if CONFIG_TRCH_WDT && !CONFIG_SYSTICK // with SysTick, we kick from ISR
        // Kicking from here is insufficient, because we sleep. There are two
        // ways to complete: (A) have TRCH disable the watchdog in response to
        // the WFI output signal from the core, and/or (B) have a scheduler
        // (with a tick interval shorter than the watchdog timeout interval)
        // and kick from the scheuduler tick. As a temporary stop-gap, we go
        // with (C): kick on return from WFI/WFI as a result of first stage
        // timeout IRQ.
        watchdog_kick(COMP_CPU_TRCH);
#endif // CONFIG_TRCH_WDT

        //printf("main\r\n");

        sw_timer_run();

        subsys_t subsys;
        while (!boot_handle(&subsys)) {
            int rc = boot_reboot(subsys, &syscfg, trch_fs);
            if (rc) {
                trch_panic("reboot request failed");
            }
            verbose = true; // to end log with 'waiting' msg
        }

        /* only process one at a time, and go around the main loop */
        if (!ev_loop_process(&main_event_loop)) {
            verbose = true; /* to end output with 'waiting' msg */
        }

        /* TODO: move within shmem-link, use event loop */
        struct cmd cmd;
        struct link *link_curr;
        llist_iter_init(&link_list);
        do {
            link_curr = (struct link *) llist_iter_next(&link_list);
            if (!link_curr)
                break;
            cmd.len = link_curr->recv(link_curr, cmd.msg, sizeof(cmd.msg));
            if (cmd.len) {
                printf("%s: recv: got message\r\n", link_curr->name);
                cmd.link = link_curr;
                if (cmd_enqueue(&cmd))
                    trch_panic("TRCH: failed to enqueue command");
            }
        } while (1);

        /* TODO: implement using event loop */
        while (!cmd_dequeue(&cmd)) {
            cmd_handle(&cmd);
            verbose = true; // to end log with 'waiting' msg
        }

        int_disable(); // the check and the WFI must be atomic
        if (!cmd_pending() && !boot_pending() &&
            !ev_loop_pending(&main_event_loop)) {
            if (verbose)
                printf("[%u] Waiting for interrupt...\r\n", iter);
            asm("wfi"); // ignores PRIMASK set by int_disable
        }
        int_enable();
    }
}
