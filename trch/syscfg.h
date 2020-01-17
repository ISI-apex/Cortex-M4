#ifndef SYSCFG_H
#define SYSCFG_H

#include <stdint.h>
#include <stdbool.h>

#include "subsys.h"

// Fields in the 32-bit words specifying the boot config
// Exposing since it's an external-facing interface
#define SYSCFG__LOAD_BINARIES__SHIFT      0
#define SYSCFG__LOAD_BINARIES__MASK       (0x1 << SYSCFG__LOAD_BINARIES__SHIFT)
#define SYSCFG__RTPS_MODE__SHIFT           1
#define SYSCFG__RTPS_MODE__MASK            (0x3 << SYSCFG__RTPS_MODE__SHIFT)
#define SYSCFG__RTPS_CORES__SHIFT          3
#define SYSCFG__RTPS_CORES__MASK           (0x3 << SYSCFG__RTPS_CORES__SHIFT)
#define SYSCFG__SUBSYS__SHIFT              5
#define SYSCFG__SUBSYS__MASK               (0xf << SYSCFG__SUBSYS__SHIFT)
#define SYSCFG__HPPS_ROOTFS_LOC__SHIFT     9
#define SYSCFG__HPPS_ROOTFS_LOC__MASK      (0x7 << SYSCFG__HPPS_ROOTFS_LOC__SHIFT)
#define SYSCFG__HAVE_SFS_OFFSET__SHIFT     12
#define SYSCFG__HAVE_SFS_OFFSET__MASK      (0x1 << SYSCFG__HAVE_SFS_OFFSET__SHIFT)
#define SYSCFG__SFS_OFFSET__WORD           1

enum memdev {
    MEMDEV_TRCH_SMC_SRAM = 0x0,
    MEMDEV_TRCH_SMC_NAND = 0x1,
    MEMDEV_HPPS_SMC_SRAM = 0x2,
    MEMDEV_HPPS_SMC_NAND = 0x3,
    MEMDEV_HPPS_DRAM     = 0x4,
    MEMDEV_RTPS_DRAM     = 0x5,
    MEMDEV_RTPS_TCM      = 0x6,
    MEMDEV_TRCH_SRAM     = 0x7,
};

enum rtps_mode {
    SYSCFG__RTPS_MODE__LOCKSTEP    = 0x0,
    SYSCFG__RTPS_MODE__SMP	       = 0x1,
    SYSCFG__RTPS_MODE__SPLIT       = 0x2,
};

struct syscfg_hpps {
    enum memdev rootfs_loc;
};

struct syscfg {
    subsys_t subsystems; // bitmask of subsystems to boot
    enum rtps_mode rtps_mode;
    uint8_t rtps_cores; /* bitmask (valid only in SPLIT mode) */
    bool have_sfs_offset;
    uint32_t sfs_offset;
    bool load_binaries;
    struct syscfg_hpps hpps;
};

int syscfg_load(struct syscfg *cfg, uint8_t *addr);
void syscfg_print(struct syscfg *cfg);

const char *memdev_name(enum memdev d);
#endif // SYSCFG_H
