#include "test.h"

/* Run tests whose only dependency is at most the system timer */
int test_standalone()
{
    int rc;

#if TEST_FLOAT
    rc = test_float();
    if (rc) return rc;
#endif /* TEST_FLOAT */

#if TEST_SORT
    rc = test_sort();
    if (rc) return rc;
#endif /* TEST_SORT */

#if TEST_RT_MMU
    rc = test_rt_mmu();
    if (rc) return rc;
#endif /* TEST_RT_MMU */

#if TEST_RTPS_MMU
    rc = test_rtps_mmu();
    if (rc) return rc;
#endif /* TEST_RT_MMU */

    return 0;
}
