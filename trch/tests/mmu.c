#include <stdint.h>
#include <stdio.h>

#include "printf.h"
#include "hwinfo.h"
#include "mem-map.h"
#include "mmu.h"

#include "test.h"

int test_rt_mmu()
{
    // In this test, TRCH accesses same location as RTPS but via a different virtual addr.
    // RTPS should read 0xc0000000 and find 0xbeeff00d, not 0xf00dbeef.

    // Mappings are setup in trch/mmus.c since no easy way to keep this test
    // truly standalone, since the mappings for this test and for the MMU
    // usecase need to be active at the same time (since RTPS needs to
    // excercise these mappings when it boots).

    volatile uint32_t *addr = (volatile uint32_t *)RT_MMU_TEST_DATA_HI_1_WIN_ADDR;
    uint32_t val = 0xbeeff00d;
    printf("%p <- %08lx\r\n", addr, val);
    *addr = val;

    addr = (volatile uint32_t *)RT_MMU_TEST_DATA_HI_0_WIN_ADDR;
    val = 0xf00dbeef;
    printf("%p <- %08lx\r\n", addr, val);
    *addr = val;

    return 0;
}

int test_32_mmu_access_physical_mwr(uint32_t virt_write_addr, uint32_t phy_read_addr, unsigned mapping_sz){
    //map-write-read test
    //1. TRCH creates a mapping
    //2. TRCH enables MMU
    //3. TRCH writes to an address in the mapping created
    //4. TRCH disables MMU
    //5. TRCH reads the physical address where the data was written to and checks if the data is correct

    //Test is successful if TRCH reads the correct data


    _Bool success = 0;

    struct mmu *mmu_32;
    struct mmu_context *trch_ctx;
    struct mmu_stream *trch_stream;
    struct balloc *ba;

    uint32_t val = 0xbeeff00d;
    uint32_t old_val;

    mmu_32 = mmu_create("RTPS/TRCH->HPPS", RTPS_TRCH_TO_HPPS_SMMU_BASE);

    if (!mmu_32)
        return 1;

    mmu_disable(mmu_32); // might be already enabled if the core reboots

    ba = balloc_create("32", (uint64_t *)RTPS_HPPS_PT_ADDR, RTPS_HPPS_PT_SIZE);

    if (!ba)
        goto cleanup_balloc;

    trch_ctx = mmu_context_create(mmu_32, ba, MMU_PAGESIZE_4KB);
    if (!trch_ctx) {
        printf("test mmu 32-bit access physical map-write-read: trch context create failed\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("test mmu 32-bit access physical map-write-read: trch stream create failed\n");
        goto cleanup_stream;
    }

    if (mmu_map(trch_ctx, virt_write_addr, phy_read_addr,
                          mapping_sz)) {
        printf("test mmu 32-bit access physical map-write-read: mapping create failed\n");
        goto cleanup_map;
    }

    mmu_enable(mmu_32);

    old_val = *((uint32_t*)virt_write_addr);
    *((uint32_t*)virt_write_addr) = val;

    mmu_disable(mmu_32);

    if (*((uint32_t*)phy_read_addr) == val) {
        success = 1;
    }
    else {
        printf("test mmu 32-bit access physical map-write-read: read wrong value\n");
        success = 0;
    }

    mmu_enable(mmu_32);

    *((uint32_t*)virt_write_addr) = old_val;

    mmu_disable(mmu_32);

    mmu_unmap(trch_ctx, virt_write_addr, mapping_sz);
cleanup_map:
    mmu_stream_destroy(trch_stream);
cleanup_stream:
    mmu_context_destroy(trch_ctx);
cleanup_context:
    balloc_destroy(ba);
cleanup_balloc:
    mmu_destroy(mmu_32);

    if (success) {
        return 0;
    }
    return 1;
}

int test_32_mmu_access_physical_wmr(uint32_t virt_read_addr, uint32_t phy_write_addr, unsigned mapping_sz){
    //write-map-read test
    //1. TRCH creates a mapping
    //2. TRCH writes to a low physical address
    //3. TRCH enables MMU
    //4. TRCH reads the virtual address corresponding to the physical address written to and checks if data is correct

    //Test is successful if TRCH reads the correct data


    _Bool success = 0;

    struct mmu *mmu_32;
    struct mmu_context *trch_ctx;
    struct mmu_stream *trch_stream;
    struct balloc *ba;

    uint32_t val = 0xbeeff00d;
    uint32_t old_val;

    mmu_32 = mmu_create("RTPS/TRCH->HPPS", RTPS_TRCH_TO_HPPS_SMMU_BASE);

    if (!mmu_32)
        return 1;

    mmu_disable(mmu_32); // might be already enabled if the core reboots

    ba = balloc_create("32", (uint64_t *)RTPS_HPPS_PT_ADDR, RTPS_HPPS_PT_SIZE);

    if (!ba)
        goto cleanup_balloc;

    trch_ctx = mmu_context_create(mmu_32, ba, MMU_PAGESIZE_4KB);
    if (!trch_ctx) {
        printf("test mmu 32-bit access physical write-map-read: trch context create failed\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("test mmu 32-bit access physical write-map-read: trch stream create failed\n");
        goto cleanup_stream;
    }

    old_val = *((uint32_t*)phy_write_addr);
    *((uint32_t*)phy_write_addr) = val;

    if (mmu_map(trch_ctx, virt_read_addr, phy_write_addr,
                          mapping_sz)) {
        printf("test mmu 32-bit access physical write-map-read: mapping create failed\n");
        goto cleanup_map;
    }


    mmu_enable(mmu_32);

    if (*((uint32_t*)virt_read_addr) == val) {
        success = 1;
    }
    else {
        printf("test mmu 32-bit access physical write-map-read: read wrong value\n");
        success = 0;
    }

    *((uint32_t*)virt_read_addr) = old_val;

    mmu_disable(mmu_32);

    mmu_unmap(trch_ctx, virt_read_addr, mapping_sz);
cleanup_map:
    mmu_stream_destroy(trch_stream);
cleanup_stream:
    mmu_context_destroy(trch_ctx);
cleanup_context:
    balloc_destroy(ba);
cleanup_balloc:
    mmu_destroy(mmu_32);

    if (success) {
        return 0;
    }
    return 1;
}

int test_mmu_mapping_swap(uint32_t virt_write_addr, uint32_t virt_read_addr, uint64_t phy_addr, unsigned mapping_sz){
    //In this test,
    //1. TRCH creates a mapping from virt_write_addr to phy_addr
    //2. TRCH enables MMU
    //3. TRCH writes data to virt_write_addr (effectively writing to phy_addr)
    //4. TRCH unmaps the mapping created in step 1
    //5. TRCH maps virt_read_addr to phy_addr
    //6. TRCH reads virt_read_addr, checking if data is same as in step 3

    //Test is successful if TRCH reads the correct data


    _Bool success = 0;

    struct mmu *mmu_32;
    struct mmu_context *trch_ctx;
    struct mmu_stream *trch_stream;
    struct balloc *ba;

    uint32_t val = 0xbeeff00d;
    uint32_t old_val;

    mmu_32 = mmu_create("RTPS/TRCH->HPPS", RTPS_TRCH_TO_HPPS_SMMU_BASE);

    if (!mmu_32)
        return 1;

    mmu_disable(mmu_32); // might be already enabled if the core reboots

    ba = balloc_create("32", (uint64_t *)RTPS_HPPS_PT_ADDR, RTPS_HPPS_PT_SIZE);

    if (!ba)
        goto cleanup_balloc;

    trch_ctx = mmu_context_create(mmu_32, ba, MMU_PAGESIZE_4KB);
    if (!trch_ctx) {
        printf("test mmu mapping swap: trch context create failed\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("test mmu mapping swap: trch stream create failed\n");
        goto cleanup_stream;
    }

    if (mmu_map(trch_ctx, virt_write_addr, phy_addr,
                          mapping_sz)) {
        printf("test mmu mapping swap: addr_from_1->addr_to mapping create failed\n");
        goto cleanup_map;
    }

    mmu_enable(mmu_32);

    old_val = *((uint32_t*)virt_write_addr);
    *((uint32_t*)virt_write_addr) = val;

    mmu_disable(mmu_32);

    mmu_unmap(trch_ctx, virt_write_addr, mapping_sz);

    if (mmu_map(trch_ctx, virt_read_addr, phy_addr,
                          mapping_sz)) {
        printf("test mmu mapping swap: addr_from_1->addr_to mapping create failed\n");
        goto cleanup_map;
    }

    mmu_enable(mmu_32);

    if (*((uint32_t*)virt_read_addr) == val) {
        success = 1;
    }
    else {
        printf("test mmu mapping swap: read wrong value %lx \n", *((uint32_t*)virt_read_addr));
        success = 0;
    }

    *((uint32_t*)virt_read_addr) = old_val;

    mmu_disable(mmu_32);

    mmu_unmap(trch_ctx, virt_read_addr, mapping_sz);
cleanup_map:
    mmu_stream_destroy(trch_stream);
cleanup_stream:
    mmu_context_destroy(trch_ctx);
cleanup_context:
    balloc_destroy(ba);
cleanup_balloc:
    mmu_destroy(mmu_32);

    if (success) {
        return 0;
    }
    return 1;
}
