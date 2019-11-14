#define DEBUG 0

#include <stdbool.h>
#include <stdint.h>

#include "arm.h"
#include "boot.h"
#include "command.h"
#include "board.h"
#include "console.h"
#include "dmas.h"
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
#endif // CONFIG_SLEEP_TIMER
#endif // CONFIG_SYSTICK

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
        nvic_request(TRCH_IRQ__HT_MBOX_0 + MBOX_HPPS_TRCH__TRCH_RCV_INT);
    mldev_hpps.rcv_int_idx = MBOX_HPPS_TRCH__TRCH_RCV_INT;
    mldev_hpps.ack_irq =
        nvic_request(TRCH_IRQ__HT_MBOX_0 + MBOX_HPPS_TRCH__TRCH_ACK_INT);
    mldev_hpps.ack_int_idx = MBOX_HPPS_TRCH__TRCH_ACK_INT;
    mbox_link_dev_add(MBOX_DEV_HPPS, &mldev_hpps);
#endif // CONFIG_MBOX_DEV_HPPS

#if CONFIG_MBOX_DEV_LSIO
    struct mbox_link_dev mldev_lsio;
    mldev_lsio.base = MBOX_LSIO__BASE;
    mldev_lsio.rcv_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + MBOX_LSIO__TRCH_RCV_INT);
    mldev_lsio.rcv_int_idx = MBOX_LSIO__TRCH_RCV_INT;
    mldev_lsio.ack_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + MBOX_LSIO__TRCH_ACK_INT);
    mldev_lsio.ack_int_idx = MBOX_LSIO__TRCH_ACK_INT;
    mbox_link_dev_add(MBOX_DEV_LSIO, &mldev_lsio);
#endif // CONFIG_MBOX_DEV_LSIO

#if CONFIG_HPPS_TRCH_MAILBOX_SSW
    struct link *hpps_link_ssw = mbox_link_connect("HPPS_MBOX_SSW_LINK",
                    &mldev_hpps,
                    MBOX_HPPS_TRCH__HPPS_TRCH_SSW, MBOX_HPPS_TRCH__TRCH_HPPS_SSW,
                    /* server */ MASTER_ID_TRCH_CPU,
                    /* client */ MASTER_ID_HPPS_CPU0);
    if (!hpps_link_ssw)
        panic("HPPS_MBOX_SSW_LINK");
    // Never release the link, because we listen on it in main loop
#endif

#if CONFIG_HPPS_TRCH_MAILBOX
    struct link *hpps_link = mbox_link_connect("HPPS_MBOX_LINK", &mldev_hpps,
                    MBOX_HPPS_TRCH__HPPS_TRCH, MBOX_HPPS_TRCH__TRCH_HPPS,
                    /* server */ MASTER_ID_TRCH_CPU,
                    /* client */ MASTER_ID_HPPS_CPU0);
    if (!hpps_link)
        panic("HPPS_MBOX_LINK");
    // Never release the link, because we listen on it in main loop
#endif

#if CONFIG_HPPS_TRCH_MAILBOX_ATF
    struct link *hpps_atf_link = mbox_link_connect("HPPS_MBOX_ATF_LINK", &mldev_hpps,
                    MBOX_HPPS_TRCH__HPPS_ATF_TRCH, MBOX_HPPS_TRCH__TRCH_ATF_HPPS,
                    /* server */ MASTER_ID_TRCH_CPU,
                    /* client */ MASTER_ID_HPPS_CPU0);
    if (!hpps_atf_link)
        panic("HPPS_MBOX_ATF_LINK");

    // Never release the link, because we listen on it in main loop
#endif

#if CONFIG_RTPS_TRCH_MAILBOX
    struct link *rtps_links[RTPS_R52_NUM_CORES] = {0};
    rtps_links[0] = mbox_link_connect("RTPS_R52_0_MBOX_LINK",
        &mldev_lsio,
        MBOX_LSIO__RTPS_R52_0_TRCH, MBOX_LSIO__TRCH_RTPS_R52_0,
        /* server */ MASTER_ID_TRCH_CPU,
        /* client */ MASTER_ID_RTPS_CPU0);
    if (!rtps_links[0])
        panic("RTPS_R52_0_MBOX_LINK");
    if (syscfg.rtps_mode != SYSCFG__RTPS_MODE__LOCKSTEP) {
        rtps_links[1] = mbox_link_connect("RTPS_R52_1_MBOX_LINK",
            &mldev_lsio,
            MBOX_LSIO__RTPS_R52_1_TRCH, MBOX_LSIO__TRCH_RTPS_R52_1,
            /* server */ MASTER_ID_TRCH_CPU,
            /* client */ MASTER_ID_RTPS_CPU1);
        if (!rtps_links[1])
            panic("RTPS_R52_1_MBOX_LINK");
    }
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_RTPS_TRCH_MAILBOX

#if CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI
    struct link *rtps_a53_psci_link =
            mbox_link_connect("RTPS_A53_PSCI_MBOX_LINK", &mldev_lsio,
                    MBOX_LSIO__RTPS_A53_TRCH_PSCI, MBOX_LSIO__TRCH_RTPS_A53_PSCI,
                    /* server */ MASTER_ID_TRCH_CPU,
                    /* client */ MASTER_ID_RTPS_A53);
    if (!rtps_a53_psci_link)
        panic("RTPS_A53_PSCI_MBOX_LINK");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI

    llist_init(&link_list);

#if CONFIG_RTPS_TRCH_SHMEM
    struct link *rtps_link_shmem = shmem_link_connect("RTPS_SHMEM_LINK",
                    (void *)RTPS_R52_SHM_ADDR__TRCH_RTPS_REPLY,
                    (void *)RTPS_R52_SHM_ADDR__RTPS_TRCH_SEND);
    if (!rtps_link_shmem)
        panic("RTMS_SHMEM_LINK");
    if (llist_insert(&link_list, rtps_link_shmem))
        panic("RTMS_SHMEM_LINK: llist_insert");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_RTPS_TRCH_SHMEM

#if CONFIG_HPPS_TRCH_SHMEM
    struct link *hpps_link_shmem = shmem_link_connect("HPPS_SHMEM_LINK",
                    (void *)HPPS_SHM_ADDR__TRCH_HPPS,
                    (void *)HPPS_SHM_ADDR__HPPS_TRCH);
    if (!hpps_link_shmem)
        panic("HPPS_SHMEM_LINK");
    if (llist_insert(&link_list, hpps_link_shmem))
        panic("HPPS_SHMEM_LINK: llist_insert");
    // Never disconnect the link, because we listen on it in main loop
#endif // CONFIG_HPPS_TRCH_SHMEM

#if CONFIG_HPPS_TRCH_SHMEM_SSW
    struct link *hpps_link_shmem_ssw = shmem_link_connect("HPPS_SHMEM_SSW_LINK",
                    (void *)HPPS_SHM_ADDR__TRCH_HPPS_SSW,
                    (void *)HPPS_SHM_ADDR__HPPS_TRCH_SSW);
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

        subsys_t subsys;
        while (!boot_handle(&subsys)) {
            int rc = boot_reboot(subsys, &syscfg, trch_fs);
            if (rc) {
                trch_panic("reboot request failed");
            }
            verbose = true; // to end log with 'waiting' msg
        }

        struct cmd cmd;
        struct link *link_curr;
        int sz;
        llist_iter_init(&link_list);
        do {
            link_curr = (struct link *) llist_iter_next(&link_list);
            if (!link_curr)
                break;
            sz = link_curr->recv(link_curr, cmd.msg, sizeof(cmd.msg));
            if (sz) {
                printf("%s: recv: got message\r\n", link_curr->name);
                cmd.link = link_curr;
                if (cmd_enqueue(&cmd))
                    trch_panic("TRCH: failed to enqueue command");
            }
        } while (1);
        while (!cmd_dequeue(&cmd)) {
            cmd_handle(&cmd);
            verbose = true; // to end log with 'waiting' msg
        }

        int_disable(); // the check and the WFI must be atomic
        if (!cmd_pending() && !boot_pending()) {
            if (verbose)
                printf("[%u] Waiting for interrupt...\r\n", iter);
            asm("wfi"); // ignores PRIMASK set by int_disable
        }
        int_enable();
    }
}
