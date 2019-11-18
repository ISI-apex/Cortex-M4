#include <stdbool.h>

#include "command.h"
#include "link.h"
#include "mailbox.h"
#include "mailbox-link.h"
#include "object.h"
#include "panic.h"
#include "console.h"
#include "sleep.h"


#define MAX_LINKS 8
// pretty coarse, limited by systick
#define MIN_SLEEP_MS 500

struct cmd_ctx {
    bool tx_acked;
    uint32_t *reply;
    size_t reply_sz;
    size_t reply_sz_read;
};

struct mbox_link {
    struct object obj;
    unsigned idx_to;
    unsigned idx_from;
    struct mbox *mbox_from;
    struct mbox *mbox_to;
    volatile struct cmd_ctx cmd_ctx;
};

static struct mbox_link_dev *devs[MBOX_DEV_COUNT] = {0};
static struct link links[MAX_LINKS] = {0};
static struct mbox_link mlinks[MAX_LINKS] = {0};

int mbox_link_dev_add(mbox_dev_id id, struct mbox_link_dev *dev)
{
    ASSERT(id < MBOX_DEV_COUNT);
    if (devs[id]) {
        printf("ERROR: mbox_link_dev_add: already added id=%u\r\n", id);
        return -1;
    }
    devs[id] = dev;
    return 0;
}

void mbox_link_dev_remove(mbox_dev_id id)
{
    ASSERT(id < MBOX_DEV_COUNT);
    devs[id] = NULL;
}

struct mbox_link_dev *mbox_link_dev_get(mbox_dev_id id)
{
    ASSERT(id < MBOX_DEV_COUNT);
    return devs[id];
}

static void handle_ack(void *arg)
{
    struct link *link = arg;
    struct mbox_link *mlink = link->priv;
    printf("%s: handle_ack\r\n", link->name);
    mlink->cmd_ctx.tx_acked = true;
    mbox_event_clear_ack(mlink->mbox_to);
}

static void handle_cmd(void *arg)
{
    struct link *link = arg;
    struct mbox_link *mlink = link->priv;
    struct cmd cmd; // can't use initializer, because GCC inserts a memset for initing .msg
    cmd.link = link;
    ASSERT(sizeof(cmd.msg) == HPSC_MBOX_DATA_SIZE); // o/w zero-fill rest of msg

    printf("%s: handle_cmd\r\n", link->name);
    // read never fails if sizeof(cmd.msg) > 0
    cmd.len = mbox_read(mlink->mbox_from, cmd.msg, sizeof(cmd.msg));
    mbox_event_clear_rcv(mlink->mbox_from);
    mbox_event_set_ack(mlink->mbox_from);
    if (cmd_enqueue(&cmd))
        panic("handle_cmd: failed to enqueue command");
}

static void handle_reply(void *arg)
{
    struct link *link = arg;
    struct mbox_link *mlink = link->priv;
    printf("%s: handle_reply\r\n", link->name);
    mlink->cmd_ctx.reply_sz_read = mbox_read(mlink->mbox_from,
                                             mlink->cmd_ctx.reply,
                                             mlink->cmd_ctx.reply_sz);
    mbox_event_clear_rcv(mlink->mbox_from);
    mbox_event_set_ack(mlink->mbox_from);
}

static int mbox_link_disconnect(struct link *link) {
    struct mbox_link *mlink = link->priv;
    int rc;
    printf("%s: disconnect\r\n", link->name);
    // in case of failure, keep going and fwd code
    rc = mbox_release(mlink->mbox_from);
    rc |= mbox_release(mlink->mbox_to);
    OBJECT_FREE(mlink);
    OBJECT_FREE(link);
    return rc;
}

/* TODO: Would be good to replace 'sleep(x ms)' approach with
   'wait-for-interrupt with timeout' approach, to take advantage of the
   interrupt notification. This can be implemented using similar pattern to the
   one used in msleep, but with SEV+WFE instead of WFI, because with WFI there
   will be a race (not fatal, but would cost the timeout interval, defeating
   the whole point). */
static void msleep_and_dec(int *ms_rem)
{
    msleep(MIN_SLEEP_MS);
    // if < 0, timeout is infinite
    if (*ms_rem > 0)
        *ms_rem -= *ms_rem >= MIN_SLEEP_MS ? MIN_SLEEP_MS : *ms_rem;
}

static int mbox_link_send(struct link *link, int timeout_ms, void *buf,
                          size_t sz)
{
    struct mbox_link *mlink = link->priv;
    int sleep_ms_rem = timeout_ms;
    int rc;
    mlink->cmd_ctx.tx_acked = false;
    rc = mbox_send(mlink->mbox_to, buf, sz);
    mbox_event_set_rcv(mlink->mbox_to);
    printf("%s: send: waiting for ACK (timeout %u ms)...\r\n",
           link->name, timeout_ms);
    do {
        if (mlink->cmd_ctx.tx_acked) {
            printf("%s: send: ACK received\r\n", link->name);
            mbox_event_clear_ack(mlink->mbox_to);
            return rc;
        }
        if (!sleep_ms_rem)
            break; // timeout
        msleep_and_dec(&sleep_ms_rem);
    } while (1);
    return 0;
}

static int mbox_link_poll(struct link *link, int timeout_ms)
{
    struct mbox_link *mlink = link->priv;
    int sleep_ms_rem = timeout_ms;
    int rc;
    printf("%s: poll: waiting for reply (timeout %u ms)...\r\n",
           link->name, timeout_ms);
    do {
        rc = mlink->cmd_ctx.reply_sz_read;
        if (rc) {
            printf("%s: poll: reply received\r\n", link->name);
            break; // got data
        }
        if (!sleep_ms_rem)
            break; // timeout
        msleep_and_dec(&sleep_ms_rem);
    } while (1);
    return rc;
}

static int mbox_link_request(struct link *link,
                             int wtimeout_ms, void *wbuf, size_t wsz,
                             int rtimeout_ms, void *rbuf, size_t rsz)
{
    struct mbox_link *mlink = link->priv;
    int rc;

    printf("%s: request\r\n", link->name);
    mlink->cmd_ctx.reply_sz_read = 0;
    mlink->cmd_ctx.reply = rbuf;
    mlink->cmd_ctx.reply_sz = rsz / sizeof(uint32_t);

    rc = mbox_link_send(link, wtimeout_ms, wbuf, wsz);
    if (!rc) {
        printf("%s: request: send timed out\r\n", link->name);
        return -1;
    }
    rc = mbox_link_poll(link, rtimeout_ms);
    if (!rc)
        printf("%s: request: recv failed\r\n", link->name);
    return rc;
}

struct link *mbox_link_connect(const char *name, struct mbox_link_dev *ldev,
                               unsigned idx_from, unsigned idx_to,
                               unsigned server, unsigned client)
{
    struct mbox_link *mlink;
    struct link *link;
    printf("%s: connect\r\n", name);
    link = OBJECT_ALLOC(links);
    if (!link)
        return NULL;

    mlink = OBJECT_ALLOC(mlinks);
    if (!mlink) {
        printf("ERROR: mbox_link_connect: failed to allocate mlink state\r\n");
        goto free_link;
    }

    mlink->idx_from = idx_from;
    mlink->idx_to = idx_to;

    union mbox_cb rcv_cb = { .rcv_cb = server ? handle_cmd : handle_reply };
    mlink->mbox_from = mbox_claim(ldev->base, idx_from,
                                  ldev->rcv_irq, ldev->rcv_int_idx,
                                  server, client, server, MBOX_INCOMING,
                                  rcv_cb, link);
    if (!mlink->mbox_from) {
        printf("ERROR: mbox_link_connect: failed to claim mbox_from\r\n");
        goto free_links;
    }

    union mbox_cb ack_cb = { .ack_cb = handle_ack };
    mlink->mbox_to = mbox_claim(ldev->base, idx_to,
                                ldev->ack_irq, ldev->ack_int_idx,
                                server, server, client, MBOX_OUTGOING,
                                ack_cb, link);
    if (!mlink->mbox_to) {
        printf("ERROR: mbox_link_connect: failed to claim mbox_to\r\n");
        goto free_from;
    }

    mlink->cmd_ctx.tx_acked = false;
    mlink->cmd_ctx.reply = NULL;

    link->priv = mlink;
    link->name = name;
    link->disconnect = mbox_link_disconnect;
    link->send = mbox_link_send;
    link->request = mbox_link_request;
    link->recv = NULL;
    return link;

free_from:
    mbox_release(mlink->mbox_from);
free_links:
    OBJECT_FREE(mlink);
free_link:
    OBJECT_FREE(link);
    return NULL;
}
