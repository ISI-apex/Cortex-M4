#ifndef TEST_H
#define TEST_H

#include "dma.h"
#include "rti-timer.h"
#include "wdt.h"

struct link;

// Tests that create device instances are passed the location of the pointer
// to the device instance from where the ISR gets the pointer to the device.

int test_standalone();

int test_float();
int test_sort();
int test_gtimer();
int test_rt_mmu();
int test_rtps_mmu();
int test_rtps_trch_mailbox(struct link *trch_link);
int test_rtps_dma(struct dma **rtps_dma_ptr);
int test_wdt(struct wdt **wdt_ptr);
int test_core_rti_timer(struct rti_timer **tmr_ptr);

#endif // TEST_H
