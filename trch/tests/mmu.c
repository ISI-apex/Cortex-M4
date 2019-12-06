#include <stdint.h>

#include "console.h"
#include "hwinfo.h"
#include "mem-map.h"
#include "mmus.h"

#include "test.h"

#if MMU_TEST_REGION_SIZE > RT_MMU_TEST_DATA_LO_SIZE
#error MMU_TEST_REGION greater than scratch space RT_MMU_TEST_DATA_LO_SIZE
#endif

int test_rt_mmu()
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
