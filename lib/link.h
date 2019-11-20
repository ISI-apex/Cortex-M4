#ifndef LINK_H
#define LINK_H

#include <unistd.h>

#include "object.h"

enum link_rpc_status {
    LINK_RPC_UNKNOWN = 0,
    LINK_RPC_OK,
    LINK_RPC_ERROR,
};

typedef void (link_resp_cb_t)(void *opaque);

/**
 * The link struct is effectively an API, which can be populated by other
 * link-like interfaces.
 */
struct link {
    struct object obj;
    void *priv;
    const char *name;
    int (*disconnect)(struct link *link);

    /* Send without expecting a reply. Returns 0 on timeout, or positive value
     * for number of bytes sent. */
    ssize_t (*send)(struct link *link, int timeout_ms, void *buf, size_t sz);
    /* Check for data availability and fetch the data if available. Returns
     * negative value on error, 0 if no data, or number of bytes received. */
    ssize_t (*recv)(struct link *link, void *buf, size_t sz);

    /* Blocking RPC call: returns negative value on send failure, 0 on
     * timeout, positive value on bytes received on success. */
    ssize_t (*request)(struct link *link,
                   int wtimeout_ms, void *wbuf, size_t wsz,
                   int rtimeout_ms, void *rbuf, size_t rsz);
    /* Non-blocking RPC call: returns negative value on send failure (including
     * timeout), or positive value with number of bytes sent. Callback is
     * optional, caller can poll on status. */
    ssize_t (*request_async)(struct link *link,
            int wtimeout_ms, void *wbuf, size_t wsz, void *rbuf, size_t *rsz,
            enum link_rpc_status *status, link_resp_cb_t *cb, void *cb_arg);
};

#endif // LINK_H
