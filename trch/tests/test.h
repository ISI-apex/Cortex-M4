#ifndef TEST_H
#define TEST_H

#include <stdbool.h>

struct dma;
struct rio_switch;
struct rio_ep;
struct ev_loop;
struct ev_actor;

int test_standalone();

int test_trch_dma();
int test_rt_mmu();
int test_float();
int test_systick();
int test_wdts();
int test_etimer();
int test_core_rti_timer();
int test_shmem();

enum test_rio_tests { /* bitmask */
    TEST_RIO_NONE       = 0,
    TEST_RIO_ALL        = ~0,
    TEST_RIO_ONEWAY     = 1 << 0, /* onchip only */
    TEST_RIO_CSR        = 1 << 1,
    TEST_RIO_MSG_RPC    = 1 << 2,
    TEST_RIO_MAP        = 1 << 3,
};

void test_rio_launch(struct ev_loop *ev_loop, bool master,
		struct rio_switch *sw, struct rio_ep *ep0, struct rio_ep *ep1,
		struct dma *dmac, unsigned chan,
		enum test_rio_tests onchip_tests, enum test_rio_tests offchip_tests);

#endif // TEST_H
