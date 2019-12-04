#include <stdint.h>

#include "link.h"
#include "object.h"
#include "console.h"
#include "shmem.h"
#include "sleep.h"

struct shmem_link {
    struct object obj;
    struct shmem *shmem_out;
    struct shmem *shmem_in;
};

#define MAX_LINKS 8
// pretty coarse, limited by systick
#define MIN_SLEEP_MS 500

static struct link links[MAX_LINKS] = {0};
static struct shmem_link slinks[MAX_LINKS] = {0};

static int shmem_link_disconnect(struct link *link)
{
    struct shmem_link *slink = link->priv;
    printf("%s: disconnect\r\n", link->name);
    shmem_close(slink->shmem_out);
    shmem_close(slink->shmem_in);
    OBJECT_FREE(slink);
    OBJECT_FREE(link);
    return 0;
}

static void msleep_and_dec(int *ms_rem)
{
    msleep(MIN_SLEEP_MS);
    // if < 0, timeout is infinite
    if (*ms_rem > 0)
        *ms_rem -= *ms_rem >= MIN_SLEEP_MS ? MIN_SLEEP_MS : *ms_rem;
}

static int shmem_link_send(struct link *link, int timeout_ms, void *buf,
                           size_t sz)
{
    struct shmem_link *slink = link->priv;
    int sleep_ms_rem = timeout_ms;
    int rc = shmem_send(slink->shmem_out, buf, sz);
    shmem_set_new(slink->shmem_out, true);
    printf("%s: send: waiting for ACK...\r\n", link->name);
    do {
        if (shmem_is_ack(slink->shmem_out)) {
            printf("%s: send: ACK received\r\n", link->name);
            shmem_set_ack(slink->shmem_out, false);
            return rc;
        }
        if (!sleep_ms_rem)
            break; // timeout
        msleep_and_dec(&sleep_ms_rem);
    } while (1);
    return 0;
}

static int shmem_link_recv(struct link *link, void *buf, size_t sz)
{
    struct shmem_link *slink = link->priv;
    int rc;
    if (shmem_is_new(slink->shmem_in)) {
        rc = shmem_recv(slink->shmem_in, buf, sz);
        shmem_set_new(slink->shmem_in, false);
        shmem_set_ack(slink->shmem_in, true);
        return rc;
    }
    return 0;
}

static int shmem_link_poll(struct link *link, int timeout_ms, void *buf,
                           size_t sz)
{
    int sleep_ms_rem = timeout_ms;
    int rc;
    printf("%s: poll: waiting for reply...\r\n", link->name);
    do {
        rc = shmem_link_recv(link, buf, sz);
        if (rc > 0) {
            printf("%s: poll: reply received\r\n", link->name);
            break; // got data
        }
        if (!sleep_ms_rem)
            break; // timeout
        msleep_and_dec(&sleep_ms_rem);
    } while (1);
    return rc;
}

static int shmem_link_request(struct link *link,
                              int wtimeout_ms, void *wbuf, size_t wsz,
                              int rtimeout_ms, void *rbuf, size_t rsz)
{
    int rc;
    printf("%s: request\r\n", link->name);
    rc = shmem_link_send(link, wtimeout_ms, wbuf, wsz);
    if (!rc) {
        printf("%s: request: send timed out\r\n", link->name);
        return -1;
    }
    rc = shmem_link_poll(link, rtimeout_ms, rbuf, rsz);
    if (!rc)
        printf("%s: request: recv timed out\r\n", link->name);
    return rc;
}

struct link *shmem_link_connect(const char *name, uintptr_t addr_out,
                                uintptr_t addr_in)
{
    struct shmem_link *slink;
    struct link *link;
    printf("%s: connect\r\n", name);
    printf("\taddr_out = 0x%x\r\n", (unsigned long) addr_out);
    printf("\taddr_in  = 0x%x\r\n", (unsigned long) addr_in);
    link = OBJECT_ALLOC(links);
    if (!link)
        return NULL;

    slink = OBJECT_ALLOC(slinks);
    if (!slink) {
        goto free_link;
    }
    slink->shmem_out = shmem_open(addr_out);
    if (!slink->shmem_out)
        goto free_links;
    slink->shmem_in = shmem_open(addr_in);
    if (!slink->shmem_in)
        goto free_out;

    link->priv = slink;
    link->name = name;
    link->disconnect = shmem_link_disconnect;
    link->send = shmem_link_send;
    link->request = shmem_link_request;
    link->request_async = NULL;
    link->recv = shmem_link_recv;
    return link;

free_out:
    shmem_close(slink->shmem_out);
free_links:
    OBJECT_FREE(slink);
free_link:
    OBJECT_FREE(link);
    return NULL;
}
