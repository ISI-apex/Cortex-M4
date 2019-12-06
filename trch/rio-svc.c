#include <stdint.h>
#include <stdbool.h>

#include "console.h"
#include "panic.h"
#include "nvic.h"
#include "rio-ep.h"
#include "rio-switch.h"
#include "rio-link.h"

#include "hwinfo.h"
#include "mem-map.h"

#include "rio-svc.h"

/* Configurable parameters chosen by this application */
#define MSG_SEG_SIZE 16
#define TRANSPORT_TYPE RIO_TRANSPORT_DEV8
#define ADDR_WIDTH RIO_ADDR_WIDTH_34_BIT
#define CFG_SPACE_SIZE 0x400000 /* for this app, care about only part of 16MB */

/* Normally these are assigned by discovery routine in the driver */
#define RIO_DEVID_EP0 0x0
#define RIO_DEVID_EP1 0x1

/* External endpoint to be recheable via the given port */
#define RIO_DEVID_EP_EXT 0x2
#define RIO_EP_EXT_SWITCH_PORT 2

#if 0 /* TODO */
#define CFG_SPACE_BASE { 0x0, 0x0 }
static const rio_addr_t cfg_space_base = CFG_SPACE_BASE;
#endif

static struct rio_svc *svc = NULL; /* We need a handle for ISRs */

int rio_svc_init(struct rio_svc *s, bool master)
{
    int rc = 1;
    printf("RIO: initialize\r\n");
    ASSERT(!svc); /* only one instance allowed */

    /* TODO: this should be done only upon subscribing to IRQ, but
       this requires an interrupt controller abstraction, which is
       not yet complete. */
    nvic_int_enable(TRCH_IRQ__RIO_EP_0_MSG_RX);
    nvic_int_enable(TRCH_IRQ__RIO_EP_1_MSG_RX);

    rc = rio_switch_init(&s->sw, "RIO_SW", RIO_SWITCH_BASE, /* local */ true);
    if (rc)
        goto fail_sw;

    /* Partition buffer memory evenly among the endpoints */
    const unsigned buf_mem_size = RIO_MEM_SIZE / 2;
    uint8_t *buf_mem_cpu = (uint8_t *)RIO_MEM_WIN_ADDR;
    rio_bus_addr_t buf_mem_ep = RIO_MEM_ADDR;

    ASSERT(0 < RIO_SVC_NUM_ENDPOINTS);
    rc = rio_ep_init(&s->eps[0], "RIO_EP0", RIO_EP0_BASE, RIO_EP0_OUT_AS_BASE,
            RIO_OUT_AS_WIDTH, TRANSPORT_TYPE, ADDR_WIDTH,
            buf_mem_ep, buf_mem_cpu, buf_mem_size);
    if (rc)
        goto fail_ep0;
    buf_mem_ep += buf_mem_size;
    buf_mem_cpu += buf_mem_size;

    ASSERT(1 < RIO_SVC_NUM_ENDPOINTS);
    rc = rio_ep_init(&s->eps[1], "RIO_EP1", RIO_EP1_BASE, RIO_EP1_OUT_AS_BASE,
             RIO_OUT_AS_WIDTH, TRANSPORT_TYPE, ADDR_WIDTH,
             buf_mem_ep, buf_mem_cpu, buf_mem_size);
    if (rc)
        goto fail_ep1;
    buf_mem_ep += buf_mem_size;
    buf_mem_cpu += buf_mem_size;

    if (master) {
        rio_ep_set_devid(&s->eps[0], RIO_DEVID_EP0);
        rio_ep_set_devid(&s->eps[1], RIO_DEVID_EP1);

        rio_switch_map_local(&s->sw, RIO_DEVID_EP0, RIO_MAPPING_TYPE_UNICAST,
                             (1 << RIO_EP0_SWITCH_PORT));
        rio_switch_map_local(&s->sw, RIO_DEVID_EP1, RIO_MAPPING_TYPE_UNICAST,
                             (1 << RIO_EP1_SWITCH_PORT));
        
        /* Discover algorithm would run here and assign Device IDs... */
    }

    svc = s;
    return 0;

fail_ep1:
    rio_ep_release(&s->eps[0]);
fail_ep0:
    rio_switch_release(&s->sw);
fail_sw:
    nvic_int_disable(TRCH_IRQ__RIO_EP_1_MSG_RX);
    nvic_int_disable(TRCH_IRQ__RIO_EP_0_MSG_RX);
    return rc;
}

void rio_svc_release(struct rio_svc *s)
{
    ASSERT(s);
    ASSERT(s == svc);

    nvic_int_disable(TRCH_IRQ__RIO_EP_1_MSG_RX);
    nvic_int_disable(TRCH_IRQ__RIO_EP_0_MSG_RX);

    rio_switch_unmap_local(&s->sw, RIO_DEVID_EP0);
    rio_switch_unmap_local(&s->sw, RIO_DEVID_EP1);

    rio_ep_set_devid(&s->eps[0], 0);
    rio_ep_set_devid(&s->eps[1], 0);

    rio_ep_release(&s->eps[1]);
    rio_ep_release(&s->eps[0]);
    rio_switch_release(&s->sw);

    svc = NULL;
}

/* This is helping out the driver to subscribe to IRQs, since
   our interrupt controller driver does not have a subscription API */
void rio_ep_0_msg_tx_isr() { ASSERT(svc); rio_ep_msg_tx_isr(&svc->eps[0]); }
void rio_ep_0_msg_rx_isr() { ASSERT(svc); rio_ep_msg_rx_isr(&svc->eps[0]); }
void rio_ep_1_msg_tx_isr() { ASSERT(svc); rio_ep_msg_tx_isr(&svc->eps[1]); }
void rio_ep_1_msg_rx_isr() { ASSERT(svc); rio_ep_msg_rx_isr(&svc->eps[1]); }
