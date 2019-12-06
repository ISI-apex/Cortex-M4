#define DEBUG 1

#include <stdbool.h>

#include "printf.h"
#include "panic.h"
#include "mem.h"
#include "hwinfo.h"
#include "mem-map.h"
#include "nvic.h"
#include "dma.h"
#include "mmus.h"
#include "bit.h"
#include "oop.h"
#include "rio-link.h"
#include "event.h"
#include "command.h"
#include "swtimer.h"
#include "rio-ep.h"
#include "rio-switch.h"
#include "test.h"

/* Configurable parameters chosen by this test */
#define MSG_SEG_SIZE 16
#define TRANSPORT_TYPE RIO_TRANSPORT_DEV8
#define ADDR_WIDTH RIO_ADDR_WIDTH_34_BIT
#define CFG_SPACE_SIZE 0x400000 /* for this test, care about only part of 16MB */
#define TIMEOUT_MS 4000

/* Choose which mboxes are used for testing */
#define MBOX_CLIENT      0
#define LETTER_CLIENT    0
#define MBOX_SERVER      0
#define LETTER_SERVER    0

/* Normally these are assigned by discovery routine in the driver */
/* TODO: these are set by the rio service, so need to pass them as params */
#define RIO_DEVID_EP0 0x0
#define RIO_DEVID_EP1 0x1

/* External endpoint to be recheable via the given port */
#define RIO_DEVID_EP_EXT 0x2
#define RIO_EP_EXT_SWITCH_PORT 2

#define CFG_SPACE_BASE { 0x0, 0x0 }
static const rio_addr_t cfg_space_base = CFG_SPACE_BASE;

/* Read-only Device Identity CAR, not to be confused with assigned ID */
#define RIO_EP_DEV_ID   0x44332211 /* TODO: check real HW and update model */

/* Could make this a 64-bit value, but byte-order complicates thinking */
static uint8_t ref_dwords[][8] = {
    { 0xA1, 0xB2, 0xC3, 0xD4, 0xA5, 0xB6, 0xC7, 0xD8 },
    { 0x1F, 0x2E, 0x3D, 0x4C, 0x5F, 0x6E, 0x7D, 0x8C },
    { 0xF9, 0xE8, 0xD7, 0xC6, 0xF5, 0xE4, 0xD3, 0xC2 },
};
static uint8_t ref_bytes[] = { 0xCA, 0xFE, 0xBF };

/* The BM memory map (mem-map.h) assigns this test code some memory and some
 * MMU window space.  We partition them into sub-regions here. */

#define TEST_BUF_ADDR           (RIO_MEM_TEST_ADDR + 0x0)
#define TEST_BUF_SIZE           RIO_MEM_TEST_SIZE

/* to test data buffer in high mem */
#define TEST_BUF_WIN_ADDR      (RIO_TEST_WIN_ADDR + 0x000000) /* 0x20000 */
/* to EP0 outgoing map regions */
#define TEST_OUT_BUF_WIN_ADDR  (RIO_TEST_WIN_ADDR + 0x020000) /* 0x20000 */
#define TEST_OUT_BUF_WIN2_ADDR (RIO_TEST_WIN_ADDR + 0x040000) /* 0x20000 */
#define TEST_OUT_CFG_WIN_ADDR  (RIO_TEST_WIN_ADDR + 0x060000) /* 0x400000 */
#define TEST_WIN_USED_SIZE                          0x460000

#if TEST_WIN_USED_SIZE < RIO_TEST_WIN_SIZE
#error Window region allocated for test is too small (RIO_TEST_WIN_SIZE)
#endif

/* This one happens to be defined in mem map, so cross-check.
   We don't use this size macro directly in favor of literal offsets, for
   legibility of the above partitioning. */
#if TEST_OUT_WIN_ADDR - TEST_BUF_WIN_ADDR > RIO_MEM_TEST_SIZE
#error Window partition for test buf smaller than RIO_MEM_TEST_SIZE
#endif

#define STR_(s) #s
#define STR(s) STR_(s)

#define LEN(a) (sizeof(a) / sizeof(a[0]))

#define INVALID_STATE_PANIC(state) do { \
    printf("RIO TEST: %s: invalid state: 0x%x\r\n", __func__, state); \
    PANIC("RIO TEST: invalid state"); \
} while (0);

#define LOG_EVENT(state, sender, ev) \
    printf("RIO TEST: %s: state 0x%x event %s from %s\r\n", __func__, \
            state, t_event_name(ev), ((sender) ? (sender)->name : "NULL"))

#define LOG_RESULT(rc, state) \
    if (rc) printf("RIO TEST: %s: FAILED: rc %d state 0x%x\r\n", __func__, \
                   rc, state); \
    else printf("RIO TEST: %s: SUCCEEDED\r\n", __func__)

/* Same set of events re-used across different state machines */
enum t_event {
    EV_START,
    EV_STOP,
    EV_DONE,
    EV_ERROR,
    EV_TIMEOUT,
    EV_TICK,
    EV_PONG,
    T_EVENT_NUM
};
static const char *t_event_names[] = {
    [EV_START]          = "EV_START",
    [EV_STOP]           = "EV_STOP",
    [EV_DONE]           = "EV_DONE",
    [EV_ERROR]          = "EV_ERROR",
    [EV_TIMEOUT]        = "EV_TIMEOUT",
    [EV_TICK]           = "EV_TICK",
    [EV_PONG]           = "EV_PONG",
};
static inline const char *t_event_name(enum t_event ev)
{
    return (ev < T_EVENT_NUM) ? t_event_names[ev] : "?";
}

static struct RioPkt in_pkt, out_pkt;
static uint8_t test_buf[RIO_MEM_TEST_SIZE]
    __attribute__((aligned (DMA_MAX_BURST_BYTES)));

static inline enum t_event finish_ev(int rc)
{
    return rc ? EV_ERROR : EV_DONE;
}

void post(struct ev_loop *el, struct ev_actor *sender,
        struct ev_actor *act, enum t_event event)
{
    ASSERT(act);
    ASSERT(act->name);
    ASSERT(!sender || sender->name);
    printf("RIO TEST: post %s %s->%s\r\n", t_event_name(event),
            sender ? sender->name : "NULL", act->name);
    ev_post(el, sender, act, (void *)event);
}

static size_t split_buffers(uint8_t **ptrs, unsigned n,
        uint8_t *addr, size_t size, const char *func)
{
    uint8_t *buf_addr = addr;
    unsigned sz = size / n;
    for (int i = 0; i < n; ++i) {
        ptrs[i] = buf_addr;
        buf_addr += sz;
    }
    printf("RIO TEST: %s: split buf %p of sz %u into %u of sz %u...\r\n",
           func, addr, size, n, sz);
    for (int i = 0; i < n; ++i) {
        ASSERT(ptrs[i] >= addr);
    }
    return sz;
}

static void print_buf(uint8_t *p, unsigned size)
{
    for (int i = 0; i < size; ++i) {
        printf("%02x ", p[i]);
    }
    printf("\r\n");
}

static int test_send_receive(struct rio_ep *ep_from, struct rio_ep *ep_to)
{
    int rc = 1;

    printf("RIO TEST: %s: running send/receive test %s->%s...\r\n",
           __func__, rio_ep_name(ep_from), rio_ep_name(ep_to));

    bzero(&out_pkt, sizeof(out_pkt));

    out_pkt.addr_width = ADDR_WIDTH;
    out_pkt.ttype = TRANSPORT_TYPE;

    out_pkt.src_id = rio_ep_get_devid(ep_from);
    out_pkt.dest_id = rio_ep_get_devid(ep_to);

    /* Choose a packet type that we know is not auto-handled by HW */
    out_pkt.ftype = RIO_FTYPE_MAINT;
    out_pkt.target_tid = 0x0;
    out_pkt.hop_count = 0xFF; /* not destined to any switch */
    out_pkt.transaction = RIO_TRANS_MAINT_RESP_READ;

    out_pkt.status = 0x1;
    out_pkt.payload_len = 1;
    out_pkt.payload[0] = 0x01020304deadbeef;

    printf("RIO TEST: sending pkt on EP %s:\r\n", rio_ep_name(ep_from));
    rio_print_pkt(&out_pkt);

    rc = rio_ep_sp_send(ep_from, &out_pkt);
    if (rc) goto exit;

    rc = rio_ep_sp_recv(ep_to, &in_pkt);
    if (rc) goto exit;

    printf("RIO TEST: received pkt on EP %s:\r\n", rio_ep_name(ep_to));
    rio_print_pkt(&in_pkt);

    /* TODO: receive packets until expected response or timeout,
     * instead of blocking on receive and checking the first pkt */
    if (!(in_pkt.ftype == RIO_FTYPE_MAINT &&
          in_pkt.transaction == out_pkt.transaction &&
          in_pkt.target_tid == out_pkt.target_tid &&
          in_pkt.payload_len == 1 && in_pkt.payload[0] == out_pkt.payload[0])) {
        printf("RIO TEST: ERROR: receive packet does not match sent packet\r\n");
        goto exit;
    }
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_read_csr(struct rio_ep *ep, rio_devid_t dest)
{
    uint32_t dev_id;
    uint32_t expected_dev_id = RIO_EP_DEV_ID;
    int rc = 1;

    printf("RIO TEST: running read CSR test...\r\n");

    rc = rio_ep_read_csr32(ep, &dev_id, rio_dev_dest(dest), DEV_ID);
    if (rc) goto exit;

    if (dev_id != expected_dev_id) {
        printf("RIO TEST EP %s: unexpected value of "
                "DEV_ID CSR in EP 0x%x: %x (expected %x)\r\n",
                rio_ep_name(ep), dest, expected_dev_id);
        rc = 1;
        goto exit;
    }

    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int wr_csr_field(struct rio_ep *ep, rio_dest_t dest,
                        uint32_t csr, unsigned offset,
                        uint8_t *bytes, unsigned len,
                        uint32_t csr_val)
{
    int rc = 1;
    unsigned b;
    uint32_t read_val;
    uint8_t in_bytes[sizeof(uint32_t)] = {0};

    ASSERT(len < sizeof(in_bytes));

    rc = rio_ep_write_csr(ep, bytes, len, dest, csr + offset);
    if (rc) goto exit;

    rc = rio_ep_read_csr32(ep, &read_val, dest, csr);
    if (rc) goto exit;

    if (read_val != csr_val) {
        printf("RIO TEST: FAILED: csr 0x%x on devid %u not of expected value: "
               "%08x != %08x\r\n", csr, dest, read_val, csr_val);
        goto exit;
    }

    rc = rio_ep_read_csr(ep, in_bytes, len, dest, csr + offset);
    if (rc) goto exit;

    for (b = 0; b < len; ++b) {
        if (in_bytes[b] != bytes[b]) {
            printf("RIO TEST: FAIL: csr 0x%x field %u:%u byte %u W/R mismatch:"
                   "%x != %x\r\n", csr, offset, len, b,
                    in_bytes[b], bytes[b]);
            goto exit;
        }
    }
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

/* Write/read in size smaller than dword and smaller than word (bytes) */
static int test_write_csr(struct rio_ep *ep, rio_devid_t dest)
{
    uint32_t csr = COMP_TAG;

    /* For the purposes of this test, we partition the csr word value into:
     * (bit numbering convention: 0 is MSB, as in the datasheet):
     *     0 - 15: short "s"
     *    16 - 23: byte "b"
     *    24 - 31: nothing
     */
    const uint16_t b_shift =  8;
    const uint16_t s_shift = 16;

    /* Four separate trials, using 3 sets of field values. */
    uint8_t val_b1  = 0xaa;
    uint8_t val_b2  = 0xdd;
    uint8_t val_b3  = 0x99;
    uint16_t val_s1 = 0xccbb;
    uint16_t val_s2 = 0xffee;
    uint16_t val_s3 = 0x8877;

    int rc = 1;
    uint32_t reg = 0, read_reg, orig_reg;
    uint8_t bytes[sizeof(uint32_t)] = {0};

    printf("RIO TEST: running write CSR test...\r\n");

    rio_dest_t dest_ep = rio_dev_dest(dest);

    printf("RIO TEST: write csr: saving CSR value\r\n");
    rc = rio_ep_read_csr32(ep, &orig_reg, dest_ep, csr);
    if (rc) goto exit;

    printf("RIO TEST: write csr: testing whole CSR\r\n");
    reg = ((uint32_t)val_b1 << b_shift) | ((uint32_t)val_s1 << s_shift);

    rc = rio_ep_write_csr32(ep, reg, dest_ep, csr);
    if (rc) goto exit;

    rc = rio_ep_read_csr32(ep, &read_reg, dest_ep, csr);
    if (rc) goto exit;

    if (read_reg != reg) {
        printf("RIO TEST: FAILED: csr 0x%x on devid %u not of expected value: "
               "%08x != %08x\n", csr, dest, read_reg, reg);
        goto exit;
    }

    printf("RIO TEST: write csr: testing field: byte\r\n");
    reg &= ~(0xff << b_shift);
    reg |= (uint32_t)val_b2 << b_shift;
    bytes[0] = val_b2;
    rc = wr_csr_field(ep, dest_ep, csr, /* offset */ b_shift / 8,
                      bytes, sizeof(val_b2), reg);
    if (rc) goto exit;

    printf("RIO TEST: write csr: testing field: short\r\n");
    reg &= ~(0xffff << s_shift);
    reg |= val_s2 << s_shift;
    bytes[0] = val_s2 >> 8;
    bytes[1] = val_s2 & 0xff;
    rc = wr_csr_field(ep, dest_ep, csr, /* offset */ s_shift / 8,
                      bytes, sizeof(val_s2), reg);
    if (rc) goto exit;

    printf("RIO TEST: write csr: testing fields: byte and short\r\n");
    reg = ((uint32_t)val_b3 << b_shift) | ((uint32_t)val_s3 << s_shift);
    bytes[0] = val_s3 >> 8;
    bytes[1] = val_s3 & 0xff;
    bytes[2] = val_b3;
    rc = wr_csr_field(ep, dest_ep, csr, /* offset */ b_shift / 8,
                      bytes, sizeof(val_b3) + sizeof(val_s3), reg);
    if (rc) goto exit;

    printf("RIO TEST: write csr: restoring CSR value\r\n");
    rc = rio_ep_read_csr32(ep, &orig_reg, dest_ep, csr);
    if (rc) goto exit;

    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_hop_routing(struct rio_ep *ep0, struct rio_ep *ep1)
{
    int rc = 1;

/* We don't do enumeration/discovery traversal, hence hop-based test is limited
 * (hop-based routing with multiple hops requires device IDs to be assigned
 * along the way on all but the last hop). */
#if (RIO_HOPS_FROM_EP0_TO_SWITCH > 0) || (RIO_HOPS_FROM_EP1_TO_SWITCH > 0)
#error RIO hop-based routing test supports only hop-count 0.
#endif

    rio_dest_t ep0_to_switch = rio_sw_dest(0, RIO_HOPS_FROM_EP0_TO_SWITCH);

    rc = rio_switch_map_remote(ep0, ep0_to_switch, RIO_DEVID_EP0,
            RIO_MAPPING_TYPE_UNICAST, (1 << RIO_EP0_SWITCH_PORT));
    if (rc) goto exit_0;

    rc = rio_switch_map_remote(ep0, ep0_to_switch, RIO_DEVID_EP1,
            RIO_MAPPING_TYPE_UNICAST, (1 << RIO_EP1_SWITCH_PORT));
    if (rc) goto exit_1;

    rc = test_send_receive(ep0, ep1);
    if (rc) goto exit;
    rc = test_send_receive(ep1, ep0);
    if (rc) goto exit;

    rc = 0;
exit:
    rc |= rio_switch_unmap_remote(ep0, ep0_to_switch, RIO_DEVID_EP0);
exit_1:
    rc |= rio_switch_unmap_remote(ep0, ep0_to_switch, RIO_DEVID_EP1);
exit_0:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_msg(struct rio_ep *ep0, struct rio_ep *ep1)
{
    const rio_devid_t dest = rio_ep_get_devid(ep1);
    const unsigned mbox = MBOX_CLIENT;
    const unsigned letter = LETTER_CLIENT;
    const unsigned seg_size = 8; /* TODO: MSG_SEG_SIZE */;
    uint64_t payload = 0x01020304deadbeef;

    int rc = 1;

    printf("RIO TEST: running message test...\r\n");

    rc = rio_ep_msg_send(ep0, dest, mbox, letter, seg_size,
                         (uint8_t *)&payload, sizeof(payload));
    if (rc) goto exit;

    rio_devid_t src_id = 0;
    uint64_t rcv_time = 0;
    uint64_t rx_payload = 0;
    unsigned payload_len = sizeof(rx_payload);
    rc = rio_ep_msg_recv(ep1, mbox, letter, &src_id, &rcv_time,
                         (uint8_t *)&rx_payload, &payload_len);
    if (rc) goto exit;

    printf("RIO TEST: recved msg from %u at %08x%08x payload len %u %08x%08x\r\n",
           src_id, (uint32_t)(rcv_time >> 32), (uint32_t)(rcv_time & 0xffffffff),
           payload_len,
           (uint32_t)(rx_payload >> 32), (uint32_t)(rx_payload & 0xffffffff));

    if (rx_payload != payload) {
        printf("RIO TEST: ERROR: received msg payload mismatches sent\r\n");
        goto exit;
    }
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_doorbell(struct rio_ep *ep0, struct rio_ep *ep1)
{
    int rc = 1;
    enum rio_status status;

    printf("RIO TEST: running doorbell test...\r\n");

    uint16_t info = 0xf00d, info_recv = 0;
    rc = rio_ep_doorbell_send_async(ep0, rio_ep_get_devid(ep1), info);
    if (rc) goto exit;
    rc = rio_ep_doorbell_recv(ep1, &info_recv);
    if (rc) goto exit;
    rc = rio_ep_doorbell_reap(ep0, &status);
    if (rc) goto exit;

    if (status != RIO_STATUS_DONE) {
        printf("RIO TEST: FAILED: doorbell request returned error: "
               "status 0x%x\n\n", status);
        goto exit;
    }
    if (info_recv != info) {
        printf("RIO TEST: FAILED: received doorbell has mismatched info: "
               "0x%x != 0x%x\r\n", info_recv, info);
        goto exit;
    }
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

#if 0 /* TODO: rio driver: support last segment < SEG_SIZE */
#define MSG_PING_WORDS 2
#else
#define MSG_PING_WORDS (MSG_SEG_SIZE / sizeof(uint32_t))
#endif
#define MSG_PING_BYTES (MSG_PING_WORDS * sizeof(uint32_t))

static bool check_pong(uint32_t *request, uint32_t *reply, size_t reply_len)
{
    if (!(reply_len == MSG_PING_BYTES &&
          reply[0] == CMD_PONG && reply[1] == request[1])) {
        printf("RIO TEST: ERROR: unexpected pong: len %u cmd 0x%x arg 0x%x "
               "(expecting len %u cmd 0x%x arg 0x%x)\r\n",
               reply_len, reply[0], reply[1],
               MSG_PING_BYTES, CMD_PONG, request[1]);
        return false;
    }
    return true;
}


#define RPC_MSG_SIZE_WORDS 2

struct t_msg_rpc_client {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_MSG_RPC_CLIENT_ST_INIT,
        T_MSG_RPC_CLIENT_ST_WAITING,
    } state;

    struct rio_ep *ep;
    unsigned mbox_server;
    unsigned letter_server;

    rio_devid_t ep_server;
    unsigned mbox_client;
    unsigned letter_client;

    uint32_t request[RPC_MSG_SIZE_WORDS];
    /* TODO: drv: support last segment < SEG_SIZE, then RPC_MSG_SIZE_WORDS here */
    uint32_t reply[MSG_SEG_SIZE / sizeof(uint32_t)];
    size_t reply_len;
    enum link_rpc_status rpc_status;

    struct link *link;
    struct sw_timer tmr;
};

static void test_msg_rpc_client_on_pong(void *opaque)
{
    struct t_msg_rpc_client *s = opaque;
    post(s->ev_loop, /* sender */ NULL, &s->actor, EV_PONG);
}

static void test_msg_rpc_client_on_timeout(void *opaque)
{
    struct t_msg_rpc_client *s = opaque;
    post(s->ev_loop, /* sender */ NULL, &s->actor, EV_TIMEOUT);
}

static void test_msg_rpc_client_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_msg_rpc_client *s =
        container_of(struct t_msg_rpc_client, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ssize_t sz;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_MSG_RPC_CLIENT_ST_INIT:
        ASSERT(ev == EV_START);

        s->link = rio_link_connect("RIO_CLIENT_LINK", /* is_server */ false,
                s->ep, s->mbox_client, s->letter_client,
                s->ep_server, s->mbox_server, s->letter_server, MSG_SEG_SIZE);
        if (!s->link) {
            rc = 1;
            goto exit;
        }

        size_t req_word = 0;
        ASSERT(req_word < LEN(s->request));
        s->request[req_word++] = CMD_PING;
        ASSERT(req_word < LEN(s->request));
        s->request[req_word++] = 42;

        s->reply_len = sizeof(s->reply);
        s->rpc_status = LINK_RPC_UNKNOWN;

        sz = s->link->request_async(s->link, TIMEOUT_MS,
                s->request, sizeof(s->request), s->reply, &s->reply_len,
                &s->rpc_status, test_msg_rpc_client_on_pong, s);
        if (sz != sizeof(s->request)) {
            printf("RIO TEST: %s: request failed: rc %d\r\n", __func__, sz);
            rc = 1;
            break;
        }
        printf("RIO TEST: %s: ping sent\r\n", __func__);
        sw_timer_schedule(&s->tmr, 5000 /* ms */, SW_TIMER_ONESHOT,
                          test_msg_rpc_client_on_timeout, s);
        s->state = T_MSG_RPC_CLIENT_ST_WAITING;
        return;
    case T_MSG_RPC_CLIENT_ST_WAITING:
        switch (ev) {
        case EV_PONG:
            sw_timer_cancel(&s->tmr);
            printf("RIO TEST: %s: rpc rc 0x%x\r\n", __func__, s->rpc_status);
            if (s->rpc_status != LINK_RPC_OK) {
                rc = 1;
                break;
            }
            if (!check_pong(s->request, s->reply, s->reply_len))
                break;
            break;
        case EV_TIMEOUT:
            printf("RIO TEST: %s: timed out\r\n", __func__);
            rc = 1;
            break;
        default:
            PANIC("unexpected event");
        }
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    rc |= s->link->disconnect(s->link);
exit:
    LOG_RESULT(rc, s->state);
    s->state = T_MSG_RPC_CLIENT_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_msg_rpc_client_start(struct t_msg_rpc_client *s,
        struct ev_loop *loop, struct ev_actor *parent, struct rio_switch *sw,
        struct rio_ep *ep, unsigned mbox_client, unsigned letter_client,
        rio_devid_t ep_server, unsigned mbox_server, unsigned letter_server)
{
    s->actor.name = "test_msg_rpc_client";
    s->actor.func = test_msg_rpc_client_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->ep_server = ep_server;
    s->mbox_server = mbox_server;
    s->letter_server = letter_server;
    s->mbox_client = mbox_client;
    s->letter_client = letter_client;
    s->state = T_MSG_RPC_CLIENT_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

struct t_msg_rpc_server {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_MSG_RPC_SERVER_INIT,
        T_MSG_RPC_SERVER_LISTENING,
    } state;

    struct rio_ep *ep;
    unsigned mbox_server;
    unsigned letter_server;
    rio_devid_t ep_client;
    unsigned mbox_client;
    unsigned letter_client;
    struct link *link;
};
static void test_msg_rpc_server_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_msg_rpc_server *s =
        container_of(struct t_msg_rpc_server, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_MSG_RPC_SERVER_INIT:
        ASSERT(ev == EV_START);
        s->link = rio_link_connect("RIO_SERVER_LINK", /* is_server */ true,
                s->ep, s->mbox_server, s->letter_server,
                s->ep_client, s->mbox_client, s->letter_client,
                MSG_SEG_SIZE);
        if (!s->link) {
            rc = 1;
            goto exit;
        }
        s->state = T_MSG_RPC_SERVER_LISTENING;
        return;
    case T_MSG_RPC_SERVER_LISTENING:
        ASSERT(ev == EV_STOP);
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    ASSERT(s->link);
    s->link->disconnect(s->link);
exit:
    LOG_RESULT(rc, s->state);
    s->state = T_MSG_RPC_SERVER_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_msg_rpc_server_start(struct t_msg_rpc_server *s,
        struct ev_loop *loop, struct ev_actor *parent, struct rio_ep *ep,
        unsigned mbox_server, unsigned letter_server,
        rio_devid_t ep_client, unsigned mbox_client, unsigned letter_client)
{
    s->actor.name = "test_msg_rpc_server";
    s->actor.func = test_msg_rpc_server_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->ep_client = ep_client;
    s->mbox_server = mbox_server;
    s->letter_server = letter_server;
    s->mbox_client = mbox_client;
    s->letter_client = letter_client;
    s->state = T_MSG_RPC_SERVER_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

static uint64_t uint_from_buf(uint8_t *buf, unsigned bytes)
{
    uint64_t v = 0;
    for (int b = 0; b < bytes; ++b) {
        v <<= 8;
        v |= buf[bytes - b - 1];
    }
    return v;
}

static bool cmp_val(uint64_t val, uint64_t ref)
{
    if (val != ref) {
        printf("TEST: RIO map: value mismatch: 0x%08x%08x != 0x%08x%08x\r\n",
                (uint32_t)(val >> 32), (uint32_t)(val & 0xffffffff),
                (uint32_t)(ref >> 32), (uint32_t)(ref & 0xffffffff));
        return false;
    }
    return true;
}

struct map_tgt_info {
    const char *name;
    rio_devid_t dest_id;
    uint32_t size;
    /* Address of outgoing region in EP0's system address space */
    uint64_t out_addr;
    /* 32-bit system bus addr of window into out_addr translated by MMU */
    uint8_t *window_addr;
    /* Address of target region on the RapidIO interconnect */
    rio_addr_t rio_addr;
    bool bus_window; /* whether to create a window into bus_addr */
    /* Address the remote endpoint uses on its system bus for the target */
    rio_bus_addr_t bus_addr; /* optional */
    /* 32-bit system bus addr of window into bus_addr translated by MMU */
    uint8_t *bus_win_addr; /* optional */
};

enum {
    OUT_REGION_CFG_SPACE = 0,
    OUT_REGION_TEST_BUF,
    NUM_OUT_REGIONS
};

static const struct map_tgt_info base_map_targets[] = {
    [OUT_REGION_CFG_SPACE] = {
        .name = STR(OUT_REGION_CFG_SPACE),
        .size = CFG_SPACE_SIZE,
        .window_addr = (uint8_t *)TEST_OUT_CFG_WIN_ADDR,
        .rio_addr = CFG_SPACE_BASE,
        .bus_window = false,
        .bus_addr = 0, /* unset */
    },
    [OUT_REGION_TEST_BUF] = {
        .name = STR(OUT_REGION_TEST_BUF),
        .size = RIO_MEM_TEST_SIZE,
        .window_addr = (uint8_t *)TEST_OUT_BUF_WIN_ADDR,
        .rio_addr = { 0, TEST_BUF_ADDR & ALIGN64_MASK(ADDR_WIDTH) },
        .bus_addr = TEST_BUF_ADDR,
        .bus_window = true,
        .bus_win_addr = (uint8_t *)TEST_BUF_WIN_ADDR,
    },
};

static void map_targets_init(struct map_tgt_info *map_targets,
        size_t num_map_targets, rio_devid_t dest)
{
    ASSERT(num_map_targets == LEN(base_map_targets));
    for (int i = 0; i < LEN(base_map_targets); ++i) {
        map_targets[i] = base_map_targets[i];
        map_targets[i].dest_id = dest;
    }
}

/* The endpoint determines which region config to use exclusively
 * based on source device ID, and not based on address. TODO: true? */
enum {
    IN_ADDR_SPACE_FROM_EP0 = 0,
    NUM_IN_ADDR_SPACES
};
static struct rio_map_in_as_cfg in_as_cfgs[] = {
    [IN_ADDR_SPACE_FROM_EP0] = {
        .src_id = RIO_DEVID_EP0,
        .src_id_mask = 0xff,
        .addr_width = ADDR_WIDTH,

        /* Configuring for the test memory region to be accessible;
           if you want additional accessibility outside this region,
           then the only way is to increase RIO address width.
           These bus address high bits are only relevant when bus
           address is wider than RIO address (e.g. bus is 40-bit, RIO is
           34-bit. TODO: <--- is this how the real HW works. */
        .bus_addr = RIO_MEM_TEST_ADDR & ~ALIGN64_MASK(ADDR_WIDTH),
    },
};

static rio_addr_t saved_cfg_base;

static int map_setup_in(struct rio_ep *ep)
{
    /* CSR space is always mapped into incoming region; set the location
     * explicitly to not rely on default value upon reset. */
    saved_cfg_base = rio_ep_get_cfg_base(ep);
    rio_ep_set_cfg_base(ep, cfg_space_base);

    ASSERT(sizeof(in_as_cfgs) / sizeof(in_as_cfgs[0]) == NUM_IN_ADDR_SPACES);
    return rio_ep_map_in(ep, NUM_IN_ADDR_SPACES, in_as_cfgs);
}
static void map_teardown_in(struct rio_ep *ep)
{
    rio_ep_set_cfg_base(ep, saved_cfg_base);
    rio_ep_unmap_in(ep);
}

static int map_setup_out(struct rio_ep *ep,
        int in_addr_width, rio_bus_addr_t in_bus_addr,
        struct map_tgt_info *map_targets, unsigned num_targets)
{
    /* The RIO address is split into: <hi bits> | <within region offset> */
    int out_region_size_bits = rio_ep_get_outgoing_size_bits(ep, num_targets);

    ASSERT(num_targets <= NUM_OUT_REGIONS);
    struct rio_map_out_region_cfg out_region_cfgs[NUM_OUT_REGIONS];

    for (int i = 0; i < num_targets; ++i) {
        struct map_tgt_info *tgt = &map_targets[i];

        /* LCS register maps the configuration space (CSR) into the incoming
         * address space, so make sure our buffer does not overlap. In other
         * words, the range [0..16MB] of the system bus address space cannot be
         * accessed via RapidIO requests. */
        rio_addr_t cfg_space_bound =
            rio_addr_add64(cfg_space_base, RIO_CSR_REGION_SIZE);
        if (i != OUT_REGION_CFG_SPACE &&
            rio_addr_cmp(cfg_space_base, tgt->rio_addr) <= 0 &&
            rio_addr_cmp(tgt->rio_addr, cfg_space_bound) < 0) {
            printf("RIO TEST: ERROR: target %s overlaps with RapidIO CSR region"
                   " ([0..0x%x]): tgt 0x%02x%08x%08x cfg 0x%02x%08x%08x\r\n",
                   tgt->name, RIO_CSR_REGION_SIZE,
                   tgt->rio_addr.hi,
                   (uint32_t)(tgt->rio_addr.lo >> 32),
                   (uint32_t)tgt->rio_addr.lo,
                   cfg_space_base.hi,
                   (uint32_t)(cfg_space_base.lo >> 32),
                   (uint32_t)cfg_space_base.lo);
            return 1;
        }

        rio_bus_addr_t bus_addr = tgt->bus_addr & ~ALIGN64_MASK(ADDR_WIDTH);
        if (bus_addr != in_bus_addr) {
            printf("RIO TEST: ERROR: "
                   "target %s not addressable via incoming region: "
                   "bus addr (hibits): trgt %08x%08x != incoming %08x%08x\r\n",
                   tgt->name, (uint32_t)(bus_addr >> 32), (uint32_t)bus_addr,
                   (uint32_t)(in_bus_addr>> 32), (uint32_t)in_bus_addr);
            return 1;
        }

        ASSERT(out_region_size_bits <= sizeof(tgt->rio_addr.lo) * 8);
        tgt->out_addr = rio_ep_get_outgoing_base(ep, NUM_OUT_REGIONS, i) +
                (tgt->rio_addr.lo & ALIGN64_MASK(out_region_size_bits));

        struct rio_map_out_region_cfg *reg_cfg = &out_region_cfgs[i];
        reg_cfg->ttype = TRANSPORT_TYPE;
        reg_cfg->dest_id = tgt->dest_id;
        reg_cfg->addr_width = in_addr_width;

        ASSERT(out_region_size_bits <= sizeof(tgt->rio_addr.lo) * 8);
        rio_addr_t out_rio_addr = {
            tgt->rio_addr.hi,
            tgt->rio_addr.lo & ~ALIGN64_MASK(out_region_size_bits)
        };
        reg_cfg->rio_addr = out_rio_addr;
    }

    ASSERT(sizeof(out_region_cfgs) / sizeof(out_region_cfgs[0]) ==
           NUM_OUT_REGIONS);
    int rc = rio_ep_map_out(ep, NUM_OUT_REGIONS, out_region_cfgs);
    if (rc) return rc;

    for (int i = 0; i < NUM_OUT_REGIONS; ++i) {
        struct map_tgt_info *tgt = &map_targets[i];

        if (tgt->bus_window) {
            ASSERT(i > 0);
            printf("RIO TEST: MAPPING %x sz %x\r\n",
                    (uintptr_t)tgt->bus_win_addr, tgt->size);
            rc = rt_mmu_map((uintptr_t)tgt->bus_win_addr, tgt->bus_addr,
                            tgt->size);
            if (rc) goto cleanup;
        } else {
            tgt->bus_win_addr = NULL;
        }

        rc = rt_mmu_map((uintptr_t)tgt->window_addr, tgt->out_addr, tgt->size);
        if (rc) {
            if (tgt->bus_window)
                rt_mmu_unmap((uintptr_t)tgt->bus_win_addr, tgt->size);
            goto cleanup;
        }

        continue;
cleanup:
        for (--i; i >= 0; --i) {
            struct map_tgt_info *tgt = &map_targets[i];
            rt_mmu_unmap((uintptr_t)tgt->window_addr, tgt->size);
            if (tgt->bus_window)
                rt_mmu_unmap((uintptr_t)tgt->bus_win_addr, tgt->size);
        }
        rio_ep_unmap_out(ep);
        return 1;
    }

    printf("RIO TEST: mapped targets:\r\n");
    for (int i = 0; i < NUM_OUT_REGIONS; ++i) {
        struct map_tgt_info *tgt = &map_targets[i];
        printf("\tTarget %s:\r\n"
               "\t\t  on RIO interconnect: %02x%08x%08x\r\n"
               "\t\tvia RIO to EP1 to mem:   %08x%08x [window: %p]\r\n"
               "\t\t       via system bus:   %08x%08x [window: %p]\r\n",
                tgt->name,
                tgt->rio_addr.hi,
                (uint32_t)(tgt->rio_addr.lo >> 32), (uint32_t)tgt->rio_addr.lo,
                (uint32_t)(tgt->out_addr >> 32), (uint32_t)tgt->out_addr,
                tgt->window_addr,
                (uint32_t)(tgt->bus_addr >> 32), (uint32_t)tgt->bus_addr,
                tgt->bus_win_addr);
    }
    return 0;
}

static void map_teardown_out(struct rio_ep *ep,
        struct map_tgt_info *map_targets, unsigned num_targets)
{
    for (int i = 0; i < NUM_OUT_REGIONS; ++i) {
        struct map_tgt_info *tgt = &map_targets[i];
        rt_mmu_unmap((uintptr_t)tgt->window_addr, tgt->size);
        if (tgt->bus_window)
            rt_mmu_unmap((uintptr_t)tgt->bus_win_addr, tgt->size);
    }
    rio_ep_unmap_out(ep);
}

/* Returns number of bytes processed */
static size_t map_iter(uint8_t *addr, unsigned mem_size, uint8_t *ref_dw,
        int (*cb)(uint8_t *granule_addr, unsigned sz, uint8_t*ref))
{
    int i, j, k;

    /* Note: When this test runs on a 32-bit processor, the 64-bit access
     * will be broken up into two 32-bit accesses by the compiler. */
    const unsigned sizes[] = { sizeof(uint8_t), sizeof(uint16_t),
                               sizeof(uint32_t), sizeof(uint64_t) };
    const unsigned num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    const unsigned num_size_sets = 2; /* same test iter at diff locations */
    const unsigned test_sz = num_size_sets * num_sizes * sizeof(uint64_t);
    unsigned bytes = 0;

    ASSERT(test_sz <= mem_size);
    for (j = 0; j < num_size_sets; ++j) {
        uint8_t *set_addr = addr + j * num_sizes * sizeof(uint64_t);
        for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            unsigned size = sizes[i];
            uint8_t *dw_addr = set_addr + i * sizeof(uint64_t);
            for (k = 0; k < sizeof(uint64_t) /* dword */; k += size) {
                uint8_t *granule_addr = (uint8_t *)dw_addr + k;
                ASSERT(cb);
                int rc = cb(granule_addr, size, ref_dw);
                if (rc)
                    return 0;
                bytes += size;
            }
        }
    }
    return bytes;
}
static int map_granule_write_bus(uint8_t *addr, unsigned size, uint8_t *ref_dw)
{
    /* Write ref[0:size] directly to mem */
    memcpy(addr, ref_dw, size);
    DPRINTF("RIO TEST: filled size %u bytes @%p\r\n", size, addr);
    return 0;
}
static size_t map_write_granules_bus(uint8_t *sys_addr, unsigned mem_size,
        uint8_t *ref_dw)
{
    printf("RIO TEST: map granule test fill via local STORE...\r\n");
    bzero(sys_addr, mem_size);
    return map_iter(sys_addr, mem_size, ref_dw, map_granule_write_bus);
}
static int map_granule_read_bus(uint8_t *addr, unsigned size, uint8_t *ref_dw)
{
    /* Read ref[0:size] directly from mem and compare with reference */
    uint64_t val = uint_from_buf(addr, size);
    uint64_t ref_val = uint_from_buf(ref_dw, size);
    return cmp_val(val, ref_val) ? 0 : 1;
}
static size_t map_read_granules_bus(uint8_t *sys_addr, unsigned mem_size,
        uint8_t *ref_dw)
{
    printf("RIO TEST: map granule test check via local LOAD...\r\n");
    return map_iter(sys_addr, mem_size, ref_dw, map_granule_read_bus);
}
static int map_granule_read_rio(uint8_t *addr, unsigned size, uint8_t *ref_dw)
{
    uint64_t ref_val = uint_from_buf(ref_dw, size);
    /* Read ref[0:size] via RIO */
    uint64_t val = 0;
    DPRINTF("RIO TEST: reading %u bytes @%x (ref %08x%08x)\r\n", size, addr,
            (uint32_t)(ref_val >> 32), (uint32_t)ref_val);
    switch (size) {
        case sizeof(uint8_t):  val = *((uint8_t *)(addr));  break;
        case sizeof(uint16_t): val = *((uint16_t *)(addr)); break;
        case sizeof(uint32_t): val = *((uint32_t *)(addr)); break;
        case sizeof(uint64_t): val = *((uint64_t *)(addr)); break;
        default: PANIC("invalid size");
    }
    return cmp_val(val, ref_val) ? 0 : 1;
}
static size_t map_read_granules_rio(uint8_t *out_addr, unsigned mem_size,
        uint8_t *ref_dw)
{
    printf("RIO TEST: map granule test read via RIO...\r\n");
    return map_iter(out_addr, mem_size, ref_dw, map_granule_read_rio);
}

static int map_granule_write_rio(uint8_t *addr, unsigned size, uint8_t *ref)
{
    /* Write test_dw[0:size] via RIO */
    uint64_t ref_val = uint_from_buf(ref, size);
    printf("RIO TEST: writing %u bytes of ref %08x%08x to @%x\r\n", size,
            (uint32_t)(ref_val >> 32), (uint32_t)ref_val, addr);
    switch (size) {
        case sizeof(uint8_t):  *((uint8_t *) addr) = (uint8_t)ref_val;  break;
        case sizeof(uint16_t): *((uint16_t *)addr) = (uint16_t)ref_val; break;
        case sizeof(uint32_t): *((uint32_t *)addr) = (uint32_t)ref_val; break;
        case sizeof(uint64_t): *((uint64_t *)addr) = (uint64_t)ref_val; break;
    }
    return 0;
}
static size_t map_write_granules_rio(uint8_t *out_addr, unsigned mem_size,
        uint8_t *ref_dw)
{
    printf("RIO TEST: map granule test write via RIO...\r\n");
    return map_iter(out_addr, mem_size, ref_dw, map_granule_write_rio);
}

static int test_onchip_map_granule(uint8_t *sys_addr, uint8_t *out_addr,
        unsigned mem_size, uint8_t *ref_dw)
{
    int rc = 1;
    int sz;

    sz = map_write_granules_bus(sys_addr, mem_size, ref_dw);
    if (!sz) goto exit;
    sz = map_read_granules_rio(out_addr, mem_size, ref_dw);
    if (!sz) goto exit;

    bzero(sys_addr, mem_size);

    sz = map_write_granules_rio(out_addr, mem_size, ref_dw);
    if (!sz) goto exit;
    sz = map_read_granules_bus(out_addr, mem_size, ref_dw);
    if (!sz) goto exit;

    /* To silence warnings when ifdef'ing code out for debugging */
    (void)map_read_granules_bus;
    (void)map_write_granules_bus;
    (void)map_read_granules_rio;
    (void)map_write_granules_rio;

    /* TODO: Test misaligned access */
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static void fill_buf(uint8_t *addr, unsigned size, uint8_t ref)
{
    printf("RIO TEST: fill buf @%p size 0x%x\r\n", addr, size);
    for (int i = 0; i < size; ++i)
        addr[i] = (uint8_t)i ^ ref;
}
static int check_buf(uint8_t *addr, unsigned size, uint8_t ref)
{
    printf("RIO TEST: checking buf @%p size 0x%x...\r\n", addr, size);
    for (int i = 0; i < size; ++i) {
        if (addr[i] != ((uint8_t)i ^ ref)) {
            printf("RIO TEST: mismatch in buf[%d]: 0x%x != 0x%x\r\n",
                   i, addr[i], i);
            return 1;
        }
    }
    printf("RIO TEST: checked buf @%p size %u...\r\n", addr, size);
    return 0;
}

static int map_write_burst_bus(uint8_t *sys_addr, unsigned size, uint8_t ref)
{
    printf("RIO TEST: map burst write @%p sz 0x%x over bus, ref %x...\r\n",
           sys_addr, size, ref);
    fill_buf(sys_addr, size, ref);
    return 0;
}
static int map_read_burst_bus(uint8_t *sys_addr, unsigned size, uint8_t ref)
{
    printf("RIO TEST: map burst read @%p sz 0x%x over bus, ref %x...\r\n",
           sys_addr, size, ref);
    return check_buf(sys_addr, size, ref);
}

static int map_read_burst_rio(uint8_t *out_addr, unsigned size,
        struct dma *dmac, unsigned chan, uint8_t ref)
{
    printf("RIO: TEST: %s: DMA from mapped region @%p size %u...\r\n",
           __func__, out_addr, size);
    bzero(test_buf, size);
    struct dma_tx *dtx = dma_transfer(dmac, chan,
        (uint32_t *)out_addr, (uint32_t *)test_buf, size,
         /* no callback */ NULL, NULL);
    int rc = dma_wait(dtx);
    if (rc) {
        printf("RIO: TEST: %s: transfer from mapped region failed: rc %u\r\n",
               __func__, rc);
        return rc;
    }

    const int buf_preview_len = 16;
    printf("RIO: TEST: %s: data read via DMA (first %u bytes): ",
           __func__, buf_preview_len);
    print_buf(test_buf, buf_preview_len);

    return check_buf(test_buf, size, ref);
}

static int map_write_burst_rio(uint8_t *out_addr, unsigned size,
        struct dma *dmac, unsigned chan, uint8_t ref)
{
    printf("RIO: TEST: %s: DMA to mapped region @%p size %u...\r\n",
           __func__, out_addr, size);
    fill_buf(test_buf, size, ref);
    struct dma_tx *dtx = dma_transfer(dmac, chan,
        (uint32_t *)test_buf, (uint32_t *)out_addr, size,
        /* no callback */ NULL, NULL);
    int rc = dma_wait(dtx);
    if (rc) {
        printf("RIO: TEST: %s: transfer to mapped region failed: rc %u\r\n",
               __func__, rc);
        return rc;
    }
    return 0;
}

static int test_onchip_map_burst(
        uint8_t *sys_addr, uint8_t *out_addr, unsigned size,
        struct dma *dmac, unsigned chan)
{
    int rc;

    ASSERT(ALIGNED(size, DMA_MAX_BURST_BITS));
    ASSERT(ALIGNED(sys_addr, DMA_MAX_BURST_BITS));
    ASSERT(ALIGNED(out_addr, DMA_MAX_BURST_BITS));
    ASSERT(ALIGNED(&test_buf[0], DMA_MAX_BURST_BITS));

    rc = map_write_burst_bus(sys_addr, size, ref_bytes[0]);
    if (rc) return rc;
    rc = map_read_burst_rio(out_addr, size, dmac, chan, ref_bytes[0]);
    if (rc) return rc;

    bzero(sys_addr, size);

    rc = map_write_burst_rio(out_addr, size, dmac, chan, ref_bytes[1]);
    if (rc) return rc;
    rc = map_read_burst_bus(sys_addr, size, ref_bytes[1]);
    if (rc) return rc;

    return 0;
}

static int test_rw_cfg_space(struct rio_ep *ep, rio_devid_t dest)
{
    int rc = 1;
    uint32_t csr_addr; /* 16MB range */
    rio_addr_t csr_rio_addr; /* up to 66-bit RapidIO address */
    uint32_t csr_ref, csr_val, csr_saved;

    printf("RIO TEST: running read/write cfg space test...\r\n");

    csr_addr = DEV_ID;
    csr_rio_addr = rio_addr_from_u64(csr_addr);
    csr_ref = RIO_EP_DEV_ID;
    csr_val = 0;

    printf("RIO TEST: read cfg space at addr 0x%08x\r\n", csr_addr);
    rc = rio_ep_read32(ep, &csr_val, csr_rio_addr, dest);
    if (rc) goto exit;

    if (csr_val != csr_ref) {
        printf("RIO TEST: ERROR: read CSR value mismatches expected: "
               "0x%x != 0x%x\r\n", csr_val, csr_ref);
        goto exit;
    }

    csr_addr = COMP_TAG;
    csr_rio_addr = rio_addr_from_u64(csr_addr);
    csr_ref = 0xd00df00d;
    csr_val = 0;

    printf("RIO TEST: save/write/read/restore cfg space at addr 0x%08x\r\n",
           csr_addr);
    rc = rio_ep_read32(ep, &csr_saved, csr_rio_addr, dest);
    if (rc) goto exit;
    rc = rio_ep_write32(ep, csr_ref, csr_rio_addr, dest);
    if (rc) goto exit;
    rc = rio_ep_read32(ep, &csr_val, csr_rio_addr, dest);
    if (rc) goto exit;
    if (csr_val != csr_ref) {
        printf("RIO TEST: ERROR: read CSR value mismatches expected: "
               "0x%x != 0x%x\r\n", csr_val, csr_ref);
        goto exit;
    }
    rc = rio_ep_write32(ep, csr_saved, csr_rio_addr, dest);
    if (rc) goto exit;

    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_map_rw_cfg_space(uint32_t *csr_out_addr)
{
    int rc = 1;
    uint32_t csr_addr; /* 16MB range */
    uint32_t *csr_ptr; /* pointer via outgoing region */
    uint32_t csr_ref, csr_val, csr_saved;

    printf("RIO TEST: running read/write cfg space via mapped region...\r\n");

    csr_addr = DEV_ID;
    csr_ptr = (uint32_t *)((uint8_t *)csr_out_addr + csr_addr);
    csr_ref = RIO_EP_DEV_ID;

    printf("RIO TEST: read CSR 0x%x at addr %p\r\n", csr_addr, csr_ptr);
    csr_val = *csr_ptr;
    if (csr_val != csr_ref) {
        printf("RIO TEST: ERROR: read CSR 0x%x value mismatches expected: "
               "0x%x != 0x%x\r\n", csr_addr, csr_val, csr_ref);
        goto exit;
    }

    csr_addr = COMP_TAG;
    csr_ptr = (uint32_t *)((uint8_t *)csr_out_addr + csr_addr);
    csr_ref = 0xd00df00d;
    csr_val = 0;

    printf("RIO TEST: save/write/read/restore CSR 0x%x at addr %p\r\n",
           csr_addr, csr_ptr);
    csr_saved = *csr_ptr;
    *csr_ptr = csr_ref;
    csr_val = *csr_ptr;
    if (csr_val != csr_ref) {
        printf("RIO TEST: ERROR: CSR 0x%x mismatches written value: "
               "0x%x != 0x%x\r\n", csr_addr, csr_val, csr_ref);
        goto exit;
    }
    *csr_ptr = csr_saved;
    rc = 0;
exit:
    printf("RIO TEST: %s: %s\r\n", __func__, rc ? "FAILED" : "PASSED");
    return rc;
}

static int test_onchip_map_mmio(const struct map_tgt_info *map_targets,
        unsigned num_map_targets,
        struct dma *dmac, unsigned chan)
{
    int rc;
    ASSERT(OUT_REGION_TEST_BUF < num_map_targets);
    ASSERT(OUT_REGION_CFG_SPACE < num_map_targets);
    const struct map_tgt_info *buf_tgt = &map_targets[OUT_REGION_TEST_BUF];
    const struct map_tgt_info *cfg_tgt = &map_targets[OUT_REGION_CFG_SPACE];

    rc = test_onchip_map_granule(buf_tgt->bus_win_addr, buf_tgt->window_addr,
                                 buf_tgt->size, ref_dwords[0]);
    if (rc) goto fail;
    bzero(buf_tgt->bus_win_addr, buf_tgt->size);

    rc = test_onchip_map_burst(buf_tgt->bus_win_addr, buf_tgt->window_addr,
                               buf_tgt->size, dmac, chan);
    if (rc) goto fail;
    bzero(buf_tgt->bus_win_addr, buf_tgt->size);

    rc = test_map_rw_cfg_space((uint32_t *)cfg_tgt->window_addr);
    if (rc) goto fail;

    rc = 0;
fail:
    return rc;
}

static int test_onchip_map(struct rio_ep *ep0, struct rio_ep *ep1,
        struct dma *dmac, unsigned chan)
{
    int rc = 1;

    rc = map_setup_in(ep1);
    if (rc) goto fail_in;

    struct rio_map_in_as_cfg *in_cfg = &in_as_cfgs[IN_ADDR_SPACE_FROM_EP0];

    /* Lifetime is equal to lifetime of this func, just don't alloc on stack */
    static struct map_tgt_info out_regions[NUM_OUT_REGIONS];
    map_targets_init(out_regions, LEN(out_regions), rio_ep_get_devid(ep1));

    rc = map_setup_out(ep0, in_cfg->addr_width, in_cfg->bus_addr,
                       out_regions, LEN(out_regions));
    if (rc) goto fail_out;

    rc = test_onchip_map_mmio(out_regions, LEN(out_regions), dmac, chan);
    if (rc) goto fail;

    rc = test_rw_cfg_space(ep0, rio_ep_get_devid(ep1));
    if (rc) goto fail;

    rc = 0;
fail:
    map_teardown_out(ep0, out_regions, LEN(out_regions));
fail_out:
    map_teardown_in(ep1);
fail_in:
    return rc;
}

/* The strong test of mapped regions consists of two tests in sequence
 * (granule, then burst), each of which uses three different buffers, the
 * sequence of events is:
      1. server fills bufA with testword0
      2. client reads bufA
      3. client writes bufB
      4. server checks bufB, fills bufC with testword1
      5. client reads bufC
*/
/* on server: bus addresses, on client: outgoing window addrs */
#define T_MAP_STRONG_NUM_BUFFERS 8 /* (3+3) rouded up to nearest pow of 2*/

struct t_map_strong_client {
    struct ev_loop *ev_loop;
    struct ev_actor actor;
    struct ev_actor *parent_actor;
    enum {
        T_MAP_STRONG_CLT_ST_INIT,
        T_MAP_STRONG_CLT_ST_GRANULE_READ_BACK,
        T_MAP_STRONG_CLT_ST_BURST_READ_BACK,
    } state;

    struct dma *dmac;
    unsigned dma_chan;

    uint8_t *buf_addrs[T_MAP_STRONG_NUM_BUFFERS];
    size_t buf_size; /* size of each buffer */

    struct sw_timer tmr;
};

static void test_map_strong_on_tick(void *opaque)
{
    struct t_map_strong_client *s = opaque;
    post(s->ev_loop, /* sender */ NULL, &s->actor, EV_TICK);
}

/* This test could be synchronous -- we just need to wait for the remote
 * (blindly by time or not blindly by polling for value in buffer), and the
 * difference between sync vs async is just whether we block while we wait or
 * not. We could block, but we don't since we have async infrastructure anyway.
 */
static void test_map_strong_client_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_map_strong_client *s =
        container_of(struct t_map_strong_client, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    int g; /* number of granules */
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_MAP_STRONG_CLT_ST_INIT:
        ASSERT(ev == EV_START);

        /* Read data filled by remote end in buffer 0 */
        g = map_read_granules_rio(s->buf_addrs[0], s->buf_size, ref_dwords[0]);
        if (!g) break;

        /* Write other data into buffer 1 */
        g = map_write_granules_rio(s->buf_addrs[1], s->buf_size, ref_dwords[1]);
        if (!g) break;

        /* Wait for remote end to check buffer 1 and fill buffer 2 */
        sw_timer_schedule(&s->tmr, 5000 /* ms */, SW_TIMER_ONESHOT,
                test_map_strong_on_tick, s);
        s->state = T_MAP_STRONG_CLT_ST_GRANULE_READ_BACK;
        return;
    case T_MAP_STRONG_CLT_ST_GRANULE_READ_BACK:
        ASSERT(ev == EV_TICK);

        /* Read data filled by remote in buffer 2 */
        g = map_read_granules_rio(s->buf_addrs[2], s->buf_size, ref_dwords[2]);
        if (!g) break;

        /* Read data filled by remote in buffer 3 */
        rc = map_read_burst_rio(s->buf_addrs[3], s->buf_size,
                s->dmac, s->dma_chan, ref_bytes[0]);
        if (rc) break;

        /* Write other data into buffer 4 */
        rc = map_write_burst_rio(s->buf_addrs[4], s->buf_size,
                s->dmac, s->dma_chan, ref_bytes[1]);
        if (rc) break;

        /* Wait for remote end to check buffer 4 and fill buffer 5 */
        sw_timer_schedule(&s->tmr, 2000 /* ms */, SW_TIMER_ONESHOT,
                test_map_strong_on_tick, s);
        s->state = T_MAP_STRONG_CLT_ST_BURST_READ_BACK;
        return;
    case T_MAP_STRONG_CLT_ST_BURST_READ_BACK:
        ASSERT(ev == EV_TICK);

        /* Read data filled by remote in buffer 5 */
        rc = map_read_burst_rio(s->buf_addrs[5], s->buf_size,
                s->dmac, s->dma_chan, ref_bytes[2]);
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_MAP_STRONG_CLT_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_map_strong_client_start(struct t_map_strong_client *s,
            struct ev_loop *loop, struct ev_actor *parent,
            uint8_t *buf_out_addr, size_t buf_size,
            struct dma *dmac, unsigned chan)
{
    s->actor.name = "test_map_strong_client";
    s->actor.func = test_map_strong_client_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->dmac = dmac;
    s->dma_chan = chan;
    s->state = T_MAP_STRONG_CLT_ST_INIT;

    /* Split the buffer into so that we have disjoint buffers in order
       to not need clear (which we cannot do from the client). */
    s->buf_size = split_buffers(s->buf_addrs, T_MAP_STRONG_NUM_BUFFERS,
                                buf_out_addr, buf_size, __func__);
    post(s->ev_loop, parent, &s->actor, EV_START);
}

bool poll_memory(uint8_t *addr)
{
    uint32_t *word_addr = (uint32_t *)addr;
    printf("RIO TEST: waiting for client to write %p...\r\n", word_addr);
    if (*word_addr) {
        printf("RIO TEST: detected write from client at %p\r\n", word_addr);
        return true;
    }
    return false;
}

struct t_map_strong_server {
    struct ev_loop *ev_loop;
    struct ev_actor actor;
    struct ev_actor *parent_actor;
    enum {
        T_MAP_STRONG_SVR_ST_INIT,
        T_MAP_STRONG_SVR_ST_WAITING_GRANULE,
        T_MAP_STRONG_SVR_ST_WAITING_BURST,
    } state;

    struct rio_ep *ep;

    uint8_t *buf_addrs[T_MAP_STRONG_NUM_BUFFERS];
    size_t buf_size; /* size of each buffer */
    size_t granules_sz;

    struct sw_timer tmr;
};
static void test_map_strong_server_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_map_strong_server *s =
        container_of(struct t_map_strong_server, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    int sz;

    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_MAP_STRONG_SVR_ST_INIT:
        ASSERT(ev == EV_START);

        for (unsigned i = 0; i < T_MAP_STRONG_NUM_BUFFERS; ++i)
            bzero(s->buf_addrs[i], s->buf_size);

        /* for granule r/w test */
        s->granules_sz = map_write_granules_bus(s->buf_addrs[0], s->buf_size,
                ref_dwords[0]);
        if (!s->granules_sz) break;

        /* for burst r/w (via DMA) test */
        fill_buf(s->buf_addrs[3], s->buf_size, ref_bytes[0]);

        /* Poll waiting for remote end to write data to buffer 1 */
        /* Server waits indefinitely (only client can fail). */
        sw_timer_schedule(&s->tmr, 1000 /* ms */, SW_TIMER_PERIODIC,
                          test_map_strong_on_tick, s);
        s->state = T_MAP_STRONG_SVR_ST_WAITING_GRANULE;
        return;
    case T_MAP_STRONG_SVR_ST_WAITING_GRANULE:
        if (ev == EV_STOP) {
            sw_timer_cancel(&s->tmr);
            break;
        }
        ASSERT(ev == EV_TICK);
        if (!poll_memory(s->buf_addrs[1] + s->granules_sz - sizeof(uint32_t)))
            return; /* keep polling */
        sw_timer_cancel(&s->tmr);

        sz = map_read_granules_bus(s->buf_addrs[1], s->buf_size,
                ref_dwords[1]);
        if (!sz) {
            rc = 1;
            break;
        }
        sz = map_write_granules_bus(s->buf_addrs[2], s->buf_size,
                ref_dwords[2]);
        if (!sz) {
            rc = 1;
            break;
        }
        sw_timer_schedule(&s->tmr, 1000 /* ms */, SW_TIMER_PERIODIC,
                          test_map_strong_on_tick, s);
        s->state = T_MAP_STRONG_SVR_ST_WAITING_BURST;
        return;
    case T_MAP_STRONG_SVR_ST_WAITING_BURST:
        if (ev == EV_STOP) {
            sw_timer_cancel(&s->tmr);
            break;
        }
        ASSERT(ev == EV_TICK);
        if (!poll_memory(s->buf_addrs[4] + s->buf_size - sizeof(uint32_t)))
            return; /* keep polling */
        sw_timer_cancel(&s->tmr);

        rc = map_read_burst_bus(s->buf_addrs[4], s->buf_size, ref_bytes[1]);
        if (rc) break;
        rc = map_write_burst_bus(s->buf_addrs[5], s->buf_size, ref_bytes[2]);
        if (rc) break;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_MAP_STRONG_SVR_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_map_strong_server_start(struct t_map_strong_server *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_ep *ep, uint8_t *buf_out_addr, size_t buf_size)
{
    s->actor.name = "test_map_strong_server";
    s->actor.func = test_map_strong_server_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->state = T_MAP_STRONG_SVR_ST_INIT;

    /* Split the buffer into so that we have disjoint buffers in order
       to not need clear (which we cannot do from the client). */
    s->buf_size = split_buffers(s->buf_addrs, T_MAP_STRONG_NUM_BUFFERS,
                                buf_out_addr, buf_size, __func__);

    post(s->ev_loop, parent, &s->actor, EV_START);
}

static int test_map_weak_granule_client(uint8_t *buf_addr, unsigned buf_size)
{
    int rc = 1;
    int sz;
    printf("RIO: TEST: %s: starting client...\r\n", __func__);

    /* This test uses two buffers, so that we can test write without
       an ability to clear the buffer. */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_addr + 0,
        buf_addr + size,
    };

    /* Read data from buffer filled by server */
    sz = map_read_granules_rio(addrs[0], size, ref_dwords[0]);
    if (!sz) goto exit;

    /* Write then read back data from another buffer */
    sz = map_write_granules_rio(addrs[1], size, ref_dwords[1]);
    if (!sz) goto exit;
    sz = map_read_granules_rio(addrs[1], size, ref_dwords[1]);
    if (!sz) goto exit;

    rc = 0;
exit:
    printf("RIO: TEST: %s: done: rc %d\r\n", __func__, rc);
    return rc;
}
static int test_map_weak_granule_server(
        uint8_t *buf_sys_addr, unsigned buf_size)
{
    int rc = 1;
    int sz;

    printf("RIO TEST: %s: starting server...\r\n", __func__);

    /* This test uses two buffers, but server needs to init only the 1st */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_sys_addr,
        buf_sys_addr + size,
    };

    sz = map_write_granules_bus(addrs[0], size, ref_dwords[0]);
    if (!sz) goto exit;

    rc = 0;
exit:
    printf("RIO: TEST: %s: done: rc %d\r\n", __func__, rc);
    return rc;
}

static int test_map_weak_burst_client(uint8_t *buf_addr, unsigned buf_size,
        struct dma *dmac, unsigned chan)
{
    int rc;
    printf("RIO: TEST: %s: starting...\r\n", __func__);

    /* This test uses two buffers, so that we can test write without
       an ability to clear the buffer. */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_addr + 0,
        buf_addr + size,
    };

    /* Read data from buffer filled by server */
    rc = map_read_burst_rio(addrs[0], size, dmac, chan, ref_bytes[0]);
    if (rc) goto exit;

    /* Write then read back data from another buffer */
    rc = map_write_burst_rio(addrs[1], size, dmac, chan, ref_bytes[1]);
    if (rc) goto exit;
    rc = map_read_burst_rio(addrs[1], size, dmac, chan, ref_bytes[1]);
    if (rc) goto exit;

    rc = 0;
exit:
    printf("RIO: TEST: %s: done: rc %d\r\n", __func__, rc);
    return rc;
}
static int test_map_weak_burst_server(uint8_t *buf_sys_addr, unsigned buf_size)
{
    printf("RIO TEST: %s: starting buf %p sz %u...\r\n", __func__,
           buf_sys_addr, buf_size);

    /* This test uses two buffers, but server needs to init only 1st */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_sys_addr + 0,
        buf_sys_addr + size,
    };

    fill_buf(addrs[0], size, ref_bytes[0]);

    printf("RIO TEST: %s: done:\r\n", __func__);
    return 0;
}

static int test_map_weak_client(uint8_t *buf_addr, size_t buf_size,
        uint32_t *cfg_addr, struct dma *dmac, unsigned chan)
{
    int rc;
    printf("RIO TEST: %s: starting buf %p sz %u cfg %p...\r\n", __func__,
           buf_addr, buf_size, cfg_addr);

    /* This test uses two buffers */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_addr,
        buf_addr + size,
    };

    rc = test_map_weak_granule_client(addrs[0], size);
    if (rc) goto exit;
    rc = test_map_weak_burst_client(addrs[1], size, dmac, chan);
    if (rc) goto exit;

    rc = test_map_rw_cfg_space(cfg_addr);
    if (rc) goto exit;

    rc = 0;
exit:
    printf("RIO TEST: %s: done: rc %d\r\n", __func__, rc);
    return rc;
}
static int test_map_weak_server(uint8_t *buf_sys_addr, size_t buf_size)
{
    int rc;
    printf("RIO TEST: %s: starting server: buf sys %p sz %u...\r\n", __func__,
           buf_sys_addr, buf_size);

    /* This test uses two buffers */
    size_t size = buf_size / 2;
    uint8_t *addrs[] = {
        buf_sys_addr,
        buf_sys_addr + size,
    };

    rc = test_map_weak_granule_server(addrs[0], size);
    if (rc) goto exit;
    rc = test_map_weak_burst_server(addrs[1], size);
    if (rc) goto exit;

    rc = 0;
exit:
    printf("RIO TEST: %s: done: rc %d\r\n", __func__, rc);
    return rc;
}

struct t_map_client {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_MAP_CLIENT_ST_INIT,
        T_MAP_CLIENT_ST_STRONG,
    } state;

    struct rio_ep *ep;
    rio_devid_t ep_server;
    struct rio_map_in_as_cfg *in_region;
    struct dma *dmac;
    unsigned dma_chan;

    struct map_tgt_info out_regions[NUM_OUT_REGIONS];

    struct t_map_strong_client strong_actor;
};
static void test_map_client_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_map_client *s = container_of(struct t_map_client, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_MAP_CLIENT_ST_INIT:
        ASSERT(ev == EV_START);

        map_targets_init(s->out_regions, LEN(s->out_regions), s->ep_server);
        ASSERT(OUT_REGION_TEST_BUF < LEN(s->out_regions));
        ASSERT(OUT_REGION_CFG_SPACE < LEN(s->out_regions));
        struct map_tgt_info *buf_tgt = &s->out_regions[OUT_REGION_TEST_BUF];
        struct map_tgt_info *cfg_tgt = &s->out_regions[OUT_REGION_CFG_SPACE];
        ASSERT(s->in_region);
        ASSERT(!cfg_tgt->bus_window);

        rc = map_setup_out(s->ep,
                s->in_region->addr_width, s->in_region->bus_addr,
                s->out_regions, LEN(s->out_regions));
        if (rc) goto exit;

        /* In map category, b/c requires remote to configure incoming region */
        rc = test_rw_cfg_space(s->ep, s->ep_server);
        if (rc) break;

        /* This test uses two buffers */
        uint8_t *buf_addrs[2];
        size_t buf_size = split_buffers(buf_addrs, LEN(buf_addrs),
                buf_tgt->window_addr, buf_tgt->size, __func__);
        uint32_t *cfg_addr = (uint32_t *)cfg_tgt->window_addr;

        rc = test_map_weak_client(buf_addrs[0], buf_size, cfg_addr,
                s->dmac, s->dma_chan);
        if (rc) break;

        test_map_strong_client_start(&s->strong_actor, s->ev_loop, &s->actor,
                buf_addrs[1], buf_size, s->dmac, s->dma_chan);
        s->state = T_MAP_CLIENT_ST_STRONG;
        return;
    case T_MAP_CLIENT_ST_STRONG:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = ev == EV_DONE ? 0 : 1;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    map_teardown_out(s->ep, s->out_regions, LEN(s->out_regions));
exit:
    LOG_RESULT(rc, s->state);
    s->state = T_MAP_CLIENT_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_map_client_start(struct t_map_client *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_ep *ep, rio_devid_t ep_server,
        struct rio_map_in_as_cfg *in_region,
        struct dma *dmac, unsigned chan)
{
    s->actor.name = "test_map_client";
    s->actor.func = test_map_client_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->ep_server = ep_server;
    s->in_region = in_region;
    s->dmac = dmac;
    s->dma_chan = chan;
    s->state = T_MAP_CLIENT_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

struct t_map_server {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_MAP_SERVER_ST_INIT,
        T_MAP_SERVER_ST_LISTENING,
        T_MAP_SERVER_ST_STOPPING,
    } state;

    struct rio_ep *ep;

    struct t_map_strong_server strong_actor;
};
static void test_map_server_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_map_server *s =
        container_of(struct t_map_server, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);

    const struct map_tgt_info *tgt = &base_map_targets[OUT_REGION_TEST_BUF];
    /* Map buffer into a window that's disjoint with client's window */
    uint8_t *win_addr = (uint8_t *)TEST_OUT_BUF_WIN2_ADDR;

    switch (s->state) {
    case T_MAP_SERVER_ST_INIT:
        ASSERT(ev == EV_START);

        printf("RIO TEST: %s: mapping buf to @0x%x...\r\n", __func__, win_addr);
        rc = rt_mmu_map((uintptr_t)win_addr, tgt->bus_addr, tgt->size);
        if (rc) goto cleanup_map;

        rc = map_setup_in(s->ep);
        if (rc) goto cleanup_setup;

        /* This test uses two buffers */
        uint8_t *buf_addrs[2];
        size_t buf_size = split_buffers(buf_addrs, LEN(buf_addrs),
                win_addr, tgt->size, __func__);

        rc = test_map_weak_server(buf_addrs[0], buf_size);
        if (rc) break;

        test_map_strong_server_start(&s->strong_actor, s->ev_loop, &s->actor,
                s->ep, buf_addrs[1], buf_size);
        s->state = T_MAP_SERVER_ST_LISTENING;
        return;
    case T_MAP_SERVER_ST_LISTENING:
        if (ev == EV_ERROR || ev == EV_DONE) {
            ASSERT(sender == &s->strong_actor.actor);
            rc = ev == EV_DONE ? 0 : 1;
            break;
        }
        ASSERT(ev == EV_STOP);
        post(s->ev_loop, &s->actor, &s->strong_actor.actor, EV_STOP);
        s->state = T_MAP_SERVER_ST_STOPPING;
        return;
    case T_MAP_SERVER_ST_STOPPING:
        ASSERT(ev == EV_DONE);
        ASSERT(sender == &s->strong_actor.actor);
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    map_teardown_in(s->ep);
cleanup_setup:
    rt_mmu_unmap((uintptr_t)win_addr, tgt->size);
cleanup_map:
    LOG_RESULT(rc, s->state);
    s->state = T_MAP_SERVER_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_map_server_start(struct t_map_server *s,
        struct ev_loop *ev_loop, struct ev_actor *parent, struct rio_ep *ep)
{
    s->actor.name = "test_map_server";
    s->actor.func = test_map_server_act;
    s->ev_loop = ev_loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->state = T_MAP_SERVER_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

/* To test the backend, have to talk to another Qemu instance or a standalone
 * device model. Can't test the out-of-process backend using loopback alone
 * (e.g. send from EP0 to EP1 via external path), because loopback out of the
 * switch back into the same switch creates an infinite routing loop (would
 * need to somehow change the routing table in the middle of the test).
 * Alternatively, could make a proxy that rewrites destination ID, which could
 * be a standalone process or within Qemu process (slightly weaker test). */

/* Support two switches on the way to the slave EP (1 local + 1 remote) */
struct rt_entry {
    rio_devid_t dest;
    int port;
};
#define RT_END {0, -1}
#define RT_IS_END(r) (r->port < 0)
#define MAX_ROUTES (2 + 1) /* +1 for invalid entry */

static int route_setup(struct rio_ep *ep, rio_devid_t slave_ep,
        const struct rt_entry (*routes)[MAX_ROUTES], unsigned num_switches)
{
    int rc = 1;
    int hops;
    const struct rt_entry *route;
    rio_dest_t sw_dest;

    for (hops = 0; hops < num_switches; ++hops) {
        sw_dest = rio_sw_dest(slave_ep, hops);
        ASSERT(routes && routes[hops]);
        route = &routes[hops][0];
        while (!RT_IS_END(route)) {
            ASSERT(route->port >= 0);
            printf("RIO TEST: %s: switch (0x%x, hops %u): "
                   "add route 0x%x->%u\r\n", __func__,
                   sw_dest.devid, sw_dest.hops, route->dest, route->port);
            rc = rio_switch_map_remote(ep, sw_dest, route->dest,
                    RIO_MAPPING_TYPE_UNICAST, (1 << route->port));
            if (rc) goto cleanup_current_switch_routes;
            ++route;
        }
    }

    /* At this point, dev ID in our packet won't match the current devid of
     * slave EP, but the EP should process the request regardless (by spec). */
    uint32_t did = 0;
    did = FIELD_DP32(did, B_DEV_ID, BASE_DEVICE_ID, slave_ep);
    did = FIELD_DP32(did, B_DEV_ID, LARGE_BASE_DEVICE_ID, slave_ep);
    printf("RIO TEST: %s: set dev ID of remote EP %u to 0x%x...\r\n",
            __func__, slave_ep, did);
    rc = rio_ep_write_csr32(ep, did, rio_dev_dest(slave_ep), B_DEV_ID);
    if (rc) goto cleanup_routes;

    return 0;
cleanup_current_switch_routes:
    while (--route != &routes[hops][0])
        rio_switch_unmap_remote(ep, sw_dest, route->dest);
cleanup_routes:
    for (--hops; hops >= 0; --hops) {
        sw_dest = rio_sw_dest(slave_ep, hops);
        route = &routes[hops][0];
        while (!RT_IS_END(route)) {
            rio_switch_unmap_remote(ep, sw_dest, route->dest);
            ++route;
        }
    }
    return rc;
}
static int route_teardown(struct rio_ep *ep, rio_devid_t slave_ep,
        const struct rt_entry (*routes)[MAX_ROUTES], unsigned num_switches)
{
    int rc = 0;
    printf("RIO TEST: %s: reset dev ID of remote EP %u to 0x0...\r\n",
            __func__, slave_ep);
    rc |= rio_ep_write_csr32(ep, 0, rio_dev_dest(slave_ep), B_DEV_ID);

    for (int hops = num_switches - 1; hops >= 0; --hops) {
        rio_dest_t sw_dest = rio_sw_dest(slave_ep, hops);
        const struct rt_entry *route = &routes[hops][0];
        while (!RT_IS_END(route)) {
            ASSERT(route->port >= 0);
            printf("RIO TEST: %s: switch (0x%x, hops %u): "
                   "reset route for 0x%x\r\n", __func__,
                   sw_dest.devid, sw_dest.hops, route->dest);
            rc |= rio_switch_map_remote(ep, sw_dest, route->dest,
                    RIO_MAPPING_TYPE_UNICAST, 0);
            ++route;
        }
    }
    return rc;
}

/* Tests between a pair of endpoints without local access to the remote
 * endpoint. */
struct t_remote_client {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    struct rio_ep *ep;
    enum {
        T_REMOTE_CLIENT_ST_INIT,
        T_REMOTE_CLIENT_ST_MSG_RPC_START,
        T_REMOTE_CLIENT_ST_MAP_START,
        T_REMOTE_CLIENT_ST_MSG_RPC,
        T_REMOTE_CLIENT_ST_MAP,
    } state;

    struct rio_switch *sw;
    struct dma *dmac;
    unsigned dma_chan;
    rio_devid_t ep_server;
    const struct rt_entry (*routes)[MAX_ROUTES];
    unsigned num_switches;
    struct rio_map_in_as_cfg *in_region;
    unsigned mbox_server;
    unsigned letter_server;
    unsigned mbox_client;
    unsigned letter_client;
    enum test_rio_tests tests;

    struct t_map_client map_actor;
    struct t_msg_rpc_client msg_rpc_client_actor;
};
static void test_remote_client_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_remote_client *s = container_of(struct t_remote_client, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_REMOTE_CLIENT_ST_INIT:
        rc = route_setup(s->ep, s->ep_server, s->routes, s->num_switches);
        if (rc) goto exit;

        if (s->tests & TEST_RIO_CSR) {
            rc = test_read_csr(s->ep, s->ep_server);
            if (rc) break;
            rc = test_write_csr(s->ep, s->ep_server);
            if (rc) break;
        }

        if (s->tests & TEST_RIO_MSG_RPC) {
            s->state = T_REMOTE_CLIENT_ST_MSG_RPC_START;
            post(s->ev_loop, &s->actor, &s->actor, EV_START);
            return;
        } else if (s->tests & TEST_RIO_MAP) {
            s->state = T_REMOTE_CLIENT_ST_MAP_START;
            post(s->ev_loop, &s->actor, &s->actor, EV_START);
            return;
        }
        break;
    case T_REMOTE_CLIENT_ST_MSG_RPC_START:
        ASSERT(ev == EV_START);
        test_msg_rpc_client_start(&s->msg_rpc_client_actor, s->ev_loop,
                &s->actor, s->sw, s->ep, s->mbox_client, s->letter_client,
                s->ep_server, s->mbox_server, s->letter_server);
        s->state = T_REMOTE_CLIENT_ST_MSG_RPC;
        return;
    case T_REMOTE_CLIENT_ST_MAP_START:
        ASSERT(ev == EV_START);
        test_map_client_start(&s->map_actor, s->ev_loop, &s->actor,
                s->ep, s->ep_server, s->in_region, s->dmac, s->dma_chan);
        s->state = T_REMOTE_CLIENT_ST_MAP;
        return;
    case T_REMOTE_CLIENT_ST_MSG_RPC:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = ev == EV_DONE ? 0 : 1;
        if (rc) break;

        if (s->tests & TEST_RIO_MAP) {
            s->state = T_REMOTE_CLIENT_ST_MAP_START;
            post(s->ev_loop, &s->actor, &s->actor, EV_START);
            return;
        } else {
            break;
        }
    case T_REMOTE_CLIENT_ST_MAP:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = ev == EV_DONE ? 0 : 1;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    rc |= route_teardown(s->ep, s->ep_server, s->routes, s->num_switches);
exit:
    LOG_RESULT(rc, s->state);
    s->state = T_MSG_RPC_SERVER_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_remote_client_start(struct t_remote_client *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_switch *sw, struct rio_ep *ep, rio_devid_t ep_server,
        const struct rt_entry (*routes)[MAX_ROUTES], unsigned num_switches,
        struct rio_map_in_as_cfg *in_region,
        unsigned mbox_server, unsigned letter_server,
        unsigned mbox_client, unsigned letter_client,
        struct dma *dmac, unsigned chan, enum test_rio_tests tests)
{
    s->actor.name = "test_remote_client";
    s->actor.func = test_remote_client_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->sw = sw;
    s->ep = ep;
    s->ep_server = ep_server;
    s->routes = routes;
    s->num_switches = num_switches;
    s->in_region = in_region;
    s->dmac = dmac;
    s->dma_chan = chan;
    s->mbox_server = mbox_server;
    s->letter_server = letter_server;
    s->mbox_client = mbox_client;
    s->letter_client = letter_client;
    s->tests = tests;
    s->state = T_REMOTE_CLIENT_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

struct t_remote_server {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_REMOTE_SERVER_ST_INIT,
        T_REMOTE_SERVER_ST_LISTENING,
        T_REMOTE_SERVER_ST_LISTENING_MSG_RPC, /* only */
        T_REMOTE_SERVER_ST_STOPPING,
        T_REMOTE_SERVER_ST_STOPPING_MSG_RPC,
        T_REMOTE_SERVER_ST_STOPPING_MAP,
    } state;

    struct rio_ep *ep;
    rio_devid_t ep_client;
    unsigned mbox_server;
    unsigned letter_server;
    unsigned mbox_client;
    unsigned letter_client;
    enum test_rio_tests tests;

    int status;
    struct t_msg_rpc_server msg_rpc_actor;
    struct t_map_server map_actor;
};
static void test_remote_server_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_remote_server *s = container_of(struct t_remote_server, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_REMOTE_SERVER_ST_INIT:
        ASSERT(ev == EV_START);

        if (!s->tests)
            break; /* nothing to launch */

        /* For read/write CSR test, nothing is needed except init of the
         * endpoints which is done outside of the test */


        if (s->tests & TEST_RIO_MSG_RPC) {
            test_msg_rpc_server_start(&s->msg_rpc_actor, s->ev_loop,
                    &s->actor, s->ep, s->mbox_server, s->letter_server,
                    s->ep_client, s->mbox_client, s->letter_client);
        }

        if (s->tests & TEST_RIO_MAP) {
            test_map_server_start(&s->map_actor, s->ev_loop, &s->actor, s->ep);
        }

        ASSERT(s->tests); /* at least one launched */
        s->status = 0;
        s->state = T_REMOTE_SERVER_ST_LISTENING;
        return;
    case T_REMOTE_SERVER_ST_LISTENING:
        ASSERT(ev == EV_STOP || ev == EV_ERROR || ev == EV_DONE);
        s->status = ev == EV_ERROR ? 1 : 0;
        if (ev == EV_STOP) {
            post(s->ev_loop, &s->actor, &s->msg_rpc_actor.actor, EV_STOP);
            post(s->ev_loop, &s->actor, &s->map_actor.actor, EV_STOP);
            s->state = T_REMOTE_SERVER_ST_STOPPING;
        } else if (sender == &s->map_actor.actor) {
            if (ev == EV_ERROR) {
                post(s->ev_loop, &s->actor, &s->msg_rpc_actor.actor, EV_STOP);
                s->state = T_REMOTE_SERVER_ST_STOPPING_MSG_RPC;
            } else if (ev == EV_DONE) {
                s->state = T_REMOTE_SERVER_ST_LISTENING_MSG_RPC;
            }
        } else if (ev == EV_ERROR && sender == &s->msg_rpc_actor.actor) {
            post(s->ev_loop, &s->actor, &s->map_actor.actor, EV_STOP);
            s->state = T_REMOTE_SERVER_ST_STOPPING_MAP;
        }
        return;
    case T_REMOTE_SERVER_ST_LISTENING_MSG_RPC:
        if (ev == EV_ERROR || ev == EV_DONE) {
            rc = ev == EV_DONE ? 0 : 1;
            rc |= s->status;
            break;
        }
        ASSERT(ev == EV_STOP);
        post(s->ev_loop, &s->actor, &s->msg_rpc_actor.actor, EV_STOP);
        s->state = T_REMOTE_SERVER_ST_STOPPING_MSG_RPC;
        return;
    case T_REMOTE_SERVER_ST_STOPPING:
        if (ev == EV_STOP)
            return;
        ASSERT(ev == EV_DONE);
        if (sender == &s->map_actor.actor) {
            s->state = T_REMOTE_SERVER_ST_STOPPING_MSG_RPC;
        } else if (sender == &s->msg_rpc_actor.actor) {
            s->state = T_REMOTE_SERVER_ST_STOPPING_MAP;
        } else {
            PANIC("RIO TEST: event from unexpected sender");
        }
        return;
    case T_REMOTE_SERVER_ST_STOPPING_MSG_RPC:
        if (ev == EV_STOP)
            return;
        ASSERT(ev == EV_DONE);
        ASSERT(sender == &s->msg_rpc_actor.actor);
        rc |= s->status;
        break;
    case T_REMOTE_SERVER_ST_STOPPING_MAP:
        if (ev == EV_STOP)
            return;
        ASSERT(ev == EV_DONE);
        ASSERT(sender == &s->map_actor.actor);
        rc |= s->status;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_REMOTE_SERVER_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_remote_server_start(struct t_remote_server *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_switch *sw, struct rio_ep *ep, rio_devid_t ep_client,
        unsigned mbox_server, unsigned letter_server,
        unsigned mbox_client, unsigned letter_client,
        enum test_rio_tests tests)
{
    s->actor.name = "test_remote_server";
    s->actor.func = test_remote_server_act;
    s->state = T_REMOTE_SERVER_ST_INIT;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->ep = ep;
    s->ep_client = ep_client;
    s->mbox_server = mbox_server;
    s->letter_server = letter_server;
    s->mbox_client = mbox_client;
    s->letter_client = letter_client;
    s->tests = tests;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

/* Tests that can run synchronously (without event loop) */
static int test_local(struct rio_switch *sw,
                      struct rio_ep *ep0, struct rio_ep *ep1,
                      struct dma *dmac, unsigned chan)
{
    int rc = 1;

    printf("RIO TEST: %s: start\r\n", __func__);

    /* so that we can ifdef tests out without warnings */
    (void)test_send_receive;
    (void)test_read_csr;
    (void)test_write_csr;
    (void)test_msg;
    (void)test_doorbell;
    (void)test_onchip_map_granule;
    (void)test_onchip_map_burst;
    (void)test_rw_cfg_space;
    (void)test_map_rw_cfg_space;
    (void)test_hop_routing;

    rc = test_send_receive(ep0, ep1);
    if (rc) goto fail;
    rc = test_send_receive(ep1, ep0);
    if (rc) goto fail;

    rc = test_read_csr(ep0, RIO_DEVID_EP1);
    if (rc) goto fail;
    rc = test_read_csr(ep1, RIO_DEVID_EP0);
    if (rc) goto fail;

    rc = test_write_csr(ep0, RIO_DEVID_EP1);
    if (rc) goto fail;
    rc = test_write_csr(ep1, RIO_DEVID_EP0);
    if (rc) goto fail;

    /* TODO: flipped test? */
    rc = test_msg(ep0, ep1);
    if (rc) goto fail;
    rc = test_doorbell(ep0, ep1);
    if (rc) goto fail;

    /* TODO: flipped test? */
    rc = test_onchip_map(ep0, ep1, dmac, chan);
    if (rc) goto fail;

    /* TODO: this save/restore is not great b/c the setting is in rio-svc.c */
    /* Before hop routing test: reset switch routing table */
    rio_switch_unmap_local(sw, RIO_DEVID_EP0);
    rio_switch_unmap_local(sw, RIO_DEVID_EP1);

    rc = test_hop_routing(ep0, ep1);
    if (rc) goto fail;

    /* After hop routing test: restore switch routing table */
    rio_switch_map_local(sw, RIO_DEVID_EP0, RIO_MAPPING_TYPE_UNICAST,
                         (1 << RIO_EP0_SWITCH_PORT));
    rio_switch_map_local(sw, RIO_DEVID_EP1, RIO_MAPPING_TYPE_UNICAST,
                         (1 << RIO_EP1_SWITCH_PORT));

    rc = 0;
fail:
    printf("RIO TEST: %s: %s\r\n", __func__,
           rc ? "SOME FAILED!" : "ALL PASSED");
    return rc;
}

struct t_rio_master {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_RIO_MASTER_ST_INIT,
        T_RIO_MASTER_ST_ONCHIP_MSG_RPC_EP0_EP1,
        T_RIO_MASTER_ST_ONCHIP_MSG_RPC_EP1_EP0,
        T_RIO_MASTER_ST_ONCHIP_REMOTE,
        T_RIO_MASTER_ST_ONCHIP_REMOTE_STOPPING,
        T_RIO_MASTER_ST_ONCHIP_REMOTE_SERVER_DONE,
        T_RIO_MASTER_ST_OFFCHIP,
    } state;

    struct rio_switch *sw;
    struct rio_ep *ep0;
    struct rio_ep *ep1;
    struct dma *dmac;
    unsigned dma_chan;
    bool master;

    enum test_rio_tests onchip_tests;
    enum test_rio_tests offchip_tests;

    int status;
    struct t_remote_server remote_server_actor;
    struct t_remote_client remote_client_actor;
};
static void test_rio_master_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_rio_master *s = container_of(struct t_rio_master, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_RIO_MASTER_ST_INIT:
        ASSERT(ev == EV_START);

        /* Only master can run the local test because we are testing against
           RIO service, which sets up routing table only in master mode. If we
           change/add a fully standalone test, then both master and non-masters
           would be able to run the local test. */
        if (s->onchip_tests & TEST_RIO_ONEWAY) {
            rc = test_local(s->sw, s->ep0, s->ep1, s->dmac, s->dma_chan);
            if (rc) break;
        }

        test_remote_server_start(&s->remote_server_actor, s->ev_loop, &s->actor,
                s->sw, s->ep1, RIO_DEVID_EP0,
                MBOX_SERVER, LETTER_SERVER, MBOX_CLIENT, LETTER_CLIENT,
                s->onchip_tests);

        /* Routes assumed to be setup by same entity that inits the driver */
        test_remote_client_start(&s->remote_client_actor, s->ev_loop, &s->actor,
                s->sw, s->ep0, RIO_DEVID_EP1, /* routes */ NULL, 0,
                &in_as_cfgs[IN_ADDR_SPACE_FROM_EP0],
                MBOX_SERVER, LETTER_SERVER, MBOX_CLIENT, LETTER_CLIENT,
                s->dmac, s->dma_chan, s->onchip_tests);

        s->status = 0;
        s->state = T_RIO_MASTER_ST_ONCHIP_REMOTE;
        return;
    case T_RIO_MASTER_ST_ONCHIP_REMOTE:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        s->status = ev == EV_DONE ? 0 : 1;
        if (sender == &s->remote_client_actor.actor) {
            post(s->ev_loop, &s->actor, &s->remote_server_actor.actor, EV_STOP);
            s->state = T_RIO_MASTER_ST_ONCHIP_REMOTE_STOPPING;
        } else if (sender == &s->remote_server_actor.actor) {
            s->state = T_RIO_MASTER_ST_ONCHIP_REMOTE_SERVER_DONE;
            /* wait for the client to be done */
        } else {
            PANIC("event from unexpected sender");
        }
        return;
    case T_RIO_MASTER_ST_ONCHIP_REMOTE_SERVER_DONE:
        ASSERT(sender == &s->remote_client_actor.actor);
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = s->status | (ev == EV_DONE ? 0 : 1);
        break;
    case T_RIO_MASTER_ST_ONCHIP_REMOTE_STOPPING:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = s->status | (ev == EV_DONE ? 0 : 1);
        if (rc) break;

        /* TODO: flipped remote test: EP0->EP1 */

        if (!s->offchip_tests)
            break; /* finish */

        /* [hops to switch] -> {(dest dev id -> port),...} */
        static const struct rt_entry offchip_routes[][MAX_ROUTES] = {
            [0] = {
                /* {RIO_DEVID_EP0, RIO_EP0_SWITCH_PORT}, -- assumed to exist */
                {RIO_DEVID_EP_EXT, RIO_EP_EXT_SWITCH_PORT},
                RT_END
            },
            [1] = {
                {RIO_DEVID_EP_EXT, RIO_EP0_SWITCH_PORT},
                {RIO_DEVID_EP0,    RIO_EP_EXT_SWITCH_PORT},
                RT_END
            },
        };
        test_remote_client_start(&s->remote_client_actor, s->ev_loop, &s->actor,
                s->sw, s->ep0,
                RIO_DEVID_EP_EXT, offchip_routes, LEN(offchip_routes),
                &in_as_cfgs[IN_ADDR_SPACE_FROM_EP0],
                MBOX_SERVER, LETTER_SERVER, MBOX_CLIENT, LETTER_CLIENT,
                s->dmac, s->dma_chan, s->offchip_tests);
        s->state = T_RIO_MASTER_ST_OFFCHIP;
        return;
    case T_RIO_MASTER_ST_OFFCHIP:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = ev == EV_DONE ? 0 : 1;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_RIO_MASTER_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_rio_master_start(struct t_rio_master *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_switch *sw, struct rio_ep *ep0, struct rio_ep *ep1,
        struct dma *dmac, unsigned chan,
        enum test_rio_tests onchip_tests, enum test_rio_tests offchip_tests)
{
    s->actor.name = "test_rio_master";
    s->actor.func = test_rio_master_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->sw = sw;
    s->ep0 = ep0;
    s->ep1 = ep1;
    s->dmac = dmac;
    s->dma_chan = chan;
    s->onchip_tests = onchip_tests;
    s->offchip_tests = offchip_tests;
    s->state = T_RIO_MASTER_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

struct t_rio_slave {
    struct ev_actor actor;
    struct ev_loop *ev_loop;
    struct ev_actor *parent_actor;
    enum {
        T_RIO_SLAVE_ST_INIT,
        T_RIO_SLAVE_ST_LISTENING,
        T_RIO_SLAVE_ST_STOPPING,
    } state;

    struct rio_switch *sw;
    struct rio_ep *ep;
    enum test_rio_tests tests;

    struct t_remote_server remote_server_actor;
};
static void test_rio_slave_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_rio_slave *s = container_of(struct t_rio_slave, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;
    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    switch (s->state) {
    case T_RIO_SLAVE_ST_INIT:
        ASSERT(ev == EV_START);

        test_remote_server_start(&s->remote_server_actor, s->ev_loop, &s->actor,
                s->sw, s->ep, RIO_DEVID_EP0,
                MBOX_SERVER, LETTER_SERVER, MBOX_CLIENT, LETTER_CLIENT,
                s->tests);
        s->state = T_RIO_SLAVE_ST_LISTENING;
        return;
    case T_RIO_SLAVE_ST_LISTENING:
        ASSERT(ev == EV_STOP);
        printf("RIO TEST: %s: stopping...\r\n", __func__);
        post(s->ev_loop, &s->actor, &s->remote_server_actor.actor, EV_STOP);
        s->state = T_RIO_SLAVE_ST_STOPPING;
        return;
    case T_RIO_SLAVE_ST_STOPPING:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        ASSERT(sender == &s->remote_server_actor.actor);
        rc = ev == EV_DONE ? 0 : 1;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_RIO_SLAVE_ST_INIT;
    post(s->ev_loop, &s->actor, s->parent_actor, finish_ev(rc));
}
static void test_rio_slave_start(struct t_rio_slave *s,
        struct ev_loop *loop, struct ev_actor *parent,
        struct rio_switch *sw, struct rio_ep *ep, enum test_rio_tests tests)
{
    s->actor.name = "test_rio_slave";
    s->actor.func = test_rio_slave_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->sw = sw;
    s->ep = ep;
    s->tests = tests;
    s->state = T_RIO_SLAVE_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

struct t_rio {
    struct ev_loop *ev_loop;
    struct ev_actor actor;
    struct ev_actor *parent_actor;
    enum {
        T_RIO_ST_INIT,
        T_RIO_ST_MST_RUNNING,
        T_RIO_ST_SLV_LISTENING,
        T_RIO_ST_SLV_STOPPING,
    } state;

    struct rio_switch *sw;
    struct rio_ep *ep0;
    struct rio_ep *ep1;
    struct dma *dmac;
    unsigned dma_chan;

    bool master;
    enum test_rio_tests onchip_tests;
    enum test_rio_tests offchip_tests;

    struct t_rio_master master_actor;
    struct t_rio_slave slave_actor;
};
static void test_rio_act(struct ev_actor *a,
        struct ev_actor *sender, void *event)
{
    struct t_rio *s = container_of(struct t_rio, actor, a);
    enum t_event ev = (enum t_event)event;
    int rc = 0;

    ASSERT(s);
    LOG_EVENT(s->state, sender, ev);
    printf("RIO_TEST: %s: master %u tests onchip 0x%x offchip 0x%x\r\n",
            __func__, s->master, s->onchip_tests, s->offchip_tests);
    switch (s->state) {
    case T_RIO_ST_INIT:
        ASSERT(ev == EV_START);
        if (s->master) {
            test_rio_master_start(&s->master_actor, s->ev_loop, &s->actor,
                    s->sw, s->ep0, s->ep1, s->dmac, s->dma_chan,
                    s->onchip_tests, s->offchip_tests);
            s->state = T_RIO_ST_MST_RUNNING;
        } else if (s->offchip_tests) {
            test_rio_slave_start(&s->slave_actor, s->ev_loop, &s->actor,
                    s->sw, s->ep0, s->offchip_tests);
            s->state = T_RIO_ST_SLV_LISTENING;
        }
        return;
    case T_RIO_ST_SLV_LISTENING:
        ASSERT(ev == EV_STOP);
        printf("RIO TEST: %s: stopping...\r\n", __func__);
        post(s->ev_loop, &s->actor, &s->slave_actor.actor, EV_STOP);
        s->state = T_RIO_ST_SLV_STOPPING;
        return;
    case T_RIO_ST_SLV_STOPPING:
    case T_RIO_ST_MST_RUNNING:
        ASSERT(ev == EV_DONE || ev == EV_ERROR);
        rc = ev == EV_DONE ? 0 : 1;
        break;
    default:
        INVALID_STATE_PANIC(s->state);
    }
    LOG_RESULT(rc, s->state);
    s->state = T_RIO_ST_INIT;

    /* This is top-level actor, so nowhere to propagate error, handle here.
       For now, simply PANIC, if desired can generate a "test report" for main
       loop (or for an actor defined in main.c) to process. */
    if (rc)
        PANIC("RIO TEST: failed");
}
static void test_rio_start(struct t_rio *s,
        struct ev_loop *loop, struct ev_actor *parent, bool master,
        struct rio_switch *sw, struct rio_ep *ep0, struct rio_ep *ep1,
        struct dma *dmac, unsigned chan,
        enum test_rio_tests onchip_tests, enum test_rio_tests offchip_tests)
{
    s->actor.name = "test_rio";
    s->actor.func = test_rio_act;
    s->ev_loop = loop;
    s->parent_actor = parent;
    s->sw = sw;
    s->ep0 = ep0;
    s->ep1 = ep1;
    s->dmac = dmac;
    s->dma_chan = chan;
    s->master = master;
    s->onchip_tests = onchip_tests;
    s->offchip_tests = offchip_tests;
    s->state = T_RIO_ST_INIT;

    post(s->ev_loop, parent, &s->actor, EV_START);
}

void test_rio_launch(struct ev_loop *ev_loop, bool master,
        struct rio_switch *sw, struct rio_ep *ep0, struct rio_ep *ep1,
        struct dma *dmac, unsigned chan,
        enum test_rio_tests onchip_tests, enum test_rio_tests offchip_tests)
{
    /* Allocate in scope of this module. We could expose test_rio_start
     * instead, and thus grant the caller the choice of how to allocate memory
     * for the state, but then, we would need to expose all actor structs in
     * the header (for size information). We choose encapsulation. */
    static struct t_rio s;

    test_rio_start(&s, ev_loop, /* parent */ NULL, master,
            sw, ep0, ep1, dmac, chan, onchip_tests, offchip_tests);
    printf("RIO TEST: %s: launched: master %u\r\n", __func__, master);

    /* Each actor cleans up after itself when it finishes its work. However, in
     * case of actors on the slave (that processes requests from master), clean
     * up is not fully implemented -- the cleanup code on stop event exists,
     * but the stop event is never generated, because we don't have
     * notifications when a server processed its respective test request(s). It
     * is in theory possible to implement, just need notification from lower
     * level, e.g. have the link somehow notify that a ping request was
     * processed, but now the lower level is opaque (initialize and forget). */
}
