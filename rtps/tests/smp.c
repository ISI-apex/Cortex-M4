#include <stdbool.h>
#include <stdint.h>

#include "command.h"
#include "gic.h"
#include "hwinfo.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "mutex.h"
#include "pm_defs.h"
#include "printf.h"
#include "test.h"

unsigned int start_r52_1 = 0;
uint32_t smp_mutex = unlocked;

int wakeup_r52_1() {
#define LSIO_RCV_IRQ_IDX  MBOX_LSIO__RTPS_RCV_INT
#define LSIO_ACK_IRQ_IDX  MBOX_LSIO__RTPS_ACK_INT

    struct mbox_link_dev mdev;
    mdev.base = MBOX_LSIO__BASE;
    mdev.rcv_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + LSIO_RCV_IRQ_IDX,
                               GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mdev.rcv_int_idx = LSIO_RCV_IRQ_IDX;
    mdev.ack_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + LSIO_ACK_IRQ_IDX,
                               GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mdev.ack_int_idx = LSIO_ACK_IRQ_IDX;

    struct link *rtps_link = mbox_link_connect("RTPS_TRCH_MBOX_PSCI_LINK",
                    &mdev, MBOX_LSIO__TRCH_RTPS_PSCI, MBOX_LSIO__RTPS_TRCH_PSCI,
                    /* server */ 0, /* client */ MASTER_ID_RTPS_CPU0);
    if (!rtps_link)
        return 1;

    uint32_t arg[] = { CMD_PSCI, NODE_RPU_0, PM_REQ_WAKEUP, NODE_RPU_1, 0x0, 0x0, 0x0};
    uint32_t reply[sizeof(arg) / sizeof(arg[0])] = {0};
    printf("arg len: %u\r\n", sizeof(arg) / sizeof(arg[0]));
    int rc = rtps_link->request(rtps_link,
                                CMD_TIMEOUT_MS_SEND, arg, sizeof(arg),
                                CMD_TIMEOUT_MS_RECV, reply, 0);
    if (rc <= 0)
        return rc;

    rc = rtps_link->disconnect(rtps_link);
    if (rc)
        return 1;
}

int test_r52_smp()
{
    int id;
    asm volatile ("mrc p15, 0, %0, c0, c0, 5":"=r" (id) :);
    if (id > 0) { /* SMP CPU-1 */
        printf("RTPS-1 is up and about to wake up RTPS-0\r\n");
        lock_mutex(&smp_mutex);
        start_r52_1 = 1;
        asm volatile ("dmb ");
        unlock_mutex(&smp_mutex);
        printf("RTPS-1 changes 'start_r52_1' value to %d\r\nRTPS-1 sleeps\r\n", start_r52_1);
        while(1) {
            asm volatile ("wfi");
	};
    } else {	/* SMP CPU-0 */
        lock_mutex(&smp_mutex);
        start_r52_1 = 0;
        unlock_mutex(&smp_mutex);
        printf("RTPS R52 SMP test\r\n");
        printf("RTPS-0: start_rt2_1 = %d\r\n", start_r52_1);
        printf("RTPS-0 wakes up RTPS-1\r\n");
        wakeup_r52_1();
        printf("RTPS-0 waits for RTPS-1's changing 'start_52_1' value to '1'\r\n");
        while (1) {
            asm volatile ("dmb ");
            lock_mutex(&smp_mutex);
            if (start_r52_1 == 1)
                break;
            unlock_mutex(&smp_mutex);
        }
        unlock_mutex(&smp_mutex);
        printf("\r\n\r\nRTPS-0 is waked up: start_r52_1 = %d\r\n\r\n", start_r52_1);
    }

}