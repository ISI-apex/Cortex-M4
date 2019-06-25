#ifndef HWINFO_H
#define HWINFO_H

// Copies from Qemu Device Tree repository
#include "hpsc-irqs.dtsh"
#include "hpsc-busids.dtsh"

// This file fulfills the role of a device tree

#define RTPS_GIC_BASE   0x30e00000
#define TRCH_SCS_BASE   0xe000e000

#define RTPS_TRCH_TO_HPPS_SMMU_BASE   0x31100000
#define RTPS_SMMU_BASE                0x31000000
#define HPPS_SMMU_BASE                0xf9300000

#define TRCH_DMA_BASE   0x21000000
#define RTPS_DMA_BASE   0x30a08000

#define LSIO_UART0_BASE 0x30000000
#define LSIO_UART1_BASE 0x30001000
#define HPPS_UART_BASE  0xf92c0000

#define UART_CLOCK      100000000

#define ETIMER__BASE                    0x2100a000
#define RTI_TIMER_TRCH__BASE            0x21009000
#define RTI_TIMER_RTPS_R52_0__TRCH_BASE 0x21007000
#define RTI_TIMER_RTPS_R52_1__TRCH_BASE 0x21008000
#define RTI_TIMER_RTPS_A53__TRCH_BASE   0x21006000
#define RTI_TIMER_RTPS_R52_0__RTPS_BASE 0x30a05000
#define RTI_TIMER_RTPS_R52_1__RTPS_BASE 0x30a06000
#define RTI_TIMER_RTPS_A53__RTPS_BASE   0x30a04000

#define MBOX_LSIO__BASE           0x3000a000
#define MBOX_HPPS_TRCH__BASE      0xfff50000
#define MBOX_HPPS_RTPS__BASE      0xfff60000

#define WDT_SIZE_4KB              0x1000
#define WDT_SIZE_64KB            0x10000

#define WDT_TRCH_BASE              0x21002000
#define WDT_TRCH_SIZE              WDT_SIZE_4KB

#define WDT_RTPS_R52_0_TRCH_BASE   0x21004000
#define WDT_RTPS_R52_1_TRCH_BASE   0x21005000
#define WDT_RTPS_A53_TRCH_BASE     0x21003000
#define WDT_RTPS_R52_0_RTPS_BASE   0x30a0a000
#define WDT_RTPS_R52_1_RTPS_BASE   0x30a0b000
#define WDT_RTPS_R52_SIZE          WDT_SIZE_4KB
#define WDT_RTPS_A53_RTPS_BASE     0x30a09000
#define WDT_RTPS_A53_SIZE          WDT_SIZE_4KB

#define WDT_HPPS_TRCH_BASE         0x21010000
#define WDT_HPPS_RTPS_BASE         0xfff70000
#define WDT_HPPS_SIZE              WDT_SIZE_64KB

#define APU       0xfd5c0000
#define APU1      0xfd5c1000
#define CRF       0xfd1a0000
#define CRL       0xff5e0000
#define RPU_CTRL  0xff9a0000

#define RESET_CTRL              0x22000000

#define HSIO_BASE               0xe3000000
#define HSIO_SIZE               0x15000000

#define SMC_LSIO_CSR_BASE           0x30006000
#define SMC_LSIO_SRAM_BASE0         0x28000000
#define SMC_LSIO_SRAM_SIZE0         0x01000000
#define SMC_LSIO_SRAM_BASE1         0x29000000
#define SMC_LSIO_SRAM_SIZE1         0x01000000
#define SMC_LSIO_SRAM_BASE2         0x2a000000
#define SMC_LSIO_SRAM_SIZE2         0x01000000
#define SMC_LSIO_SRAM_BASE3         0x2b000000
#define SMC_LSIO_SRAM_SIZE3         0x01000000
#define SMC_LSIO_NAND_BASE0         0x2c000000
#define SMC_LSIO_NAND_SIZE0         0x04000000

// See props in Qemu device tree node (or real HW characteristics)
#define ETIMER_NOMINAL_FREQ_HZ 1000000000
#define ETIMER_CLK_FREQ_HZ      125000000
#define ETIMER_MAX_DIVIDER             32
#define ETIMER_MAX_DIVIDER_BITS         5 // log2(ETIMER_MAX_DIVIDER)
#define ETIMER_MIN_CLK_FREQ_HZ (ETIMER_CLK_FREQ_HZ / ETIMER_MAX_DIVIDER)
// See Qemu model: max count is less than 2^64-1 due to a limitation
#define ETIMER_MAX_COUNT  ((1ULL << (64 - ETIMER_MAX_DIVIDER_BITS - 1)) - 1)
#define RTI_MAX_COUNT ETIMER_MAX_COUNT

#define WDT_CLK_FREQ_HZ         125000000
#define WDT_MAX_DIVIDER                32
#define WDT_MIN_FREQ_HZ (WDT_CLK_FREQ_HZ / WDT_MAX_DIVIDER)

// SW should normally get this value from gtimer_get_frq(), which gets it from
// a register set by SW (the bootloader). But, for some parts of our code,
// i.e. tests, we don't want to rely on the bootloader, so it's defined here.
#define GTIMER_FREQ_HZ		125000000

// When timing is implemented by a busyloop instead of by a HW timer,
// we need to convert seconds to interations (empirically calibrated).
#define RTPS_R52_BUSYLOOP_FACTOR       1000000
#define TRCH_M4_BUSYLOOP_FACTOR		800000

#endif // HWINFO_H
