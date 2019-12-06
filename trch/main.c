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
#include "links.h"
#include "mailbox.h"
#include "sfs.h"
#include "mmu.h"
#include "mmus.h"
#include "nvic.h"
#include "panic.h"
#include "console.h"
#include "reset.h"
#include "server.h"
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

// Default boot config (if not loaded from NV mem)
static struct syscfg syscfg = {
    .sfs_offset = 0x0,
    .subsystems = SUBSYS_INVALID,
    .rtps_mode = SYSCFG__RTPS_MODE__LOCKSTEP,
    .hpps_rootfs_loc = MEMDEV_HPPS_DRAM,
    .load_binaries = false,
};

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

    if (test_standalone())
        panic("standalone tests");

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

    links_init(syscfg.rtps_mode);
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

        if (links_poll())
            trch_panic("poll links");

        /* TODO: implement using event loop */
        static struct cmd cmd; /* lifetime = body, but don't alloc on stack */
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
