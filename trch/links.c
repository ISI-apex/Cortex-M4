#include "command.h"
#include "hwinfo.h"
#include "llist.h"
#include "mem-map.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "nvic.h"
#include "panic.h"
#include "shmem-link.h"
#include "subsys.h"
#include "syscfg.h"

#include "links.h"

/* inferred */
#define CONFIG_MBOX_DEV_HPPS \
   (CONFIG_HPPS_TRCH_MAILBOX_SSW || \
    CONFIG_HPPS_TRCH_MAILBOX || \
    CONFIG_HPPS_TRCH_MAILBOX_ATF)

#define CONFIG_MBOX_DEV_LSIO \
   (CONFIG_RTPS_TRCH_MAILBOX || \
    CONFIG_RTPS_TRCH_MAILBOX_PSCI)

static struct llist shm_links = { 0 };

#if CONFIG_MBOX_DEV_HPPS
static struct mbox_link_dev mldev_hpps;
#endif
#if CONFIG_MBOX_DEV_LSIO
static struct mbox_link_dev mldev_lsio;
#endif
#if CONFIG_HPPS_TRCH_MAILBOX_SSW
static struct link *hpps_link_ssw;
#endif
#if CONFIG_HPPS_TRCH_MAILBOX
static struct link *hpps_link;
#endif
#if CONFIG_HPPS_TRCH_SHMEM
static struct link *hpps_link_shmem;
#endif
#if CONFIG_HPPS_TRCH_SHMEM_SSW
static struct link *hpps_link_shmem_ssw ;
#endif

/* As many entries as maximum concurrent RTPS R52 (logical) subsystems */
#if CONFIG_RTPS_TRCH_MAILBOX
static struct link *rtps_mb_links[RTPS_R52_NUM_CORES] = {0};
#endif
#if CONFIG_RTPS_TRCH_SHMEM
static struct link *rtps_shm_links[RTPS_R52_NUM_CORES] = {0};
#endif

/* panics on failure */
void links_init(enum rtps_mode rtps_mode)
{
    int rc = 1;

    llist_init(&shm_links);

#if CONFIG_MBOX_DEV_HPPS
    mldev_hpps.base = MBOX_HPPS_TRCH__BASE;
    mldev_hpps.rcv_irq =
        nvic_request(TRCH_IRQ__HT_MBOX_0 + HPPS_MBOX0_INT_EVT0__TRCH_SSW);
    mldev_hpps.rcv_int_idx = HPPS_MBOX0_INT_EVT0__TRCH_SSW;
    mldev_hpps.ack_irq =
        nvic_request(TRCH_IRQ__HT_MBOX_0 + HPPS_MBOX0_INT_EVT1__TRCH_SSW);
    mldev_hpps.ack_int_idx = HPPS_MBOX0_INT_EVT1__TRCH_SSW;
    rc = mbox_link_dev_add(MBOX_DEV_HPPS, &mldev_hpps);
    if (rc)
       panic("HPPS mailbox device");
#endif // CONFIG_MBOX_DEV_HPPS

#if CONFIG_MBOX_DEV_LSIO
    mldev_lsio.base = MBOX_LSIO__BASE;
    mldev_lsio.rcv_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + LSIO_MBOX0_INT_EVT0__TRCH_SSW);
    mldev_lsio.rcv_int_idx = LSIO_MBOX0_INT_EVT0__TRCH_SSW;
    mldev_lsio.ack_irq =
        nvic_request(TRCH_IRQ__TR_MBOX_0 + LSIO_MBOX0_INT_EVT1__TRCH_SSW);
    mldev_lsio.ack_int_idx = LSIO_MBOX0_INT_EVT1__TRCH_SSW;
    rc = mbox_link_dev_add(MBOX_DEV_LSIO, &mldev_lsio);
    if (rc)
       panic("LSIO mailbox device");
#endif // CONFIG_MBOX_DEV_LSIO

    unsigned self_owner = OWNER(SW_SUBSYS_TRCH, SW_COMP_SSW);
    (void)self_owner; /* silence unused warning when ifdef'ed out */

#if CONFIG_HPPS_TRCH_MAILBOX_SSW
    hpps_link_ssw = mbox_link_connect("HPPS_MBOX_SSW_LINK", &mldev_hpps,
        HPPS_MBOX0_CHAN__HPPS_SMP_SSW__TRCH_SSW,
        HPPS_MBOX0_CHAN__TRCH_SSW__HPPS_SMP_SSW,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_SSW));
    if (!hpps_link_ssw)
        panic("HPPS_MBOX_SSW_LINK");
#endif /* CONFIG_HPPS_TRCH_MAILBOX_SSW */

#if CONFIG_HPPS_TRCH_MAILBOX
    hpps_link = mbox_link_connect("HPPS_MBOX_LINK", &mldev_hpps,
        HPPS_MBOX0_CHAN__HPPS_SMP_APP__TRCH_SSW,
        HPPS_MBOX0_CHAN__TRCH_SSW__HPPS_SMP_APP,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_APP));
    if (!hpps_link)
        panic("HPPS_MBOX_LINK");
#endif /* CONFIG_HPPS_TRCH_MAILBOX */

#if CONFIG_HPPS_TRCH_MAILBOX_ATF
    struct link *hpps_atf_link = mbox_link_connect("HPPS_MBOX_ATF_LINK",
        &mldev_hpps,
        HPPS_MBOX0_CHAN__HPPS_SMP_ATF__TRCH_SSW,
        HPPS_MBOX0_CHAN__TRCH_SSW__HPPS_SMP_ATF,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_HPPS_SMP, SW_COMP_ATF));
    if (!hpps_atf_link)
        panic("HPPS_MBOX_ATF_LINK");
#endif /* CONFIG_HPPS_TRCH_MAILBOX_ATF */

    switch (rtps_mode) {
    case SYSCFG__RTPS_MODE__LOCKSTEP:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_LOCKSTEP_MBOX_LINK",
            &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW,
            LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_LOCKSTEP, SW_COMP_SSW));
        if (!rtps_mb_links[0])
            panic("RTPS_R52_LOCKSTEP_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect(
            "RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK",
            RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_LOCKSTEP_SSW,
            RTPS_DDR_ADDR__SHM__RTPS_R52_LOCKSTEP_SSW__TRCH_SSW);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK");
        if (llist_insert(&shm_links, rtps_shm_links[0]))
            panic("RTPS_R52_LOCKSTEP_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    case SYSCFG__RTPS_MODE__SMP:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_SMP_MBOX_LINK",
            &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SMP_SSW__TRCH_SSW,
            LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SMP_SSW,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SMP, SW_COMP_SSW));
        if (!rtps_mb_links[0])
            panic("RTPS_R52_SMP_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect("RTPS_R52_SMP_SSW_SHMEM_LINK",
            RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SMP_SSW,
            RTPS_DDR_ADDR__SHM__RTPS_R52_SMP_SSW__TRCH_SSW);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_SMP_SSW_SHMEM_LINK");
        if (llist_insert(&shm_links, rtps_shm_links[0]))
            panic("RTPS_R52_SMP_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    case SYSCFG__RTPS_MODE__SPLIT:
#if CONFIG_RTPS_TRCH_MAILBOX
        rtps_mb_links[0] = mbox_link_connect("RTPS_R52_0_MBOX_LINK", &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_0_SSW__TRCH_SSW,
            LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SPLIT_0_SSW,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SPLIT_1, SW_COMP_SSW));
        if (!rtps_mb_links[0])
            panic("RTPS_R52_SPLIT_0_MBOX_LINK");
        rtps_mb_links[1] = mbox_link_connect("RTPS_R52_1_MBOX_LINK",
            &mldev_lsio,
            LSIO_MBOX0_CHAN__RTPS_R52_SPLIT_1_SSW__TRCH_SSW,
            LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_R52_SPLIT_1_SSW,
            /* server */ self_owner,
            /* client */ OWNER(SW_SUBSYS_RTPS_R52_SPLIT_1, SW_COMP_SSW));
        if (!rtps_mb_links[1])
            panic("RTPS_R52_SPLIT_1_MBOX_LINK");
#endif /* CONFIG_RTPS_TRCH_MAILBOX */
#if CONFIG_RTPS_TRCH_SHMEM
        rtps_shm_links[0] = shmem_link_connect(
            "RTPS_R52_SPLIT_0_SSW_SHMEM_LINK",
            RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SPLIT_0_SSW,
            RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_0_SSW__TRCH_SSW);
        if (!rtps_shm_links[0])
            panic("RTPS_R52_SPLIT_0_SSW_SHMEM_LINK");
        if (llist_insert(&shm_links, rtps_shm_links[0]))
            panic("RTPS_R52_SPLIT_0_SSW_SHMEM_LINK: llist_insert");
        rtps_shm_links[1] = shmem_link_connect(
            "RTPS_R52_SPLIT_1_SSW_SHMEM_LINK",
            RTPS_DDR_ADDR__SHM__TRCH_SSW__RTPS_R52_SPLIT_1_SSW,
            RTPS_DDR_ADDR__SHM__RTPS_R52_SPLIT_1_SSW__TRCH_SSW);
        if (!rtps_shm_links[1])
            panic("RTPS_R52_SPLIT_1_SSW_SHMEM_LINK");
        if (llist_insert(&shm_links, rtps_shm_links[1]))
            panic("RTPS_R52_SPLIT_1_SSW_SHMEM_LINK: llist_insert");
#endif /* CONFIG_RTPS_TRCH_SHMEM */
        break;
    default:
        panic("invalid RTPS R52 mode in syscfg");
    }

#if CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI
    struct link *rtps_a53_psci_link = mbox_link_connect(
        "RTPS_A53_PSCI_MBOX_LINK", &mldev_lsio,
        LSIO_MBOX0_CHAN__RTPS_A53_ATF__TRCH_SSW,
        LSIO_MBOX0_CHAN__TRCH_SSW__RTPS_A53_ATF,
        /* server */ self_owner,
        /* client */ OWNER(SW_SUBSYS_RTPS_A53, SW_COMP_ATF));
    if (!rtps_a53_psci_link)
        panic("RTPS_A53_PSCI_MBOX_LINK");
#endif /* CONFIG_RTPS_A53_TRCH_MAILBOX_PSCI */

#if CONFIG_HPPS_TRCH_SHMEM
    hpps_link_shmem = shmem_link_connect("HPPS_SHMEM_LINK",
        HPPS_DDR_ADDR__SHM__HPPS_SMP_APP__TRCH_SSW,
        HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_APP);
    if (!hpps_link_shmem)
        panic("HPPS_SHMEM_LINK");
    if (llist_insert(&shm_links, hpps_link_shmem))
        panic("HPPS_SHMEM_LINK: llist_insert");
#endif /* CONFIG_HPPS_TRCH_SHMEM */

#if CONFIG_HPPS_TRCH_SHMEM_SSW
    hpps_link_shmem_ssw = shmem_link_connect("HPPS_SHMEM_SSW_LINK",
        HPPS_DDR_ADDR__SHM__HPPS_SMP_SSW__TRCH_SSW,
        HPPS_DDR_ADDR__SHM__TRCH_SSW__HPPS_SMP_SSW);
    if (!hpps_link_shmem_ssw)
        panic("HPPS_SHMEM_SSW_LINK");
    if (llist_insert(&shm_links, hpps_link_shmem_ssw))
        panic("HPPS_SHMEM_SSW_LINK: llist_insert");
#endif /* CONFIG_HPPS_TRCH_SHMEM_SSW */
}

int links_poll()
{
   /* TODO: move within shmem-link, use event loop */
   static struct cmd cmd; /* lifetime=func, but don't alloc on stack */
   struct link *link_curr;
   llist_iter_init(&shm_links);
   do {
      link_curr = (struct link *) llist_iter_next(&shm_links);
      if (!link_curr)
          break;
      cmd.len = link_curr->recv(link_curr, cmd.msg, sizeof(cmd.msg));
      if (cmd.len) {
          printf("%s: recv: got message\r\n", link_curr->name);
          cmd.link = link_curr;
          if (cmd_enqueue(&cmd)) {
              return 1;
          }
      }
   } while (1);
   return 0;
}
