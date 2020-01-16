#include "arm.h"
#include "gic.h"
#include "hwinfo.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "link.h"
#include "panic.h"
#include "subsys.h"

#include "links.h"

/* inferred */
#define CONFIG_MBOX_DEV_HPPS (CONFIG_HPPS_RTPS_MAILBOX)
#define CONFIG_MBOX_DEV_LSIO (CONFIG_RTPS_TRCH_MAILBOX)

#ifdef CONFIG_MBOX_DEV_LSIO
static struct mbox_link_dev mldev_trch;
#endif
#if CONFIG_MBOX_DEV_HPPS
static struct mbox_link_dev mldev_hpps;
#endif

#if CONFIG_RTPS_TRCH_MAILBOX
static struct link *trch_link;
#endif
#if CONFIG_HPPS_RTPS_MAILBOX
static struct link *hpps_smp_app_link;
#endif

/* panics on failure */
void links_init()
{
    /* Resources are allocated per an "owner" (i.e. a SW entity), and this
     * code runs in the context of one of several different entities. */
    enum sw_comp self_sw = SW_COMP_SSW;
    unsigned self_owner;
    unsigned trch_mbox[2], hpps_mbox[2]; /* {incoming, outgoing} */
    unsigned trch_mbox_ev[2], hpps_mbox_ev[2]; /* {rcv, ack} */

#if CONFIG_SMP
    /* Note that in SMP mode, the app uses one mailbox; synchronization
     * among the cores is up to the software (we only run stuff on primary). */
    self_owner = OWNER(SW_SUBSYS_RTPS_R52_SMP, self_sw);
    trch_mbox_ev[0] = LSIO_MBOX0_INT_EVT0__RTPS_R52_SMP_SSW;
    trch_mbox_ev[1] = LSIO_MBOX0_INT_EVT1__RTPS_R52_SMP_SSW;
    trch_mbox[0] = LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SMP_SSW;
    trch_mbox[1] = LSIO_MBOX0_CHAN__RTPS_R52_SMP_SSW__TRCH_SSW;

    hpps_mbox_ev[0] = HPPS_MBOX1_INT_EVT0__RTPS_R52_SMP_SSW;
    hpps_mbox_ev[1] = HPPS_MBOX1_INT_EVT1__RTPS_R52_SMP_SSW;
    hpps_mbox[0] = HPPS_MBOX1_CHAN__HPPS_SMP_APP__RTPS_R52_SMP_SSW;
    hpps_mbox[1] = HPPS_MBOX1_CHAN__RTPS_R52_SMP_SSW__HPPS_SMP_APP;
#else /* !CONFIG_SMP */
#if CONFIG_SPLIT /* same binary for either core; condition at runtime */
    unsigned core = self_core_id();
    ASSERT(core < RTPS_R52_NUM_CORES);
    switch (core) {
    case 0:
        self_owner = OWNER(SW_SUBSYS_RTPS_R52_SPLIT_0, self_sw);
        trch_mbox_ev[0] = LSIO_MBOX0_INT_EVT0__RTPS_R52_SPLIT_0_SSW;
        trch_mbox_ev[1] = LSIO_MBOX0_INT_EVT1__RTPS_R52_SPLIT_0_SSW;
        trch_mbox[0] = LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SPLIT_0_SSW;
        trch_mbox[1] = LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_0_SSW__TRCH_SSW;

        hpps_mbox_ev[0] = HPPS_MBOX1_INT_EVT0__RTPS_R52_SPLIT_0_SSW;
        hpps_mbox_ev[1] = HPPS_MBOX1_INT_EVT1__RTPS_R52_SPLIT_0_SSW;
        hpps_mbox[0] = HPPS_MBOX1_CHAN__HPPS_SMP_APP__RTPS_R52_SPLIT_0_SSW;
        hpps_mbox[1] = HPPS_MBOX1_CHAN__RTPS_R52_SPLIT_0_SSW__HPPS_SMP_APP;
        break;
    case 1:
        self_owner = OWNER(SW_SUBSYS_RTPS_R52_SPLIT_1, self_sw);
        trch_mbox_ev[0] = LSIO_MBOX0_INT_EVT0__RTPS_R52_SPLIT_1_SSW;
        trch_mbox_ev[1] = LSIO_MBOX0_INT_EVT1__RTPS_R52_SPLIT_1_SSW;
        trch_mbox[0] = LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SPLIT_1_SSW;
        trch_mbox[1] = LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_1_SSW__TRCH_SSW;

        hpps_mbox_ev[0] = HPPS_MBOX1_INT_EVT0__RTPS_R52_SPLIT_1_SSW;
        hpps_mbox_ev[1] = HPPS_MBOX1_INT_EVT1__RTPS_R52_SPLIT_1_SSW;
        hpps_mbox[0] = HPPS_MBOX1_CHAN__HPPS_SMP_APP__RTPS_R52_SPLIT_1_SSW;
        hpps_mbox[1] = HPPS_MBOX1_CHAN__RTPS_R52_SPLIT_1_SSW__HPPS_SMP_APP;
        break;
    default:
        panic("invalid RTPS R52 core ID");
    }
#else /* !CONFIG_SPLIT */
    self_owner = OWNER(SW_SUBSYS_RTPS_R52_LOCKSTEP, self_sw);
    trch_mbox_ev[0] = LSIO_MBOX0_INT_EVT0__RTPS_R52_LOCKSTEP_SSW;
    trch_mbox_ev[1] = LSIO_MBOX0_INT_EVT1__RTPS_R52_LOCKSTEP_SSW;
    trch_mbox[0] = LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW;
    trch_mbox[1] = LSIO_MBOX0_CHAN__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW;

    hpps_mbox_ev[0] = HPPS_MBOX1_INT_EVT0__RTPS_R52_LOCKSTEP_SSW;
    hpps_mbox_ev[1] = HPPS_MBOX1_INT_EVT1__RTPS_R52_LOCKSTEP_SSW;
    hpps_mbox[0] = HPPS_MBOX1_CHAN__HPPS_SMP_APP__RTPS_R52_LOCKSTEP_SSW;
    hpps_mbox[1] = HPPS_MBOX1_CHAN__RTPS_R52_LOCKSTEP_SSW__HPPS_SMP_APP;
#endif /* !CONFIG_SPLIT */
#endif /* !CONFIG_SMP */

#ifdef CONFIG_MBOX_DEV_LSIO
    mldev_trch.base = MBOX_LSIO__BASE;
    mldev_trch.rcv_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + trch_mbox_ev[0],
            GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_trch.rcv_int_idx = trch_mbox_ev[0];
    mldev_trch.ack_irq = gic_request(RTPS_IRQ__TR_MBOX_0 + trch_mbox_ev[1],
            GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_trch.ack_int_idx = trch_mbox_ev[1];
#endif /* CONFIG_MBOX_DEV_LSIO */

#if CONFIG_RTPS_TRCH_MAILBOX
    trch_link = mbox_link_connect("TRCH_MBOX_LINK", &mldev_trch,
        trch_mbox[0], trch_mbox[1], /* server */ 0, /* client */ self_owner);
    if (!trch_link)
        panic("RTPS->TRCH mailbox");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */

#if CONFIG_MBOX_DEV_HPPS
    mldev_hpps.base = MBOX_HPPS_RTPS__BASE;
    mldev_hpps.rcv_irq = gic_request(RTPS_IRQ__HR_MBOX_0 + hpps_mbox_ev[0],
            GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_hpps.rcv_int_idx = hpps_mbox_ev[0];
    mldev_hpps.ack_irq = gic_request(RTPS_IRQ__HR_MBOX_0 + hpps_mbox_ev[1],
            GIC_IRQ_TYPE_SPI, GIC_IRQ_CFG_LEVEL);
    mldev_hpps.ack_int_idx = hpps_mbox_ev[1];
    mbox_link_dev_add(MBOX_DEV_HPPS, &mldev_hpps);
#endif /* CONFIG_MBOX_DEV_HPPS */

#if CONFIG_HPPS_RTPS_MAILBOX
    hpps_smp_app_link = mbox_link_connect("HPPS_SMP_APP_MBOX_LINK",
        &mldev_hpps, hpps_mbox[0], hpps_mbox[1],
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_APP));
    if (!hpps_smp_app_link)
        panic("HPPS link");
    // Never release the link, because we listen on it in main loop
#endif // CONFIG_HPPS_RTPS_MAILBOX
}

#if CONFIG_RTPS_TRCH_MAILBOX
struct link *links_get_trch_mbox_link()
{
    ASSERT(trch_link);
    return trch_link;
}
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
