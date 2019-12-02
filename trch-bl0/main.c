#define DEBUG 1

#include <stdbool.h>
#include <stdint.h>

#include "arm.h"
#include "board.h"
#include "console.h"
#include "hwinfo.h"
#include "debug.h"
#include "smc.h"
#include "sha256.h"
#include "ecc.h"
#include "panic.h"

#if DEBUG
#define PANICX PANIC
#else /* !DEBUG (no console) */
#define PANICX(...) while (1)
#endif /* !DEBUG */

#define VECTOR_TABLE_SIZE 0x400
#define STACK_POINTER	0xffffc

#define SHA256_CHECKSUM_SIZE 32

/*
   Boot Select Code:
       bs[0]: 0: SRAM; 1: SPI
       bs[1]: 0: rank0; 1: rank1
       bs[2]: 0: NOR; 1: MRAM
       bs[3]: 0: offset1; 1: offset2
       bs[4]: 0: failover disabled; 1: failover enabled
       bs[5]: 0: parity (odd); 1: parity (even)
*/
#define BS_INTERFACE_SHIFT 0
#define BS_RANK_SHIFT 1
#define BS_SRAM_TYPE_SHIFT 2
#define BS_CONFIGURATION_OFFSET_SHIFT 3
#define BS_FAILOVER_SHIFT 4
#define BS_PARITY_SHIFT 5

#define BS_SIZE 6
#define BS_MASK(a, b) (a & (1 << b))
#define BS_SRAM_INTERFACE 		(0 << BS_INTERFACE_SHIFT)
#define BS_SPI_INTERFACE 		(1 << BS_INTERFACE_SHIFT)
#define BS_SRAM_NOR 			(0 << BS_SRAM_TYPE_SHIFT)
#define BS_SRAM_MRAM 			(1 << BS_SRAM_TYPE_SHIFT)
#define BS_MEM_RANK0 			(0 << BS_RANK_SHIFT)
#define BS_MEM_RANK1 			(1 << BS_RANK_SHIFT)
#define BS_CONFIGURATION_OFFSET0 	(0 << BS_CONFIGURATION_OFFSET_SHIFT)
#define BS_CONFIGURATION_OFFSET1 	(1 << BS_CONFIGURATION_OFFSET_SHIFT)
#define BS_FAILOVER_DISABLE		(0 << BS_FAILOVER_SHIFT)
#define BS_FAILOVER_ENABLE		(1 << BS_FAILOVER_SHIFT)
#define BS_PARITY_ODD 			(0 << BS_PARITY_SHIFT)
#define BS_PARITY_EVEN			(1 << BS_PARITY_SHIFT)

enum boot_sel_mem_type {
    BOOT_SEL_MEM_TYPE_NOR = 0,
    BOOT_SEL_MEM_TYPE_MRAM = 1,
};

extern void relocate_code (uint32_t);
extern void clean_and_jump (uint32_t, uint32_t);

/* Failover: For initial draft */
#define NUM_FAILOVER_MEM_RANKS 2
#define NUM_BLOB_COPIES	0x2
static uint32_t blob_offsets[NUM_BLOB_COPIES] = {
    CONFIG_CFG_ADDR_0,
    CONFIG_CFG_ADDR_1
};

typedef struct {
    uint32_t bl1_size;
    uint32_t bl1_offset;     /* where copies of bl1 image is stored in NVRAM */
    uint32_t bl1_load_addr; /* where bl1 image is loaded */
    uint32_t bl1_entry_offset; /* entry offset from the start of the bl1 image */
    unsigned char checksum[SHA256_CHECKSUM_SIZE]; /* checksum of the bl1 image */
    unsigned char ecc[ECC_512_SIZE]; /* ecc of the bl0_blob except ecc */
} bl0_blob;

static void show_config(bl0_blob *cfg)
{
    int i;
    DPRINTF("========== Config Blob ==========\r\n");
    DPRINTF("bl1_size = 0x%x\r\n", cfg->bl1_size);
    DPRINTF("bl1_img_addr = 0x%x\r\n", cfg->bl1_offset);
    DPRINTF("bl1_load_addr = 0x%x\r\n", cfg->bl1_load_addr);
    DPRINTF("bl1_entry_offset = 0x%x\r\n", cfg->bl1_entry_offset);
    DPRINTF("bl1_checksum (sha256) = ");
    for (i = 0; i < SHA256_CHECKSUM_SIZE; i++)
        DPRINTF("%02x", cfg->checksum[i]);
    DPRINTF("\r\n");
    DPRINTF("ecc = 0x%02x 0x%02x 0x%02x\r\n",
        cfg->ecc[0], cfg->ecc[1], cfg->ecc[2]);
}

static inline int diff_checksum(unsigned char * a, unsigned char * b) {
    int i;
    for (i = 0; i < SHA256_CHECKSUM_SIZE; i++) {
        if (a[i] != b[i]) return 1;
    }
    DPRINTF("BL0: checksum success\r\n");
    return 0;
}

/* copied and modified from lib/sfs.c */
static int load_memcpy(uint32_t *mem_addr, uint32_t *load_addr, unsigned size)
{
    unsigned w, b;

    uint32_t nwords = size / sizeof(uint32_t);
    uint32_t rem_bytes = (size) % sizeof(uint32_t);

    for (w = 0; w < nwords; w++) {
        * load_addr = * mem_addr;
        load_addr++;
        mem_addr++;
    }
    uint8_t *load_addr_8 = (uint8_t *) load_addr;
    uint8_t *mem_addr_8 = (uint8_t *) mem_addr;
    for (b = 0; b < rem_bytes; b++) {
        * load_addr_8 = * mem_addr_8;
        load_addr_8++;
        mem_addr_8++;
    }
    return 0;
}

static int parity_check(uint8_t data)
{
    int i, j, count;
    for (i = count = 0, j = 1; i < BS_SIZE; i++) {
            count += (data & j) >> i;
            j <<= 1;
    }
    if ((count & 1) == 0) return -1;
    return 0;
}

#if 0 /* TODO */
static void invalidate_icache()
{
    uint32_t *iciallu = (uint32_t *) 0xE000EF50;

    *iciallu = 0x0;

    asm("DSB");
    asm("ISB");
}
#endif

static uint8_t *get_smc_sram_rank_addr(struct smc *smc, unsigned rank)
{
#if 0 /* TODO: implement in SMC model */
    return smc_get_base_addr(smc, SMC_IFACE_SRAM, rank);
#else
    switch (rank){
        case 0: return (uint8_t *)SMC_LSIO_SRAM_BASE0;
        case 1: return (uint8_t *)SMC_LSIO_SRAM_BASE1;
        case 2: return (uint8_t *)SMC_LSIO_SRAM_BASE2;
        case 3: return (uint8_t *)SMC_LSIO_SRAM_BASE3;
        default:
            PANICX("rank index out of range");
    }
#endif
    return NULL; /* unreachable */
}

int main ( void )
{
    int mem_rank;
    unsigned mem_chip, mem_chip_backup;
    uint8_t * load_addr, * mem_addr, * mem_base_addr;
    uint8_t boot_select;
    enum boot_sel_mem_type mem_type;
    uint8_t chip_mask;
    struct smc *smc;

    /* TODO: interrupt must be disabled here if it was enabled */

    /* TODO: setup watchdog? */

#if DEBUG /* console allowed only in debug mode */
    console_init();
#endif /* DEBUG */

    DPRINTF("BL0: entering priveleged mode: svc #0\r\n");
    asm("svc #0");

#ifdef CONFIG_BOOT_SELECT /* override, used for testing */
    boot_select = CONFIG_BOOT_SELECT;
#else
/* TODO */
#error NOT IMPLEMENTED: read of boot selection config from GPIO
#endif

    DPRINTF("BL0: boot select: 0x%x\r\n", boot_select);
    if (parity_check(boot_select) < 0) {
        /* TODO: should anything be fatal? maybe assume some default? */
        PANICX("BL0: FATAL: boot selector code parity check failed\r\n");
    }
    bool failover = BS_MASK(boot_select, BS_FAILOVER_SHIFT);
    /* 1. select the interface */
    if (BS_MASK(boot_select, BS_INTERFACE_SHIFT) == BS_SRAM_INTERFACE) {
        /* SMC */
        DPRINTF("BL0: initialize SMC\r\n");
        /* 3. Memory type: determines subset of memory chips */
        mem_type = BS_MASK(boot_select, BS_SRAM_TYPE_SHIFT);
        /* 2. Rank (index within ranks belonging to the mem type) */
        mem_rank = BS_MASK(boot_select, BS_RANK_SHIFT) >> BS_RANK_SHIFT;
        switch (mem_type) {
            case BOOT_SEL_MEM_TYPE_NOR:
                mem_chip = SMC_MEM_RANK_NOR_START + mem_rank;
                mem_chip_backup = SMC_MEM_RANK_NOR_START +
                    ((mem_rank + 1) % SMC_MEM_RANK_NOR_COUNT);
                break;
            case BOOT_SEL_MEM_TYPE_MRAM:
                mem_chip = SMC_MEM_RANK_MRAM_START + mem_rank;
                mem_chip_backup = SMC_MEM_RANK_MRAM_START +
                    ((mem_rank + 1) % SMC_MEM_RANK_MRAM_COUNT);
                break;
        }
        chip_mask = (1 << mem_chip) | (failover ? (1 << mem_chip_backup) : 0);
        DPRINTF("BL0: SMC interface(SRAM), rank mask(%x)\r\n", chip_mask);
        smc = smc_init(SMC_LSIO_CSR_BASE, &lsio_smc_mem_cfg,
                       SMC_IFACE_SRAM_MASK, chip_mask);
    } else {
        /* TODO: SPI */
    }

    /* load configuration blob */
    DPRINTF("BL0: read configuration blob\r\n");
    bl0_blob config_blob;
    unsigned char * ptr1 = (unsigned char *)&config_blob;
    unsigned char * ptr2 = (unsigned char *)config_blob.ecc;
    unsigned int data_size = (unsigned long) ptr2 - (unsigned long) ptr1; 
    unsigned char ecc_calc[ECC_512_SIZE];
    int i, j;
    int mem_ranks_trial = failover ? NUM_FAILOVER_MEM_RANKS : 1;
    int curr_mem_chip = mem_chip;

    for (i = 0; i < mem_ranks_trial; i++) {
        mem_base_addr = get_smc_sram_rank_addr(smc, curr_mem_chip);
        /* 
           fail-over within a memory rank. 
           two copies in a memory rank are assumed to exist.
         */
        for (j = 0; j < NUM_BLOB_COPIES; j++) {
            mem_addr = mem_base_addr + blob_offsets[j];

            DPRINTF("BL0: cp config_blob from 0x%x to %p\r\n",
                   mem_addr, &config_blob);
            load_memcpy((uint32_t *)(mem_addr), (uint32_t *)&config_blob,
                        sizeof(config_blob));
            show_config(&config_blob);

            calculate_ecc((unsigned char *)&config_blob, data_size, ecc_calc);
            DPRINTF("BL0: ecc %x %x %x\r\n",
                   ecc_calc[0], ecc_calc[1], ecc_calc[2]);
            if (correct_data((unsigned char *)&config_blob, config_blob.ecc,
                             ecc_calc, data_size) < 0) {
                continue;
            }

            /* 
             load BL1 image to temporary address (bl1_load_addr + bl1_entry_offset).
             This image is to be copied again.
             TODO: reduce the extra copy except the vector table.
             */
            load_addr = (uint8_t *) (config_blob.bl1_load_addr);
            load_addr += config_blob.bl1_entry_offset;
            mem_addr = mem_base_addr + config_blob.bl1_offset; /* + vtbl_size; */
            DPRINTF("BL0: load BL1 image (0x%x) to (0x%x), size(0x%x)\r\n",
                   mem_addr, load_addr, config_blob.bl1_size);
            load_memcpy((uint32_t *)mem_addr, (uint32_t *)load_addr,
                       config_blob.bl1_size);
   
            /* checksum */   
            unsigned char output[SHA256_CHECKSUM_SIZE];
            mbedtls_sha256_ret((unsigned char *) load_addr,
                               config_blob.bl1_size, output, false);
            if (diff_checksum(output, config_blob.checksum)) {
                DPRINTF("Checksum Failure\r\n");
                continue;
            }
            break;
        }
        if (j < NUM_BLOB_COPIES)
            break;
        curr_mem_chip = mem_chip_backup;
    }
    if (i == mem_ranks_trial) {
        /* TODO: should anything be fatal? maybe should keep looping? */
        PANICX("BL0: FATAL: all backup copies failed\r\n");
    }

    /* move bl1 image to the actual bl1_load_addr */ 
    load_addr = (uint8_t *) config_blob.bl1_load_addr;
    mem_addr = (uint8_t *) config_blob.bl1_load_addr;
    mem_addr += config_blob.bl1_entry_offset;
    DPRINTF("BL0: move BL1 image from (0x%x) to (0x%x), size(0x%x)\r\n",
           mem_addr, load_addr, config_blob.bl1_size);
    load_memcpy((uint32_t *)mem_addr, (uint32_t *)load_addr, config_blob.bl1_size);

    smc_deinit(smc);
 
    /* jump to new entry_offset of BL1 */
    DPRINTF("BL0: jump to the bootloader (0x%x)\r\n",
           config_blob.bl1_entry_offset);
    // invalidate_icache();
    clean_and_jump(config_blob.bl1_entry_offset, STACK_POINTER);

    PANICX("unreacheable");
    return 0; /* unreachable */
}
