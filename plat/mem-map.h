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
// SRC, DST: subsystem (enum sw_subsys in subsys.h)
// SW: the software component on SRC/DEST subsystem (enum sw_comp in susbys.h)

// TODO: Update remaining macros to use the naming convention

//////////
// RTPS //
//////////

// Page tables for RTPS MMU: in RPTS DRAM
#define RTPS_PT_ADDR 0x40060000
#define RTPS_PT_SIZE   0x200000

// DMA Microcode buffer: in RTPS DRAM (cannot be in TCM)
#define RTPS_DMA_MCODE_ADDR 0x40050000
#define RTPS_DMA_MCODE_SIZE 0x00001000

// Buffers for DMA test: in RTPS DRAM
#define RTPS_DMA_SRC_ADDR       0x40051000
#define RTPS_DMA_SIZE           0x00000200

#define RTPS_DMA_DST_ADDR       0x40052000 // align to page
#define RTPS_DMA_DST_REMAP_ADDR 0x40053000 // MMU test maps this to DST_ADDR

/* Allocations can overlap for subsystems that cannot run concurrently. */
/* Note: SMP OS uses one region -- synchronization up to the SW */
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP			  0x40260000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP			  0x00040000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0			  0x40260000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_0			  0x00020000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1			  0x40280000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_1			  0x00020000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SMP			  0x40260000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SMP			  0x00040000

// TRCH <-> RTPS_R52_{LOCKSTEP,SPLIT_{0,1},SMP}
#define RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW 	  0x40260000
#define RTPS_DDR_SIZE__SHM__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW 	  0x00008000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW 	  0x40268000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW 	  0x00008000
#define RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SPLIT_0_SSW  	  0x40260000
#define RTPS_DDR_SIZE__SHM__TRCH_SSW__RTPS_R52_SPLIT_0_SSW  	  0x00008000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0_SSW__TRCH_SSW  	  0x40268000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_0_SSW__TRCH_SSW  	  0x00008000
#define RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SPLIT_1_SSW  	  0x40260000
#define RTPS_DDR_SIZE__SHM__TRCH_SSW__RTPS_R52_SPLIT_1_SSW  	  0x00008000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1_SSW__TRCH_SSW  	  0x40268000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_1_SSW__TRCH_SSW  	  0x00008000
#define RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SMP_SSW      	  0x40280000
#define RTPS_DDR_SIZE__SHM__TRCH_SSW__RTPS_R52_SMP_SSW      	  0x00008000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SMP_SSW__TRCH_SSW      	  0x40288000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SMP_SSW__TRCH_SSW      	  0x00008000

// Shared memory regions accessible to RTPS, reserved but not allocated
#define RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP__FREE		  0x40280000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_LOCKSTEP__FREE		  0x00020000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0__FREE		  0x402a0000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_0__FREE		  0x00000000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1__FREE		  0x402a0000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SPLIT_1__FREE		  0x00000000
#define RTPS_DDR_ADDR__SHM__RTPS_R52_SMP__FREE			  0x40280000
#define RTPS_DDR_SIZE__SHM__RTPS_R52_SMP__FREE			  0x00020000

//////////
// HPPS //
//////////

#define WINDOWS_TO_40BIT_ADDR            0xc0000000
#define WINDOWS_TO_40BIT_SIZE            0x20000000

// Within WINDOWS_TO_40BIT_ADDR, windows to RT_MMU_TEST_DATA_HI_x
#define RT_MMU_TEST_DATA_HI_0_WIN_ADDR   0xc0000000
#define RT_MMU_TEST_DATA_HI_1_WIN_ADDR   0xc1000000

// Two regions of same size
#define RT_MMU_TEST_DATA_HI_0_ADDR      0x100000000
#define RT_MMU_TEST_DATA_HI_1_ADDR      0x100010000
#define RT_MMU_TEST_DATA_HI_SIZE            0x10000

// Page table should be in memory that is on a bus accessible from the MMUs
// master port ('dma' prop in MMU node in Qemu DT).  We put it in HPPS DRAM,
// because that seems to be the only option, judging from high-level Chiplet
// diagram.
#define RTPS_HPPS_PT_ADDR                0x87e00000
#define RTPS_HPPS_PT_SIZE                  0x1ff000

#define RT_MMU_TEST_DATA_LO_ADDR         0x87fff000
#define RT_MMU_TEST_DATA_LO_SIZE           0x001000

// Full HPPS DRAM
#define HPPS_DDR_LOW_ADDR__HPPS_SMP				  0x80000000
#define HPPS_DDR_LOW_SIZE__HPPS_SMP				  0x40000000

// Shared memory regions accessible to HPPS
#define HPPS_DDR_ADDR__SHM__HPPS_SMP				  0x87600000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP   				    0x400000
// HPPS userspace <-> TRCH SSW
#define HPPS_DDR_ADDR__SHM__HPPS_SMP_APP__TRCH_SSW		  0x87600000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP_APP__TRCH_SSW 		     0x10000
#define HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_APP 		  0x87610000
#define HPPS_DDR_SIZE__SHM__TRCH_SSW__HPPS_SMP_APP 		     0x10000
// HPPS SSW <-> TRCH SSW
#define HPPS_DDR_ADDR__SHM__HPPS_SMP_SSW__TRCH_SSW 		  0x879f0000
#define HPPS_DDR_SIZE__SHM__HPPS_SMP_SSW__TRCH_SSW 		     0x08000
#define HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_SSW 		  0x879f8000
#define HPPS_DDR_SIZE__SHM__TRCH_SSW__HPPS_SMP_SSW 		     0x08000

#endif // MEM_MAP_H
