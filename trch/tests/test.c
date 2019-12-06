#include "test.h"

/* Run tests whose only dependency is at most the systick timer */
int test_standalone()
{
    int rc;
    (void)rc;

#if TEST_ETIMER
    rc = test_etimer();
    if (rc) return rc;
#endif /* TEST_ETIMER */

#if TEST_RTI_TIMER
    rc = test_core_rti_timer();
    if (rc) return rc;
#endif /* TEST_RTI_TIMER */

#if TEST_WDTS
    rc = test_wdts();
    if (rc) return rc;
#endif /* TEST_WDTS */

#if TEST_FLOAT
    rc = test_float();
    if (rc) return rc;
#endif /* TEST_FLOAT */

#if TEST_TRCH_DMA
    rc = test_trch_dma();
    if (rc) return rc;
#endif /* TEST_TRCH_DMA */

#if TEST_SHMEM
    rc = test_shmem();
    if (rc) return rc;
#endif /* TEST_SHMEM */

    return 0;
}
