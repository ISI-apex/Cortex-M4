#ifndef MEM_MAP_H
#define MEM_MAP_H

// This file indicates the resource allocations in memory

// Naming convention:
// General use:   REGION_BLOCK_PROP__ID
// REGION: [TRCH, LSIO, RTPS, HPPS] - the Chiplet region
// BLOCK: [DDR, SRAM, NAND] - the memory type in REGION
// PROP: [ADDR, SIZE] - address or size
// ID - some useful identifier for what's in memory
//
// Shared memory messaging: ID = 'SHM'__SRC_SW__DEST_SW
// SRC, DST: [TRCH, RTPS_R52_LOCKSTEP, RTPS_R52_SMP,
//            RTPS_R52_SPLIT_0, RTPS_R52_SPLIT_1, RTPS_A53,
//            HPPS_SMP, HPPS_SMP_CL0, HPPS_SMP_CL1] - subsystem
// SW: [SSW, ATF, APP] - the SRC/DST software component

// TODO: Update remaining macros to use the naming convention

//////////
// RTPS //
//////////

// Binary images with executables for RTPS
#define RTPS_DDR_ADDR__RTPS_R52_LOCKSTEP               0x40000000
#define RTPS_DDR_SIZE__RTPS_R52_LOCKSTEP               0x7e000000 // excludes shm

// Page tables for RTPS MMU
#define RTPS_PT_ADDR 0x40010000
#define RTPS_PT_SIZE   0x200000

// DMA Microcode buffer (cannot be in TCM)
#define RTPS_DMA_MCODE_ADDR 0x40000000
#define RTPS_DMA_MCODE_SIZE 0x00001000

// Buffers for DMA test
#define RTPS_DMA_SRC_ADDR       0x40001000
#define RTPS_DMA_SIZE           0x00000200
#define RTPS_DMA_DST_ADDR       0x40002000 // align to page
#define RTPS_DMA_DST_REMAP_ADDR 0x40003000 // MMU test maps this to DST_ADDR

// Shared memory regions accessible to RTPS - kept at end of RTPS DRAM region
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP                   0xbe000000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP                   0x02000000
// TRCH <-> RTPS R52
#define RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW     0xbe000000
#define RTPS_DDR_SIZE__SHM__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW     0x00008000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW     0xbe008000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW     0x00008000
// Shared memory - reserved but not allocated
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP__FREE             0xbe010000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP__FREE             0x01ff0000

//////////
// HPPS //
//////////

// Binary images with executables for HPPS
#define HPPS_DDR_LOW_ADDR__HPPS_SMP   0xc0000000
#define HPPS_DDR_LOW_SIZE__HPPS_SMP   0x1fc00000 // excludes TRCH reservations in HPPS DRAM

#define WINDOWS_TO_40BIT_ADDR            0xc0000000
#define WINDOWS_TO_40BIT_SIZE            0x20000000
// Within WINDOWS_TO_40BIT_ADDR, windows to RT_MMU_TEST_DATA_HI_x
#define RT_MMU_TEST_DATA_HI_0_WIN_ADDR   0xc0000000
#define RT_MMU_TEST_DATA_HI_1_WIN_ADDR   0xc1000000
// Two regions of same size
#define RT_MMU_TEST_DATA_HI_0_ADDR      0x100000000
#define RT_MMU_TEST_DATA_HI_1_ADDR      0x100010000
#define RT_MMU_TEST_DATA_HI_SIZE            0x10000

#define MMU_TEST_DATA_HI_0_ADDR        0x0500000000
#define MMU_TEST_DATA_HI_1_ADDR        0x0500001000
#define MMU_TEST_DATA_HI_SIZE              0x001000

// Boot config (TODO: to be replaced by u-boot env/script)
#define HPPS_DDR_ADDR__HPPS_SMP__BOOT_MODE 0xc001fffc

// Shared memory regions accessible to HPPS
#define HPPS_DDR_ADDR__SHM__HPPS_SMP 0xc3400000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP   0x400000
// HPPS userspace <-> TRCH SSW
#define HPPS_DDR_ADDR__SHM__HPPS_SMP_APP__TRCH_SSW 0xc3600000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP_APP__TRCH_SSW    0x10000
#define HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_APP 0xc3610000
#define HPPS_DDR_SIZE__SHM__TRCH_SSW__HPPS_SMP_APP    0x10000
// HPPS SSW <-> TRCH SSW
#define HPPS_DDR_ADDR__SHM__HPPS_SMP_SSW__TRCH_SSW 0xc39f0000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP_SSW__TRCH_SSW    0x08000
#define HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_SSW 0xc39f8000
#define HPPS_DDR_SIZE__SHM__TRCH_SSW__HPPS_SMP_SSW    0x08000

// Page table should be in memory that is on a bus accessible from the MMUs
// master port ('dma' prop in MMU node in Qemu DT).  We put it in HPPS DRAM,
// because that seems to be the only option, judging from high-level Chiplet
// diagram.
#define RTPS_HPPS_PT_ADDR                0xc3e00000
#define RTPS_HPPS_PT_SIZE                  0x1ff000

#define RT_MMU_TEST_DATA_LO_ADDR         0xc3fff000
#define RT_MMU_TEST_DATA_LO_SIZE           0x001000

#define MMU_TEST_DATA_LO_0_ADDR          0xc2ffe000
#define MMU_TEST_DATA_LO_1_ADDR          0xc2fff000
#define MMU_TEST_DATA_LO_SIZE              0x001000

#endif // MEM_MAP_H
