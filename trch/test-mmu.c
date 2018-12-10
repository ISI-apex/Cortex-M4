#include <stdint.h>

#include "printf.h"
#include "hwinfo.h"
#include "dram-map.h"
#include "mmu.h"

#include "test.h"

// A macro for testing convenience. Regions don't have to be the same.
#define MMU_TEST_REGION_SIZE 0x10000

int test_rt_mmu()
{
    struct mmu *rt_mmu = mmu_create("RTPS/TRCH->HPPS (test)",
		RTPS_TRCH_TO_HPPS_SMMU_BASE);
    if (!rt_mmu)
	return 1;

    // In this test, we share one allocator for all contexts
    struct balloc *ba = balloc_create("RT",
		(uint64_t *)RTPS_HPPS_PT_ADDR, RTPS_HPPS_PT_SIZE);
    if (!ba)
        return 1;

    struct mmu_context *rtps_ctx = mmu_context_create(rt_mmu, ba, MMU_PAGESIZE_4KB);
    if (!rtps_ctx)
	return 1;

    if (mmu_map(rtps_ctx, 0x8e100000,  0x8e100000, MMU_TEST_REGION_SIZE))
	return 1;
    if (mmu_map(rtps_ctx, 0xc0000000, 0x100000000, MMU_TEST_REGION_SIZE))
	return 1;

    struct mmu_stream *rtps_stream =
		mmu_stream_create(MASTER_ID_RTPS_CPU0, rtps_ctx);
    if (!rtps_stream)
	return 1;

    struct mmu_context *trch_ctx = mmu_context_create(rt_mmu, ba, MMU_PAGESIZE_4KB);
    if (!trch_ctx)
	return 1;

    if (mmu_map(trch_ctx, 0xc0000000, 0x100010000, MMU_TEST_REGION_SIZE))
	return 1;
    if (mmu_map(trch_ctx, 0xc1000000, 0x100000000, MMU_TEST_REGION_SIZE))
	return 1;

    struct mmu_stream *trch_stream =
		mmu_stream_create(MASTER_ID_TRCH_CPU, trch_ctx);
    if (!trch_stream)
	return 1;

    // In an alternative test, both streams could point to the same context

    mmu_enable(rt_mmu);

    // In this test, TRCH accesses same location as RTPS but via a different virtual addr.
    // RTPS should read 0xc0000000 and find 0xbeeff00d, not 0xf00dbeef.

    volatile uint32_t *addr = (volatile uint32_t *)0xc1000000;
    uint32_t val = 0xbeeff00d;
    printf("%p <- %08x\r\n", addr, val);
    *addr = val;

    addr = (volatile uint32_t *)0xc0000000;
    val = 0xf00dbeef;
    printf("%p <- %08x\r\n", addr, val);
    *addr = val;

    mmu_disable(rt_mmu);

    if (mmu_stream_destroy(rtps_stream))
	return 1;

    if (mmu_unmap(rtps_ctx, 0xc0000000, MMU_TEST_REGION_SIZE))
	return 1;
    if (mmu_unmap(rtps_ctx, 0x8e100000,  MMU_TEST_REGION_SIZE))
	return 1;

    if (mmu_context_destroy(rtps_ctx))
	return 1;

    if (mmu_stream_destroy(trch_stream))
	return 1;

    if (mmu_unmap(trch_ctx, 0xc1000000, MMU_TEST_REGION_SIZE))
	return 1;
    if (mmu_unmap(trch_ctx, 0xc0000000, MMU_TEST_REGION_SIZE))
	return 1;

    if (mmu_context_destroy(trch_ctx))
	return 1;

    if (mmu_destroy(rt_mmu))
    balloc_destroy(ba);
    return 0;
}
