#include "mmu.h"
#include "console.h"
#include "hwinfo.h"
#include "mem-map.h"
#include "test.h"

#define PAGESIZE_BITS 12 /* 4K */
#define PAGESIZE_ENUM MMU_PAGESIZE_4KB

int test_rtps_mmu()
{

    struct mmu *rtps_mmu = mmu_create("RTPS", RTPS_SMMU_BASE);
    if (!rtps_mmu)
	return 1;

    // In this test, we share one allocator for all contexts
    struct balloc *ba = balloc_create("RTPS",
		(uint64_t *)RTPS_PT_ADDR, RTPS_PT_SIZE);
    if (!ba)
        return 1;

    struct mmu_context *ctx = mmu_context_create(rtps_mmu, ba, PAGESIZE_ENUM);
    if (!ctx)
	return 1;

    if (mmu_map(ctx, RTPS_DMA_MCODE_ADDR, RTPS_DMA_MCODE_ADDR,
                ALIGN(RTPS_DMA_MCODE_SIZE, PAGESIZE_BITS)))
	return 1;
    if (mmu_map(ctx, RTPS_DMA_SRC_ADDR, RTPS_DMA_SRC_ADDR,
                ALIGN(RTPS_DMA_SIZE, PAGESIZE_BITS)))
	return 1;
    if (mmu_map(ctx, RTPS_DMA_DST_ADDR, RTPS_DMA_DST_ADDR,
                ALIGN(RTPS_DMA_SIZE, PAGESIZE_BITS)))
	return 1;
    if (mmu_map(ctx, RTPS_DMA_DST_REMAP_ADDR, RTPS_DMA_DST_ADDR,
                ALIGN(RTPS_DMA_SIZE, PAGESIZE_BITS)))
	return 1;

    struct mmu_stream *stream = mmu_stream_create(MMU_SID_RTPS_DMA, ctx);
    if (!stream)
	return 1;

    mmu_enable(rtps_mmu);

    // Do not destroy, leave setup for the DMA test

    return 0;
}
