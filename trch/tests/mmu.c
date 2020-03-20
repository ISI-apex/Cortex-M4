#include <stdint.h>

#include "console.h"
#include "hwinfo.h"
#include "mem-map.h"
#include "mmus.h"
#include "mmu.h"
#include "panic.h"

#include "test.h"

#if TEST_RT_MMU_BASIC
static int test_rt_mmu_basic()
{
    int rc;

    rc = rt_mmu_map(RT_MMU_TEST_DATA_HI_0_WIN_ADDR, RT_MMU_TEST_DATA_HI_0_ADDR,
                RT_MMU_TEST_DATA_HI_SIZE);
    if (rc) goto cleanup_hi1_win;
    rc = rt_mmu_map(RT_MMU_TEST_DATA_HI_1_WIN_ADDR, RT_MMU_TEST_DATA_HI_1_ADDR,
                RT_MMU_TEST_DATA_HI_SIZE);
    if (rc) goto cleanup_hi0_win;

    /* In this test, TRCH accesses same location as RTPS but via a different
     * virtual addr. RTPS should read 0xc0000000 and find 0xbeeff00d, not
     * 0xf00dbeef. */

    volatile uint32_t *addr = (volatile uint32_t *)RT_MMU_TEST_DATA_HI_0_WIN_ADDR;
    uint32_t val = 0xbeeff00d;
    printf("%p <- %08x\r\n", addr, val);
    *addr = val;
    if (*addr != val) {
        printf("TEST: rt mmu: FAILED: read %x (expected %x)\r\n", *addr, val);
        return 1;
    }

    addr = (volatile uint32_t *)RT_MMU_TEST_DATA_HI_1_WIN_ADDR;
    val = 0xf00dbeef;
    printf("%p <- %08x\r\n", addr, val);
    *addr = val;
    if (*addr != val) {
        printf("TEST: rt mmu: FAILED: read %x (expected %x)\r\n", *addr, val);
        return 1;
    }

    /* Can't unmap, because the part of this test running on RTPS must then use
     * these mappings. TODO: state machine to know when test is done (see
     * actors in event.h). */
    return 0;

cleanup_hi0_win:
    rt_mmu_unmap(RT_MMU_TEST_DATA_HI_0_WIN_ADDR, RT_MMU_TEST_DATA_HI_SIZE);
cleanup_hi1_win:
    return rc;
}
#endif /* TEST_RT_MMU_BASIC */

#if TEST_RT_MMU_32_ACCESS_PHYSICAL
static int test_rt_mmu_32_access_physical_mwr(uint32_t virt_write_addr,
        uint32_t phy_read_addr, unsigned mapping_sz) {
    //map-write-read test
    //1. TRCH creates a mapping
    //2. TRCH enables MMU
    //3. TRCH writes to an address in the mapping created
    //4. TRCH disables MMU
    //5. TRCH reads the physical address where the data was written to and
    //   checks if the data is correct

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
        printf("TEST: rt mmu: 32-bit phys map-write-read: trch context create failed\r\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("TEST: rt mmu: 32-bit phys map-write-read: trch stream create failed\r\n");
        goto cleanup_stream;
    }

    if (mmu_map(trch_ctx, virt_write_addr, phy_read_addr,
                          mapping_sz)) {
        printf("TEST: rt mmu: 32-bit phys map-write-read: mapping create failed\r\n");
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
        printf("TEST: rt mmu: 32-bit phys map-write-read: read wrong value\r\n");
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

static int test_rt_mmu_32_access_physical_wmr(uint32_t virt_read_addr, uint32_t
        phy_write_addr, unsigned mapping_sz) {
    //write-map-read test
    //1. TRCH creates a mapping
    //2. TRCH writes to a low physical address
    //3. TRCH enables MMU
    //4. TRCH reads the virtual address corresponding to the physical address
    //   written to and checks if data is correct

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
        printf("TEST: rt mmu: 32-bit phys write-map-read: "
               "trch context create failed\r\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("TEST: rt mmu: 32-bit phys write-map-read: "
               "trch stream create failed\r\n");
        goto cleanup_stream;
    }

    old_val = *((uint32_t*)phy_write_addr);
    *((uint32_t*)phy_write_addr) = val;

    if (mmu_map(trch_ctx, virt_read_addr, phy_write_addr,
                          mapping_sz)) {
        printf("TEST: rt mmu: 32-bit phys write-map-read: "
               "mapping create failed\r\n");
        goto cleanup_map;
    }


    mmu_enable(mmu_32);

    if (*((uint32_t*)virt_read_addr) == val) {
        success = 1;
    }
    else {
        printf("TEST: rt mmu: 32-bit phys write-map-read: read wrong value\r\n");
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
#endif /* TEST_RT_MMU_32_ACCESS_PHYSICAL */

#if TEST_RT_MMU_MAPPING_SWAP
static int test_rt_mmu_mapping_swap(uint32_t virt_write_addr, uint32_t
        virt_read_addr, uint64_t phy_addr, unsigned mapping_sz) {
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
        printf("TEST: rt mmu: swap: trch context create failed\r\n");
        goto cleanup_context;
    }

    trch_stream = mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream) {
        printf("TEST: rt mmu: swap: trch stream create failed\r\n");
        goto cleanup_stream;
    }

    if (mmu_map(trch_ctx, virt_write_addr, phy_addr,
                          mapping_sz)) {
        printf("TEST: rt mmu: swap: "
               "addr_from_1->addr_to mapping create failed\r\n");
        goto cleanup_map;
    }

    mmu_enable(mmu_32);

    old_val = *((uint32_t*)virt_write_addr);
    *((uint32_t*)virt_write_addr) = val;

    mmu_disable(mmu_32);

    mmu_unmap(trch_ctx, virt_write_addr, mapping_sz);

    if (mmu_map(trch_ctx, virt_read_addr, phy_addr,
                          mapping_sz)) {
        printf("TEST: rt mmu: swap: "
               "addr_from_1->addr_to mapping create failed\r\n");
        goto cleanup_map;
    }

    mmu_enable(mmu_32);

    if (*((uint32_t*)virt_read_addr) == val) {
        success = 1;
    }
    else {
        printf("TEST: rt mmu: swap: read wrong value %lx\r\n",
                *((uint32_t*)virt_read_addr));
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
#endif /* TEST_RT_MMU_MAPPING_SWAP */

int test_rt_mmu()
{
#if TEST_RT_MMU_BASIC
    if (test_rt_mmu_basic())
        panic("RTPS/TRCH-HPPS MMU basic test");
#endif /* TEST_RT_MMU_BASIC */

#if TEST_RT_MMU_32_ACCESS_PHYSICAL
    if (test_rt_mmu_32_access_physical_mwr(RT_MMU_TEST_DATA_LO_0_ADDR,
                RT_MMU_TEST_DATA_LO_1_ADDR, RT_MMU_TEST_DATA_LO_SIZE))
        panic("TEST: rt mmu: map-write-read");
    else
        printf("TEST: rt mmu: map-write-read: success\r\n");

    if (test_rt_mmu_32_access_physical_wmr(RT_MMU_TEST_DATA_LO_0_ADDR,
                RT_MMU_TEST_DATA_LO_1_ADDR, RT_MMU_TEST_DATA_LO_SIZE))
        panic("TEST: rt mmu: write-map-read");
    else
        printf("TEST: rt mmu: write-map-read: success\r\n");
#endif /* TEST_RT_MMU_32_ACCESS_PHYSICAL */

#if TEST_RT_MMU_MAPPING_SWAP
    //map argument 1 to argument 3 then argument 2 to argument 3
    if (test_rt_mmu_mapping_swap(RT_MMU_TEST_DATA_LO_0_ADDR,
                RT_MMU_TEST_DATA_LO_1_ADDR, RT_MMU_TEST_DATA_HI_0_ADDR,
                RT_MMU_TEST_DATA_LO_SIZE))
        panic("TEST: rt mmu: mapping swap");
    else
        printf("TEST: rt mmu: mapping swap: success\r\n");
#endif /* TEST_RT_MMU_MAPPING_SWAP */

    return 0;
}

