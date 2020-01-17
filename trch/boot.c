#include <stdlib.h>

#include "console.h"
#include "panic.h"
#include "reset.h"
#include "smc.h"
#include "watchdog.h"
#include "syscfg.h"
#include "mem-map.h"
#include "sfs.h"

#include "boot.h"

static subsys_t reboot_requests;

static int boot_load(subsys_t subsys, struct syscfg *cfg, struct sfs *fs)
{
   struct syscfg_rtps_r52 *cfg_r52;
   struct syscfg_rtps_a53 *cfg_a53;
   struct syscfg_hpps *cfg_hpps;
   ASSERT(fs);
   switch (subsys) {
      case SUBSYS_RTPS_R52:
         printf("BOOT: load RTPS\r\n");
         cfg_r52 = &cfg->rtps_r52;
         for (int i = 0; cfg_r52->blobs[i]; ++i) {
            if (sfs_load(fs, cfg_r52->blobs[i], NULL, NULL))
               return 1;
         }
         break;
      case SUBSYS_RTPS_A53:
         cfg_a53 = &cfg->rtps_a53;
         for (int i = 0; cfg_a53->blobs[i]; ++i) {
            if (sfs_load(fs, cfg_a53->blobs[i], NULL, NULL))
               return 1;
         }
         break;
      case SUBSYS_HPPS:
         printf("BOOT: load HPPS\r\n");
         cfg_hpps = &cfg->hpps;
         for (int i = 0; cfg_hpps->blobs[i]; ++i) {
            if (sfs_load(fs, cfg_hpps->blobs[i], NULL, NULL))
               return 1;
         }
         break;
      default:
         printf("BOOT: ERROR: unknown subsystem %x\r\n", subsys);
         return 1;
   };
   return 0;
}

static int boot_reset(subsys_t subsys, struct syscfg *cfg)
{
    int rc = 0;
    switch (subsys) {
        case SUBSYS_RTPS_R52:
            switch (cfg->rtps_mode) {
                case SYSCFG__RTPS_MODE__LOCKSTEP:
#if CONFIG_RTPS_R52_WDT
                    watchdog_init_group(CPU_GROUP_RTPS_R52_0);
#endif // CONFIG_RTPS_R52_WDT
                    reset_set_rtps_r52_mode(RTPS_R52_MODE__LOCKSTEP);
                    rc = reset_release(COMP_CPU_RTPS_R52_0);
                    break;
                case SYSCFG__RTPS_MODE__SMP:
#if CONFIG_RTPS_R52_WDT
                    watchdog_init_group(CPU_GROUP_RTPS_R52);
#endif // CONFIG_RTPS_R52_WDT
                    reset_set_rtps_r52_mode(RTPS_R52_MODE__SPLIT);
                    rc = reset_release(COMP_CPU_RTPS_R52_0);
                    break;
                case SYSCFG__RTPS_MODE__SPLIT:
                    reset_set_rtps_r52_mode(RTPS_R52_MODE__SPLIT);
#if CONFIG_RTPS_R52_WDT
                    if (cfg->rtps_cores & 0x1)
                       watchdog_init_group(CPU_GROUP_RTPS_R52_0);
                    if (cfg->rtps_cores & 0x2)
                       watchdog_init_group(CPU_GROUP_RTPS_R52_1);
#endif /* CONFIG_RTPS_R52_WDT */

                    if (cfg->rtps_cores & 0x1)
                        rc = reset_release(COMP_CPU_RTPS_R52_0);
                    if (cfg->rtps_cores & 0x2)
                        rc = reset_release(COMP_CPU_RTPS_R52_1);
                    break;
                default:
                    printf("BOOT: ERROR: unknown RTPS boot mode: %x\r\n",
                           cfg->rtps_mode);
                    return 1;
            }
            break;
        case SUBSYS_RTPS_A53:
#if CONFIG_RTPS_A53_WDT
            watchdog_init_group(CPU_GROUP_RTPS_A53);
#endif // CONFIG_RTPS_A53_WDT
            rc = reset_release(COMP_CPUS_RTPS_A53);
            break;
        case SUBSYS_HPPS:
#if CONFIG_HPPS_WDT
            watchdog_init_group(CPU_GROUP_HPPS);
#endif // CONFIG_HPPS_WDT
            rc = reset_release(COMP_CPU_HPPS_0);
            break;
        default:
            printf("BOOT: ERROR: invalid subsystem: %u\r\n", subsys);
            rc = 1;
    }
    return rc;
}

void boot_request(subsys_t subsys)
{
    printf("BOOT: accepted reboot request for subsystems %s\r\n",
           subsys_name(subsys));
    reboot_requests |= subsys; // coallesce requests
    // TODO: SEV (to prevent race between requests check and WFE in main loop)
}

bool boot_pending()
{
    return !!reboot_requests;
}

int boot_handle(subsys_t *subsys)
{
    int b = 0;
    while (b < NUM_SUBSYSS && !(reboot_requests & (1 << b)))
        b++;
    if (b == NUM_SUBSYSS)
        return 1;
    *subsys = (subsys_t)(1 << b);
    return 0;
}

int boot_reboot(subsys_t subsys, struct syscfg *cfg, struct sfs *fs)
{
    int rc = 0;
    printf("BOOT: rebooting subsys %s...\r\n", subsys_name(subsys));

    if (cfg->load_binaries && fs) {
        rc |= boot_load(subsys, cfg, fs);
    } else if (cfg->load_binaries) {
        if (!fs)
            printf("BOOT: not loading binaries: no Simple File System\r\n");
        else
            printf("BOOT: not loading binaries: configured as preloaded\r\n");
    }
    rc |= boot_reset(subsys, cfg);

    reboot_requests &= ~subsys;
    printf("BOOT: rebooted subsys %s: rc %u\r\n", subsys_name(subsys), rc);
   return rc;
}
