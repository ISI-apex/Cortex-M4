#ifndef SYSCFG_H
#define SYSCFG_H

#include "subsys.h"

// Fields in the 32-bit value specifying the boot config
// Exposing since it's an external-facing interface
#define SYSCFG__BIN_LOC__SHIFT             0
#define SYSCFG__BIN_LOC__MASK              (0x7 << SYSCFG__BIN_LOC__SHIFT)
#define SYSCFG__RTPS_MODE__SHIFT           3
#define SYSCFG__RTPS_MODE__MASK            (0x3 << SYSCFG__RTPS_MODE__SHIFT)
#define SYSCFG__SUBSYS__SHIFT              5
#define SYSCFG__SUBSYS__MASK               (0xf << SYSCFG__SUBSYS__SHIFT)

struct syscfg {
    enum {
        SYSCFG__BIN_LOC__SRAM = 0x1, // load HPPS/RTPS binaries from SRAM
        SYSCFG__BIN_LOC__DRAM = 0x2, // assume HPPS/RTPS binaries already in DRAM
    } bin_loc;
    subsys_t subsystems; // bitmask of subsystems to boot
    enum {
        SYSCFG__RTPS_MODE__SPLIT    = 0x0,
        SYSCFG__RTPS_MODE__LOCKSTEP = 0x1,
        SYSCFG__RTPS_MODE__SMP	    = 0x2,
    } rtps_mode;
};

int syscfg_load(struct syscfg *cfg);
void syscfg_print(struct syscfg *cfg);

#endif // SYSCFG_H