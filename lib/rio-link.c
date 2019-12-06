#define DEBUG 0

#include <stdint.h>
#include <stdbool.h>

#include "console.h"
#include "panic.h"
#include "oop.h"
#include "object.h"
#include "rio-ep.h"
#include "command.h"
#include "sleep.h"
#include "mem.h"
#include "link.h"

#define RESP_SIZE CMD_MSG_SZ
#define MIN_SLEEP_MS 500

struct cmd_ctx {
    uint8_t *resp;
    size_t *resp_len;
    link_resp_cb_t *cb;
    void *arg;
    volatile enum link_rpc_status *status;
};

struct rio_link {
    struct link link;
    struct rio_ep *ep;
    unsigned mbox_local;
    unsigned letter_local;
    rio_devid_t dest;
    unsigned mbox_remote;
    unsigned letter_remote;
    unsigned seg_size;
    struct cmd_ctx cmd_ctx;
};

#define MAX_LINKS 2
static struct rio_link links[MAX_LINKS] = { 0 };

static void handle_request(void *arg)
{
    struct rio_link *rlink = arg;
    int rc;
    rio_devid_t src = 0;
    uint64_t rcv_time = 0;
    struct cmd cmd;

    ASSERT(rlink);

    cmd.link = &rlink->link;
    unsigned msg_sz = sizeof(cmd.msg);
    rc = rio_ep_msg_recv(rlink->ep, rlink->mbox_local, rlink->letter_local,
                         &src, &rcv_time, cmd.msg, &msg_sz);
    if (rc) goto exit;
    if (src != rlink->dest) {
        printf("ERROR: RIO LINK: received req from unexpected source: "
               "0x%x != 0x%x\r\n", src, rlink->dest);
        rc = 1;
        goto exit;
    }
    cmd.len = msg_sz;
    if (cmd_enqueue(&cmd)) {
        printf("ERROR: failed to enqueue command");
        rc = 2;
        goto exit;
    }
exit: /* for now, make failure fatal to catch issues */
    if (rc)
        panic("RIO LINK: failure when handling incoming request");
}

static void handle_response(void *arg)
{
    struct rio_link *rlink = arg;
    struct cmd_ctx *ctx;
    int rc;
    rio_devid_t src = 0;
    uint64_t rcv_time = 0;

    ASSERT(rlink);
    ctx = &rlink->cmd_ctx;
    ASSERT(ctx->resp && ctx->resp_len);
    rc = rio_ep_msg_recv(rlink->ep, rlink->mbox_local, rlink->letter_local,
                         &src, &rcv_time, ctx->resp, ctx->resp_len);
    if (rc) goto exit;
    if (src != rlink->dest) {
        printf("ERROR: RIO LINK: received resp from unexpected source: "
               "0x%x != 0x%x\r\n", src, rlink->dest);
        rc = 1;
        goto exit;
    }
exit:
    *ctx->status = rc ? LINK_RPC_ERROR : LINK_RPC_OK;
    if (ctx->cb)
        ctx->cb(ctx->arg);
    bzero(ctx, sizeof(struct cmd_ctx));
}

/* TODO: factor into link.c */
static void msleep_and_dec(int *ms_rem)
{
    msleep(MIN_SLEEP_MS);
    // if < 0, timeout is infinite
    if (*ms_rem > 0)
        *ms_rem -= *ms_rem >= MIN_SLEEP_MS ? MIN_SLEEP_MS : *ms_rem;
}

static int rio_link_send(struct link *link, int timeout_ms, void *buf,
                         size_t sz)
{
    struct rio_link *rlink = container_of(struct rio_link, link, link);
    int rc;
    ASSERT(link);
    DPRINTF("RIO LINK %s: send sz %u\r\n", link->name, sz);
    rc = rio_ep_msg_send(rlink->ep, rlink->dest,
                         rlink->mbox_remote, rlink->letter_remote,
                         rlink->seg_size, (uint8_t *)buf, sz);
    return rc ? 0 : sz; /* return code convention differs */
}

static int request_async(struct rio_link *rlink,
                         int wtimeout_ms, void *wbuf, size_t wsz,
                         void *rbuf, size_t *rsz,
                         volatile enum link_rpc_status *status,
                         link_resp_cb_t *cb, void *cb_arg)
{
    struct cmd_ctx *ctx = &rlink->cmd_ctx;
    ASSERT(!ctx->resp && !ctx->resp_len && !ctx->cb);
    ctx->resp = rbuf;
    ctx->resp_len = rsz;
    ctx->cb = cb;
    ctx->arg = cb_arg;
    ctx->status = status;
    return rio_link_send(&rlink->link, wtimeout_ms, wbuf, wsz);
}

static int rpc_wait(const char *link_name,
                    volatile enum link_rpc_status *status, int timeout_ms)
{
    int sleep_ms_rem = timeout_ms;
    printf("RIO LINK %s: waiting for resp (timeout %u ms)...\r\n",
           link_name, timeout_ms);
    while (*status == LINK_RPC_UNKNOWN && sleep_ms_rem) {
        msleep_and_dec(&sleep_ms_rem);
    }
    if (*status == LINK_RPC_UNKNOWN) {
        printf("RIO LINK %s: response wait timed out\r\n", link_name);
        return 1;
    }
    printf("RIO LINK %s: resp received\r\n", link_name);
    return 0;
}

static int rio_link_request(struct link *link,
                             int wtimeout_ms, void *wbuf, size_t wsz,
                             int rtimeout_ms, void *rbuf, size_t rsz)
{
    struct rio_link *rlink = container_of(struct rio_link, link, link);
    volatile enum link_rpc_status status = false;
    ssize_t sz;

    DPRINTF("RIO LINK %s: sync request\r\n", link->name);
    sz = request_async(rlink, wtimeout_ms, wbuf, wsz, rbuf, &rsz,
                       &status, NULL, NULL);
    if (sz <= 0)
        return sz;
    return rpc_wait(link->name, &status, rtimeout_ms) ? 0 : rsz;
}

static int rio_link_request_async(struct link *link,
        int wtimeout_ms, void *wbuf, size_t wsz, void *rbuf, size_t *rsz,
        enum link_rpc_status *status, link_resp_cb_t *cb, void *cb_arg)
{
    struct rio_link *rlink = container_of(struct rio_link, link, link);
    DPRINTF("RIO LINK %s: async request\r\n", link->name);
    return request_async(rlink, wtimeout_ms, wbuf, wsz, rbuf, rsz,
                         status, cb, cb_arg);
}

int rio_link_disconnect(struct link *link)
{
    struct rio_link *rlink = container_of(struct rio_link, link, link);
    ASSERT(link);
    DPRINTF("RIO LINK %s: disconnect\r\n", link->name);
    rio_ep_unsub_msg_rx(rlink->ep);
    OBJECT_FREE(rlink);
    return 0;
}

struct link *rio_link_connect(const char *name, bool is_server,
                              struct rio_ep *ep,
                              unsigned mbox_local, unsigned letter_local,
                              rio_devid_t dest,
                              unsigned mbox_remote, unsigned letter_remote,
                              unsigned seg_size)
{
    ASSERT(ep);
    DPRINTF("RIO LINK %s: connect: %s %s->devid 0x%x\r\n", name,
            is_server ? "server" : "client", rio_ep_name(ep), dest);

    struct rio_link *rlink = OBJECT_ALLOC(links);
    if (!rlink) {
        printf("RIO LINK %s: alloc failed\r\n", name);
        return NULL;
    }
    rlink->ep = ep;
    rlink->mbox_local = mbox_local;
    rlink->letter_local = letter_local;
    rlink->dest = dest;
    rlink->mbox_remote = mbox_remote;
    rlink->letter_remote = letter_remote;
    rlink->seg_size = seg_size;

    struct link *link = &rlink->link;
    link->name = name;
    link->disconnect = rio_link_disconnect;
    link->send = rio_link_send;
    link->request = rio_link_request;
    link->request_async = rio_link_request_async;
    link->recv = NULL;

    if (is_server) {
        rio_ep_sub_msg_rx(ep, handle_request, rlink);
    } else { /* client */
        rio_ep_sub_msg_rx(ep, handle_response, rlink);
    }
    return link;
}
