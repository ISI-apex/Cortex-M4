#ifndef RIO_PKT_H
#define RIO_PKT_H

#include <stdint.h>

#define RIO_MAX_PAYLOAD_SIZE 256 /* TODO: Spec reference */

/* Packet field widths (note: some are programatically determined)
 * Note: we do not use the register field helper API: FIELD() for declaring and
 * FIELD_[EX|DP]*() for accessing, because some fields are variable length
 * (multiple possible fixed widths). Instead we define our own API for
 * packing/unpacking fields sequentially: field_push()/field_pop().*/
enum { /* enum for the debugger to have the literals */
    PKT_FIELD_PRIO             =  2,
    PKT_FIELD_TTYPE            =  2,
    PKT_FIELD_FTYPE            =  4,
    PKT_FIELD_TRANSACTION      =  4,
    PKT_FIELD_RDWRSIZE         =  4,
    PKT_FIELD_TARGET_TID       =  8,
    PKT_FIELD_SRC_TID          =  8,
    PKT_FIELD_HOP_COUNT        =  8,
    PKT_FIELD_ADDR             = 29,
    PKT_FIELD_CONFIG_OFFSET    = 21,
    PKT_FIELD_WDPTR            =  1,
    PKT_FIELD_XAMSBS           =  2,
    PKT_FIELD_STATUS           =  4,
    PKT_FIELD_MSG_LEN          =  4,
    PKT_FIELD_SEG_SIZE         =  4,
    PKT_FIELD_LETTER           =  2,
    PKT_FIELD_MBOX             =  2,
    PKT_FIELD_MSGSEG_XMBOX     =  4,
    PKT_FIELD_INFO             = 16,
};

/* Max device ID in Spec is 32-bit wide */
typedef uint32_t rio_devid_t;

/* Destination device (endpoint or switch) of a RapidIO packet
 * The hops field needs to be set to reach a switch (has no devid). If hops >
 * 0, then devid still needs to be set so that the preceded switches know where
 * to route the packet. See Spec Part 3 Section 2.5 */
typedef struct {
    rio_devid_t devid;
    unsigned hops;
} rio_dest_t;

static inline rio_dest_t rio_dev_dest(rio_devid_t devid)
{
    rio_dest_t dest = { .devid = devid, .hops = 0xFF };
    return dest;
}
static inline rio_dest_t rio_sw_dest(rio_devid_t devid, unsigned hops)
{
    rio_dest_t dest = { .devid = devid, .hops = hops };
    return dest;
}

/* Spec Part 3 Section 2.4 Table 2-1 and User Guide Table 120 (assumed equal) */
enum rio_transport_type {
    RIO_TRANSPORT_DEV8  = 0b00,
    RIO_TRANSPORT_DEV16 = 0b01,
    RIO_TRANSPORT_DEV32 = 0b10,
};

/* Tabel 4-1 in RIO Spec P1 */
enum rio_ftype {
    RIO_FTYPE_READ          =  2,
    RIO_FTYPE_WRITE         =  5,
    RIO_FTYPE_STREAM_WRITE  =  6,
    RIO_FTYPE_MAINT         =  8,
    RIO_FTYPE_DOORBELL      = 10,
    RIO_FTYPE_MSG           = 11,
    RIO_FTYPE_RESP          = 13,
};

enum rio_transaction {
    RIO_TRANS_READ_NREAD            = 0b0100,
    RIO_TRANS_WRITE_NWRITE          = 0b0100,
    RIO_TRANS_WRITE_NWRITE_R        = 0b0101,
    RIO_TRANS_MAINT_REQ_READ        = 0b0000,
    RIO_TRANS_MAINT_REQ_WRITE       = 0b0001,
    RIO_TRANS_MAINT_RESP_READ       = 0b0010,
    RIO_TRANS_MAINT_RESP_WRITE      = 0b0011,
    RIO_TRANS_MAINT_REQ_PORT_WRITE  = 0b0100,
    RIO_TRANS_RESP_WITHOUT_PAYLOAD  = 0b0000,
    RIO_TRANS_RESP_WITH_PAYLOAD     = 0b1000,
    RIO_TRANS_RESP_MSG              = 0b0001,
};

/* Table 4-7 in Spec */
enum rio_status {
    RIO_STATUS_DONE     = 0x0000,
    RIO_STATUS_ERROR    = 0b0111,
};

/* Struct sufficiently general to hold a packet of any type */
/* TODO: consider splitting into physical, transport, logical, and msg? */
typedef struct RioPkt {
    /* Physical Layer */
    uint8_t prio;

    /* Transport Layer */
    enum rio_transport_type ttype;
    rio_devid_t dest_id;
    rio_devid_t src_id;

    /* Logical Layer */
    enum rio_ftype ftype;
    uint8_t src_tid; /* TODO: unify into one tid field */
    uint8_t target_tid;

    /* TODO: Union these fields to overlap msg fields */
    enum rio_transaction transaction;

    /* Ideally, we would just store a byte address and a byte length, but we
     * need to support 66-bit byte addresses, but we want to use uint64_t for
     * efficiency, so we do double-word address + byte offset. */
    uint64_t dw_addr; /* address of a dword (right-shifted byte address) */
    uint8_t dw_offset; /* index of byte within dword pointed to by dw_addr */
    uint16_t rdwr_bytes; /* number of bytes to read/write at dw_offset */
    unsigned addr_width; /* bits in byte address obtained frmo dw_addr */
    uint8_t status;
    uint8_t hop_count;

    unsigned payload_len; /* double-words */
    uint64_t payload[RIO_MAX_PAYLOAD_SIZE];

    /* Messaging Layer */
    uint8_t msg_len;
    uint8_t seg_size;
    uint8_t mbox;
    uint8_t letter;
    uint8_t msg_seg;
    uint16_t info; /* don't use payload[] b/c payload is DW granularity */

    /* Implementation metadata */
    union {
        uint64_t send_time;
        uint64_t rcv_time;
    } timestamp;
} RioPkt;

void pack_header(uint8_t *buf, unsigned size, unsigned *pos,
                 const RioPkt *pkt);
int unpack_header(RioPkt *pkt, uint8_t *buf, unsigned size,
                             unsigned *pos);
void pack_body(uint8_t *buf, unsigned size, unsigned *pos, const RioPkt *pkt);
int unpack_body(RioPkt *pkt, uint8_t *buf, unsigned size, unsigned *pos);
unsigned pack_pkt(uint8_t *buf, unsigned size, const RioPkt *pkt);
int unpack_pkt(RioPkt *pkt, uint8_t *buf, unsigned size);

#define MSG_SEG_SIZE_INVALID 0
#define MSG_SEG_FIELD_INVALID 0xff
uint16_t msg_seg_size(uint8_t seg_size_field);
uint8_t msg_seg_size_field(uint8_t seg_size);

#endif /* RIO_PKT_H */
