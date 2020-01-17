#include <stdint.h>

#include "console.h"
#include "subsys.h"
#include "syscfg.h"

#if CONFIG_CONSOLE
static const char *rtps_mode_name(unsigned m)
{
    switch (m) {
        case SYSCFG__RTPS_MODE__LOCKSTEP:   return "LOCKSTEP";
        case SYSCFG__RTPS_MODE__SMP:        return "SMP";
        case SYSCFG__RTPS_MODE__SPLIT:      return "SPLIT";
        default:                            return "?";
    };
}
#endif /* CONFIG_CONSOLE */

const char *memdev_name(enum memdev d)
{
    switch (d) {
        case MEMDEV_TRCH_SMC_SRAM:       return "TRCH_SMC_SRAM";
        case MEMDEV_TRCH_SMC_NAND:       return "TRCH_SMC_NAND";
        case MEMDEV_HPPS_SMC_SRAM:       return "HPPS_SMC_SRAM";
        case MEMDEV_HPPS_SMC_NAND:       return "HPPS_SMC_NAND";
        case MEMDEV_HPPS_DRAM:           return "HPPS_DRAM";
        case MEMDEV_RTPS_DRAM:           return "RTPS_DRAM";
        case MEMDEV_RTPS_TCM:            return "RTPS_TCM";
        case MEMDEV_TRCH_SRAM:           return "TRCH_SRAM";
        default:                         return "?";
    };
}

void syscfg_print(struct syscfg *cfg)
{
    printf("SYSTEM CONFIG:\r\n"
           "\thave sfs offset:\t%u\r\n"
           "\tsfs offset:\t0x%x\r\n"
           "\tload_binaries:\t%u\r\n"
           "\tsubsystems:\t%s\r\n"
           "\trtps mode:\t%s\r\n"
           "\trtps cores bitmask:\t0x%x\r\n"
           "\thpps rootfs loc:\t%s\r\n",
           cfg->have_sfs_offset, cfg->sfs_offset,
           cfg->load_binaries,
           subsys_name(cfg->subsystems),
           rtps_mode_name(cfg->rtps_mode), cfg->rtps_cores,
           memdev_name(cfg->hpps.rootfs_loc));
}

int syscfg_load(struct syscfg *cfg, uint8_t *addr)
{
    uint32_t *waddr = (uint32_t *)addr;
    uint32_t word0;

    word0 = *waddr++;
    printf("SYSCFG: @%p word0: %x\r\n", addr, word0);

    /* TODO: use field macros from lib/ */
    cfg->rtps_mode = (word0 & SYSCFG__RTPS_MODE__MASK) >> SYSCFG__RTPS_MODE__SHIFT;
    cfg->rtps_cores = (word0 & SYSCFG__RTPS_CORES__MASK)
                            >> SYSCFG__RTPS_CORES__SHIFT;
    cfg->subsystems = (word0 & SYSCFG__SUBSYS__MASK) >> SYSCFG__SUBSYS__SHIFT;
    cfg->hpps.rootfs_loc = (word0 & SYSCFG__HPPS_ROOTFS_LOC__MASK)
                                >> SYSCFG__HPPS_ROOTFS_LOC__SHIFT;
    cfg->have_sfs_offset = (word0 & SYSCFG__HAVE_SFS_OFFSET__MASK)
                                >> SYSCFG__HAVE_SFS_OFFSET__SHIFT;
    cfg->load_binaries = (word0 & SYSCFG__LOAD_BINARIES__MASK)
                                >> SYSCFG__LOAD_BINARIES__SHIFT;
    cfg->sfs_offset = *waddr++;

    syscfg_print(cfg);
    return 0;
}
