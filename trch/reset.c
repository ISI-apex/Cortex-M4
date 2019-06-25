#define DEBUG 0

#include <stdint.h>
#include <stdbool.h>

#include "hwinfo.h"
#include "console.h"
#include "regops.h"
#include "subsys.h"
#include "sleep.h"

#include "reset.h"

#define APU__PWRCTL 0x90
#define APU__PWRCTL__CPUxPWRDWNREQ__SHIFT               0
#define APU__PWRCTL__CPUxPWRDWNREQ                   0xff

#define CRF__RST_FPD_APU 0x104
#define CRF__RST_FPD_APU__ACPUx_RESET__SHIFT            0
#define CRF__RST_FPD_APU__ACPUx_RESET                0xff
#define CRF__RST_FPD_APU__GIC_RESET               0x40000

#define CRL__RST_LPD_TOP 0x23c
#define CRL__RST_LPD_TOP__RPU_x_RESET__SHIFT            0
#define CRL__RST_LPD_TOP__RPU_x_RESET                 0x7
#define CRL__RST_LPD_TOP__R52_GIC_RESET        0x01000000
#define CRL__RST_LPD_TOP__A53_GIC_RESET        0x02000000

#define RPU_CTRL__RPU_GLBL_CNTL	0x0
#define RPU_CTRL__RPU_GLBL_CNTL__SLSPLIT 0x8

#define RPU_CTRL__RPU_0_CFG  0x100
#define RPU_CTRL__RPU_0_CFG__NCPUHALT 0x1

#define RPU_CTRL__RPU_1_CFG  0x200
#define RPU_CTRL__RPU_1_CFG__NCPUHALT 0x1

#define RPU_CTRL__RPU_2_CFG  0x270
#define RPU_CTRL__RPU_2_CFG__NCPUHALT 0x1

#define RESET_CTRL__NCPUHALT    0x0
#define RESET_CTRL__RESET       0x4
#define RESET_CTRL__INTC_RESET  0x8

#define INTC__RTPS_R52      0x1

#if CONFIG_CONSOLE
static const char *rtps_r52_mode_name(unsigned m)
{
    switch (m) {
        case RTPS_R52_MODE__SPLIT:    return "SPLIT";
        case RTPS_R52_MODE__LOCKSTEP: return "LOCKSTEP";
        default:                      return "?";
    };
}
#endif /* CONFIG_CONSOLE */

int reset_assert(comp_t comps)
{
    printf("RESET: assert: components mask %x\r\n", comps);

    // Note: we tie the GICs to the respective CPUs unconditionally here

    if (comps & COMP_CPUS_RTPS) {
        uint8_t cpu_bitmask = ((comps & COMP_CPUS_RTPS) >> COMP_CPUS_SHIFT_RTPS);
        REGB_SET32(RESET_CTRL, RESET_CTRL__RESET, cpu_bitmask);
        REGB_SET32(RESET_CTRL, RESET_CTRL__INTC_RESET, INTC__RTPS_R52);
        REGB_CLEAR32(RESET_CTRL, RESET_CTRL__NCPUHALT, cpu_bitmask);
    }

#if 0
    if (comps & COMP_CPU_RTPS_A53_0)
        REGB_CLEAR32(RPU_CTRL, RPU_CTRL__RPU_2_CFG, RPU_CTRL__RPU_2_CFG__NCPUHALT);

    if (comps & COMP_CPUS_HPPS) {
        volatile uint8_t * apu = (volatile uint8_t *)APU;
        uint32_t shift = COMP_CPUS_SHIFT_HPPS;
        REGB_SET32(CRF, CRF__RST_FPD_APU,
                   (CRF__RST_FPD_APU__ACPUx_RESET &
                    (((comps & COMP_CPUS_HPPS) >> COMP_CPUS_SHIFT_HPPS)
                        << CRF__RST_FPD_APU__ACPUx_RESET__SHIFT)) |
                   (((comps & COMP_CPUS_HPPS) == COMP_CPUS_HPPS) ? CRF__RST_FPD_APU__GIC_RESET : 0x0));
        if (comps & COMP_CPUS_HPPS_CL1) { 
            apu = (volatile uint8_t *)APU1;
            shift = COMP_CPUS_SHIFT_HPPS_CL1;
        }
        REGB_SET32(apu, APU__PWRCTL,
               (APU__PWRCTL__CPUxPWRDWNREQ &
                (((comps & COMP_CPUS_HPPS) >> shift)
                    << APU__PWRCTL__CPUxPWRDWNREQ__SHIFT)));
    }
#endif
    return 0;
}

int reset_release(comp_t comps)
{
    printf("RESET: release: components mask %x\r\n", comps);

    // Note: we tie the GICs to the respective CPUs unconditionally here

    if (comps & COMP_CPUS_RTPS) {
        uint8_t cpu_bitmask = ((comps & COMP_CPUS_RTPS) >> COMP_CPUS_SHIFT_RTPS);
        REGB_SET32(RESET_CTRL, RESET_CTRL__NCPUHALT, cpu_bitmask);
        REGB_CLEAR32(RESET_CTRL, RESET_CTRL__INTC_RESET, INTC__RTPS_R52);
        mdelay(1); // wait for GIC at least 5 cycles (GIC-500 TRM Table A-1)
        REGB_CLEAR32(RESET_CTRL, RESET_CTRL__RESET, cpu_bitmask);
    }

#if 0
    if (comps & COMP_CPUS_RTPS_A53)
        REGB_SET32(RPU_CTRL, RPU_CTRL__RPU_2_CFG, RPU_CTRL__RPU_2_CFG__NCPUHALT);
        gic_reset |= CRL__RST_LPD_TOP__A53_GIC_RESET;

        REGB_CLEAR32(CRL, CRL__RST_LPD_TOP, gic_reset);
        mdelay(1); // wait for GIC at least 5 cycles (GIC-500 TRM Table A-1)

        REGB_CLEAR32(CRL, CRL__RST_LPD_TOP,
                CRL__RST_LPD_TOP__RPU_x_RESET &
                 ((comps >> COMP_CPUS_SHIFT_RTPS) << CRL__RST_LPD_TOP__RPU_x_RESET__SHIFT));
    }

    if (comps & COMP_CPUS_HPPS) {
        volatile uint8_t * apu = (volatile uint8_t *)APU;
        uint32_t shift = COMP_CPUS_SHIFT_HPPS;
        if (comps & COMP_CPUS_HPPS_CL1) { 
            apu = (volatile uint8_t *)APU1;
            shift = COMP_CPUS_SHIFT_HPPS_CL1;
        }
        REGB_CLEAR32(apu, APU__PWRCTL,
                (APU__PWRCTL__CPUxPWRDWNREQ &
                 (((comps & COMP_CPUS_HPPS) >> shift)
                  << APU__PWRCTL__CPUxPWRDWNREQ__SHIFT)));

        REGB_CLEAR32(CRF, CRF__RST_FPD_APU, CRF__RST_FPD_APU__GIC_RESET);
        mdelay(1); // wait for GIC at least 5 cycles (GIC-500 TRM Table A-1)

        /* DK: added in case reset is not zero */
	uint32_t value = REGB_READ32(CRF, CRF__RST_FPD_APU); 
        if (!(value & (CRF__RST_FPD_APU__ACPUx_RESET &
                    (((comps & COMP_CPUS_HPPS) >> COMP_CPUS_SHIFT_HPPS)
                        << CRF__RST_FPD_APU__ACPUx_RESET__SHIFT)))) {

            REGB_SET32(CRF, CRF__RST_FPD_APU,
                   (CRF__RST_FPD_APU__ACPUx_RESET &
                    (((comps & COMP_CPUS_HPPS) >> COMP_CPUS_SHIFT_HPPS)
                        << CRF__RST_FPD_APU__ACPUx_RESET__SHIFT)));
        }
        REGB_CLEAR32(CRF, CRF__RST_FPD_APU,
                (CRF__RST_FPD_APU__ACPUx_RESET &
                 (((comps & COMP_CPUS_HPPS) >> COMP_CPUS_SHIFT_HPPS)
                  << CRF__RST_FPD_APU__ACPUx_RESET__SHIFT)));
    }
#endif
    return 0;
}

int reset_set_rtps_r52_mode(enum rtps_r52_mode m)
{
    printf("RESET: set RTPS R52 mode: %s\r\n", rtps_r52_mode_name(m));
    switch (m) {
        case RTPS_R52_MODE__LOCKSTEP:
            REGB_CLEAR32(RPU_CTRL, RPU_CTRL__RPU_GLBL_CNTL, RPU_CTRL__RPU_GLBL_CNTL__SLSPLIT);
            break;
        case RTPS_R52_MODE__SPLIT:
            REGB_SET32(RPU_CTRL, RPU_CTRL__RPU_GLBL_CNTL, RPU_CTRL__RPU_GLBL_CNTL__SLSPLIT);
            break;
        default:
            ASSERT(false && "invalid RTPS R52 mode");
    }
    return 0;
}
