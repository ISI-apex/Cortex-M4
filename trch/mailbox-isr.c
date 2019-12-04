#include "mailbox.h"
#include "mailbox-map.h"
#include "console.h"


void mbox_lsio_rcv_isr()
{
    mbox_rcv_isr(LSIO_MBOX0_INT_EVT0__TRCH_SSW);
}
void mbox_lsio_ack_isr()
{
    mbox_ack_isr(LSIO_MBOX0_INT_EVT1__TRCH_SSW);
}
void mbox_hpps_rcv_isr()
{
    mbox_rcv_isr(HPPS_MBOX0_INT_EVT0__TRCH_SSW);
}
void mbox_hpps_ack_isr()
{
    mbox_ack_isr(HPPS_MBOX0_INT_EVT1__TRCH_SSW);
}
