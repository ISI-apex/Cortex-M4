#ifndef RIO_H
#define RIO_H

#include <stdint.h>

#include "regops.h"
#include "bit.h"

#include "rio-pkt.h"

#define RIO_CSR_REGION_SIZE    0x001000000 /* 16 MB, from RIO Spec */

#define NUM_MBOXES 4
#define NUM_LETTERS 4

#define RIO_MAX_PAYLOAD_SIZE 256 /* TODO: check */
#define RIO_MAX_PKT_SIZE 64 /* TODO */
#define MAX_MSG_SIZE 256 /* TODO */
#define MAX_MSG_SEGMENTS 16 /* Spec 2.3.1 */
#define MAX_MSGSEG_XMBOX_SIZE 256 /* keep in sync with msg_seg_sizes list in rio.c */
#define MAX_MSG_DESC_SIZE 32 /* keep in sync with msg_desc_sizes in rio.c */

#define RIO_NUM_OUT_REGIONS         64 /* User Guide Table 119 */
#define RIO_NUM_IN_ADDR_SPACES      64 /* User Guide Table 120 */

#define RX_MSG_FIFO_SIZE 4
#define NUM_MSG_RX_CHAINS 2
#define MSG_CHAIN_BUF_SIZE \
    ((MAX_MSG_DESC_SIZE + MAX_MSGSEG_XMBOX_SIZE) * MAX_MSG_SEGMENTS)

/* When we want to use a single variable (so, uint64_t) to hold a 66-bit byte
 * addresses that we know is aligned to a double-word, these are useful. */
#define BYTE_ADDR_TO_DW_ADDR(addr) ((addr) >> 3)
#define DW_ADDR_TO_BYTE_ADDR(addr) ((addr) << 3)

/* CSRs defined by the RapidIO spec (implementation-independent) */

/* The Spec (and the Praesum datasheet) indexes the MSB as Bit 0 (see User
 * Guide 1.1.1), whereas in context of shifts in C, Bit 0 is LSB. However, the
 * significance of bits in a bitfield is the same in both cases: a bitfield
 * starts at a less signficant bit and ends in a more significat bit. This
 * doesn't have anything to do with endianness, and there's no conversion, no
 * flipping being done here. There is no abiguity about where the bitfields are
 * in register words (unambigously defined by LSB/MSB). The question is just
 * cosmetically how we specify that unabiguous location in the following field
 * declaration. We could ignore the indexing convention in the datasheet
 * altogether, however then the literal values used in the datashet vs in these
 * field definitions would not be easily cross-checkable, at a glance.  So, we
 * adopt a pattern here such that the 'start' bit index value (and width value)
 * matches the tables in the datasheet verbatim:
 *      32 -  start - width, width
 * For example, for bitfield in Table 120 "Priority":
 *    Datasheet:  Bits 2-3
 *    Field def:  32 - 2 - 2, 2
 *                     ^ this value is the same verbatim
 */

REG32(DEV_ID,      0x000000)
    FIELD(DEV_ID, DEVICE_IDENTITY,              32 -  0 - 16, 16)
    FIELD(DEV_ID, DEVICE_VENDOR_IDENTITY,       32 - 16 - 16, 16)

REG32(B_DEV_ID,      0x000060)
    FIELD(B_DEV_ID, BASE_DEVICE_ID,             32 -  8 -  8,  8)
    FIELD(B_DEV_ID, LARGE_BASE_DEVICE_ID,       32 - 16 - 16, 16)

REG32(LCS_BA0,      0x000058)
REG32(LCS_BA1,      0x00005c)

REG32(COMP_TAG,      0x00006c)

/* Address in memory as accessed by the Rapid IO endpoints
 * (same type for both local and remote endpoints). We support
 * systems with up to 64-bit bus width, while RapidIO packets support
 * address field of up to 66 bits, if a 66-bit address is sent
 * in a RapidIO request NREAD/NWRITE packet that lands in an incoming
 * mapped region, the upper 2 bits of the address will be truncated. */
typedef uint64_t rio_bus_addr_t;

/* Byte address on RIO interconnect (up to 66-bit). */
/* Sacrifices perf for legibility (could work with double-word addresses
 * (stored in uint64_t), but keeping all addresses in the interface as
 * byte-addresses is a lot more intuitive and less error-prone. In particular,
 * in methods that r/w values that may be of length 1 to N bytes, it's a
 * lot simpler to accept a byte address instead of a double-word address
 * plus offset. */
typedef struct { uint8_t hi; uint64_t lo; } rio_addr_t;

static inline rio_addr_t rio_addr_from_u72(uint8_t hi, uint64_t lo)
{
    rio_addr_t rio_addr = { hi, lo };
    return rio_addr;
}
static inline rio_addr_t rio_addr_from_u64(uint64_t lo)
{
    return rio_addr_from_u72(0, lo);
}
static inline rio_addr_t rio_addr_from_dw_addr(uint64_t dw_addr)
{
    return rio_addr_from_u72((dw_addr >> (64 - 3)), dw_addr << 3);
}
static inline uint64_t rio_addr_to_dw_addr(rio_addr_t rio_addr)
{
    return (((uint64_t)rio_addr.hi) << (64 - 3)) | (rio_addr.lo >> 3);
}
static inline bool rio_addr_aligned(rio_addr_t rio_addr, unsigned bits)
{
    if (bits <= 32) {
        return ALIGNED64(rio_addr.lo, bits);
    } else { /* out_region_size_bits > 32 */
        return ALIGNED64(rio_addr.hi, bits - 32) && rio_addr.lo == 0;
    }
}
static inline int rio_addr_cmp(rio_addr_t a, rio_addr_t b)
{
    if (a.hi == b.hi) {
        if (a.lo == b.lo)
            return 0;
        else
            return a.lo < b.lo ? -1 : 1;
    } else {
        return a.hi < b.hi ? -1 : 1;
    }
}
static inline rio_addr_t rio_addr_add64(rio_addr_t addr, uint64_t val)
{
    if (addr.lo + val < addr.lo) { /* if would overflow */
        rio_addr_t r = { addr.hi + 1, addr.lo + val };
        return r;
    } else {
        rio_addr_t r = { addr.hi, addr.lo };
        return r;
    }
}

typedef void (msg_cb_t)(void *opaque);

/* TODO: prefix name */
struct rx_msg {
    uint8_t payload[MAX_MSG_SIZE];
    unsigned len; /* bytes */
    uint32_t segments; /* assert(width >= MAX_MSG_SEGMENTS) */
    rio_devid_t src_id;
    uint64_t rcv_time;
};

struct rx_msg_fifo {
    unsigned head;
    unsigned tail;
    struct rx_msg q[RX_MSG_FIFO_SIZE];
};

struct rio_ep {
    const char *name;
    uintptr_t base;
    rio_bus_addr_t out_as_base;
    unsigned out_as_width;
    enum rio_transport_type ttype;
    unsigned addr_width;

    msg_cb_t *msg_tx_cb;
    void *msg_tx_cb_arg;
    msg_cb_t *msg_rx_cb;
    void *msg_rx_cb_arg;

    rio_bus_addr_t buf_mem_ep;
    uint8_t *buf_mem_cpu;
    unsigned buf_mem_size;

    uint8_t (*msg_tx_desc_buf);
    unsigned msg_tx_desc_buf_size;

    uint8_t (*msg_rx_desc_buf)[MSG_CHAIN_BUF_SIZE];
    unsigned msg_rx_desc_buf_size;

    rio_devid_t devid;
    uint8_t *msg_rx_desc_addr;
    unsigned rx_chain;

    unsigned mapped_out_regions;
    unsigned mapped_in_addr_spaces;

    uint8_t pkt_buf[RIO_MAX_PKT_SIZE];


    /* A FIFO for received messages, one FIFO per mbox, letter endpoint.
       Only one in-flight (i.e. incomplete) message per mbox/letter is supported.
       But, we support a FIFO between driver and consumer, where *completed*
       messages are queued. The in-flight message is assembled in the "current"
       slot in this FIFO. */
    struct rx_msg_fifo rx_msgs[NUM_MBOXES][NUM_LETTERS];

    struct RioPkt in_pkt;
    struct RioPkt out_pkt;
};

enum rio_addr_width {
    RIO_ADDR_WIDTH_34_BIT = 34,
    RIO_ADDR_WIDTH_50_BIT = 50,
    RIO_ADDR_WIDTH_66_BIT = 66,
};

struct rio_map_in_as_cfg {
    rio_devid_t src_id;
    rio_devid_t src_id_mask;
    enum rio_addr_width addr_width;
    rio_bus_addr_t bus_addr;
};

struct rio_map_out_region_cfg {
    enum rio_transport_type ttype;
    rio_devid_t dest_id;
    enum rio_addr_width addr_width;
    rio_addr_t rio_addr;
};

void rio_print_pkt(struct RioPkt *pkt);

int rio_ep_init(struct rio_ep *ep, const char *name, uintptr_t base,
      rio_bus_addr_t out_as_base, unsigned out_as_width,
      enum rio_transport_type ttype, enum rio_addr_width addr_width,
      rio_bus_addr_t buf_mem_ep, uint8_t *buf_mem_cpu, unsigned buf_mem_size);
void rio_ep_release(struct rio_ep *ep);

const char *rio_ep_name(struct rio_ep *ep);

void rio_ep_set_devid(struct rio_ep *ep, rio_devid_t devid);
rio_devid_t rio_ep_get_devid(struct rio_ep *ep);

int rio_ep_sp_send(struct rio_ep *ep, struct RioPkt *pkt);
int rio_ep_sp_recv(struct rio_ep *ep, struct RioPkt *pkt);

int rio_ep_read_csr(struct rio_ep *ep, uint8_t *data, unsigned len,
                    rio_dest_t dest, uint32_t offset);
int rio_ep_write_csr(struct rio_ep *ep, const uint8_t *data, unsigned len,
                     rio_dest_t dest, uint32_t offset);
int rio_ep_read_csr32(struct rio_ep *ep, uint32_t *data,
                       rio_dest_t dest, uint32_t offset);
int rio_ep_write_csr32(struct rio_ep *ep, uint32_t data,
                        rio_dest_t dest, uint32_t offset);

void rio_ep_sub_msg_tx(struct rio_ep *ep, msg_cb_t *cb, void *arg);
void rio_ep_unsub_msg_tx(struct rio_ep *ep);
void rio_ep_sub_msg_rx(struct rio_ep *ep, msg_cb_t *cb, void *arg);
void rio_ep_unsub_msg_rx(struct rio_ep *ep);
int rio_ep_msg_send(struct rio_ep *ep, rio_devid_t dest,
                    uint8_t mbox, uint8_t letter, uint8_t seg_size,
                    uint8_t *payload, unsigned len);
int rio_ep_msg_recv(struct rio_ep *ep, uint8_t mbox, uint8_t letter,
                    rio_devid_t *src, uint64_t *rcv_time,
                    uint8_t *payload, unsigned *size);

/* Doorbell send API is async (send+reap pair), because doorbells are
 * handled (i.e. responded to) by software not HW, so to support a local
 * test, we need to let the software send, regain control flow, receive and
 * respond, and then reap. Note: only one outstanding request is supported,
 * so no need for send to return a handle. */
int rio_ep_doorbell_send_async(struct rio_ep *ep, rio_devid_t dest,
                               uint16_t info);
int rio_ep_doorbell_reap(struct rio_ep *ep, enum rio_status *status);
int rio_ep_doorbell_recv(struct rio_ep *ep, uint16_t *info);

int rio_ep_map_in(struct rio_ep *ep, unsigned num_addr_spaces,
                  struct rio_map_in_as_cfg *cfgs);
void rio_ep_unmap_in(struct rio_ep *ep);
int rio_ep_map_out(struct rio_ep *ep, unsigned num_regions,
                   struct rio_map_out_region_cfg *cfgs);
void rio_ep_unmap_out(struct rio_ep *ep);

uint64_t rio_ep_get_outgoing_base(struct rio_ep *ep, unsigned num_regions,
                                  unsigned region);
uint64_t rio_ep_get_outgoing_size_bits(struct rio_ep *ep, unsigned num_regions);

rio_addr_t rio_ep_get_cfg_base(struct rio_ep *ep);
void rio_ep_set_cfg_base(struct rio_ep *ep, rio_addr_t addr);

/* Read and write using NREAD/NWRITE_R requests */
int rio_ep_read(struct rio_ep *ep, uint8_t *buf, unsigned len,
                rio_addr_t addr, rio_devid_t dest);
int rio_ep_write(struct rio_ep *ep, uint8_t *data, unsigned len,
                 rio_addr_t addr, rio_devid_t dest);
int rio_ep_read32(struct rio_ep *ep, uint32_t *data,
                  rio_addr_t addr, rio_devid_t dest);
int rio_ep_write32(struct rio_ep *ep, uint32_t data,
                   rio_addr_t addr, rio_devid_t dest);

void rio_ep_msg_rx_isr(struct rio_ep *ep);
void rio_ep_msg_tx_isr(struct rio_ep *ep);

#endif // RIO_H
