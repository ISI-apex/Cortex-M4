/* Functions for packing/unpacking RapidIO packets to/from struct/buffer.
 * This code is nearly verbatim from Qemu model, which needs same functionality.
 */

#include "rio-pkt.h"

#include <stdbool.h>

#include "bit.h"
#include "printf.h"
#include "panic.h"
#include "mem.h"

/* To share code with Qemu model with as few modifications as possible. */
#define assert(x) ASSERT(x);
#define abort() ASSERT(false);
#define qemu_log printf

typedef enum AccessType {
    ACCESS_READ,
    ACCESS_WRITE,
} AccessType;

static uint8_t dev_id_width_table[] = {
    [RIO_TRANSPORT_DEV8]    =  8,
    [RIO_TRANSPORT_DEV16]   = 16,
    [RIO_TRANSPORT_DEV32]   = 32,
};
#define DEV_ID_WIDTH_TABLE_ENTRIES \
    (sizeof(dev_id_width_table) / sizeof(dev_id_width_table[0]))

static uint8_t dev_id_width(enum rio_transport_type ttype)
{
    assert(ttype < DEV_ID_WIDTH_TABLE_ENTRIES);
    return dev_id_width_table[ttype];
}

/* Table 4-4 (ssize field) in Spec (in bytes) */
/* TODO: move to device independent code? */
#define MSG_SEG_SIZE(ssize_field) ((ssize_field) - 0b1001) /* to save space */
#define MSG_SEG_SIZE_FIELD(seg_size) (((seg_size) + 1) | 0b1000)
static const uint16_t msg_seg_sizes[] = {
    [MSG_SEG_SIZE(0b1001)]  =   8,
    [MSG_SEG_SIZE(0b1010)]  =  16,
    [MSG_SEG_SIZE(0b1011)]  =  32,
    [MSG_SEG_SIZE(0b1100)]  =  64,
    [MSG_SEG_SIZE(0b1101)]  = 128,
    [MSG_SEG_SIZE(0b1110)]  = 256,
};
#define MSG_SEG_SIZES_ENTRIES (sizeof(msg_seg_sizes) / sizeof(msg_seg_sizes[0]))

uint16_t msg_seg_size(uint8_t seg_size_field)
{
    if (MSG_SEG_SIZE(seg_size_field) >= MSG_SEG_SIZES_ENTRIES)
        return MSG_SEG_SIZE_INVALID;
    return msg_seg_sizes[MSG_SEG_SIZE(seg_size_field)];
}

uint8_t msg_seg_size_field(uint8_t seg_size)
{
    for (int i = 0; i < MSG_SEG_SIZES_ENTRIES; ++i) {
        if (seg_size == msg_seg_sizes[i])
            return MSG_SEG_SIZE_FIELD(i);
    }
    return MSG_SEG_FIELD_INVALID;
}

typedef uint8_t RdWrSpanIdx;

#define RDWR_SPAN_INDEX(wdptr, rdwrsize) ((wdptr << 4) | (rdwrsize))
#define RDWR_SPAN_INDEX_WDPTR(index) (((index) & (1 << 4)) >> 4)
#define RDWR_SPAN_INDEX_RDWRSIZE(index) ((index) & 0b1111)

#define RDWR_SPAN(byte_offset, num_bytes) { \
    .offset = byte_offset, \
    .bytes = num_bytes, \
}

typedef struct RdWrSpan {
    uint8_t offset;
    uint16_t bytes;
} RdWrSpan;

typedef struct RdWrSpanFields {
    uint8_t wdptr;
    uint8_t rdwr_size;
} RdWrSpanFields;

/* Tables 4-3, 4-4 in Spec */
static const RdWrSpan rdwr_span_table[] = {
    [RDWR_SPAN_INDEX(0b0, 0b0000)] = RDWR_SPAN(0,   1),
    [RDWR_SPAN_INDEX(0b0, 0b0001)] = RDWR_SPAN(1,   1),
    [RDWR_SPAN_INDEX(0b0, 0b0010)] = RDWR_SPAN(2,   1),
    [RDWR_SPAN_INDEX(0b0, 0b0011)] = RDWR_SPAN(3,   1),
    [RDWR_SPAN_INDEX(0b1, 0b0000)] = RDWR_SPAN(4,   1),
    [RDWR_SPAN_INDEX(0b1, 0b0001)] = RDWR_SPAN(5,   1),
    [RDWR_SPAN_INDEX(0b1, 0b0010)] = RDWR_SPAN(6,   1),
    [RDWR_SPAN_INDEX(0b1, 0b0011)] = RDWR_SPAN(7,   1),
    [RDWR_SPAN_INDEX(0b0, 0b0100)] = RDWR_SPAN(0,   2),
    [RDWR_SPAN_INDEX(0b0, 0b0101)] = RDWR_SPAN(0,   3),
    [RDWR_SPAN_INDEX(0b0, 0b0110)] = RDWR_SPAN(2,   2),
    [RDWR_SPAN_INDEX(0b0, 0b0111)] = RDWR_SPAN(0,   5),
    [RDWR_SPAN_INDEX(0b1, 0b0100)] = RDWR_SPAN(4,   2),
    [RDWR_SPAN_INDEX(0b1, 0b0101)] = RDWR_SPAN(5,   3),
    [RDWR_SPAN_INDEX(0b1, 0b0110)] = RDWR_SPAN(6,   2),
    [RDWR_SPAN_INDEX(0b1, 0b0111)] = RDWR_SPAN(3,   5),
    [RDWR_SPAN_INDEX(0b0, 0b1000)] = RDWR_SPAN(0,   4),
    [RDWR_SPAN_INDEX(0b1, 0b1000)] = RDWR_SPAN(4,   4),
    [RDWR_SPAN_INDEX(0b0, 0b1001)] = RDWR_SPAN(0,   6),
    [RDWR_SPAN_INDEX(0b1, 0b1001)] = RDWR_SPAN(2,   6),
    [RDWR_SPAN_INDEX(0b0, 0b1010)] = RDWR_SPAN(0,   7),
    [RDWR_SPAN_INDEX(0b1, 0b1010)] = RDWR_SPAN(1,   7),
    [RDWR_SPAN_INDEX(0b0, 0b1011)] = RDWR_SPAN(0,   8),
    [RDWR_SPAN_INDEX(0b1, 0b1011)] = RDWR_SPAN(0,  16),
    [RDWR_SPAN_INDEX(0b0, 0b1100)] = RDWR_SPAN(0,  32),
    [RDWR_SPAN_INDEX(0b1, 0b1100)] = RDWR_SPAN(0,  64),
    [RDWR_SPAN_INDEX(0b0, 0b1101)] = RDWR_SPAN(0,  96),
    [RDWR_SPAN_INDEX(0b1, 0b1101)] = RDWR_SPAN(0, 128),
    [RDWR_SPAN_INDEX(0b0, 0b1110)] = RDWR_SPAN(0, 160),
    [RDWR_SPAN_INDEX(0b1, 0b1110)] = RDWR_SPAN(0, 192),
    [RDWR_SPAN_INDEX(0b0, 0b1111)] = RDWR_SPAN(0, 224),
    [RDWR_SPAN_INDEX(0b1, 0b1111)] = RDWR_SPAN(0, 256),
};

#define RDWR_SPAN_TABLE_ENTRIES \
    (sizeof(rdwr_span_table) / sizeof(rdwr_span_table[0]))

/* See entries marked reserved in Table 4-4 */
static bool is_rdwr_len_supported(AccessType access, uint16_t bytes)
{
    return !(access == ACCESS_WRITE &&
             bytes > sizeof(uint64_t) &&
             !is_pow2(bytes));
}

static int rdwr_span_from_fields(uint16_t *bytes, uint8_t *offset,
                                 AccessType access,
                                 uint8_t wdptr, uint8_t rdwr_size)
{
    RdWrSpanIdx idx = RDWR_SPAN_INDEX(wdptr, rdwr_size);
    if (idx >= RDWR_SPAN_TABLE_ENTRIES ||
        !is_rdwr_len_supported(access, rdwr_span_table[idx].bytes)) {
        qemu_log("RIO: NOTICE: invalid span fields: wdptr 0x%x rdwrsize 0x%x\r\n",
                 wdptr, rdwr_size);
        return 1;
    }
    *bytes = rdwr_span_table[idx].bytes;
    *offset = rdwr_span_table[idx].offset;
    return 0;
}

static void rdwr_span_to_fields(uint8_t *wdptr, uint8_t *rdwr_size,
                                AccessType access,
                                uint16_t bytes, uint8_t offset)
{
    RdWrSpanIdx idx;
    if (is_rdwr_len_supported(access, bytes)) {
        for (idx = 0; idx < RDWR_SPAN_TABLE_ENTRIES; ++idx) {
            if (rdwr_span_table[idx].bytes == bytes &&
                rdwr_span_table[idx].offset == offset) {
                *wdptr = RDWR_SPAN_INDEX_WDPTR(idx);
                *rdwr_size = RDWR_SPAN_INDEX_RDWRSIZE(idx);
                return;
            }
        }
    }
    qemu_log("RIO: NOTICE: invalid span: bytes %u offset %u\r\n",
             bytes, offset);
    abort(); /* invariant in RioPkt state */
}

static inline uint64_t rdwr_dw_mask_from_span(uint16_t bytes, uint8_t offset)
{
    return ((1ULL << (bytes * BITS_PER_BYTE)) - 1)
        << ((sizeof(uint64_t) - bytes - offset) * BITS_PER_BYTE);
}

static inline unsigned div_ceil(unsigned x, unsigned y)
{
    return x / y + (x % y ? 1 : 0);
}

/* Pack the bits of a value into a buffer, MSB first. Bytes of the buffer
 * are filled starting from MSB, e.g. push(0x2) => [0x80].
 * Internal representation of value (byteorder) does not affect this func. */
static void field_push(uint8_t *buf, unsigned size, unsigned *pos,
                       unsigned width, uint64_t value)
{
    assert(width <= sizeof(value) * 8);
    assert(pos);
    unsigned byte_pos = *pos / BITS_PER_BYTE;
    unsigned bit_pos = *pos % BITS_PER_BYTE;
    unsigned bits_left_in_byte = BITS_PER_BYTE - bit_pos;
    unsigned bits_left = width;
    unsigned frag_width, frag_shift, buf_shift;
    uint8_t frag, buf_mask;
    uint64_t frag_mask;

    /* move value bits to the MSB edge; as we pack, we shift value left */
    value <<= (sizeof(uint64_t) * BITS_PER_BYTE - width);

    while (bits_left > 0) {
        unsigned bits_in_current_byte = MIN(bits_left_in_byte, bits_left);
        assert(byte_pos < size);
        assert(bit_pos < BITS_PER_BYTE);
        assert(BITS_PER_BYTE - bit_pos >= bits_in_current_byte);

        frag_width = bits_in_current_byte;
        frag_mask = ~(~0ULL >> frag_width);
        frag_shift = sizeof(uint64_t) * BITS_PER_BYTE - frag_width;
        frag = (value & frag_mask) >> frag_shift;

        buf_shift = BITS_PER_BYTE - bit_pos - frag_width;
        buf_mask = ((1U << frag_width) - 1) << buf_shift;
        buf[byte_pos] &= ~buf_mask;
        buf[byte_pos] |= frag << buf_shift;

        bit_pos = (bit_pos + bits_in_current_byte) % BITS_PER_BYTE;
        bits_left_in_byte -= bits_in_current_byte;
        if (bits_left_in_byte == 0) {
            byte_pos += 1;
            bits_left_in_byte = BITS_PER_BYTE;
        }
        *pos += bits_in_current_byte;
        bits_left -= bits_in_current_byte;
        value <<= bits_in_current_byte;
    }
}

static uint64_t field_pop(uint8_t *buf, unsigned size, unsigned *pos,
                          unsigned width)
{
    assert(pos);
    unsigned byte_pos = *pos / BITS_PER_BYTE;
    unsigned bit_pos = *pos % BITS_PER_BYTE;
    unsigned val_pos = 0;
    unsigned bits_left_in_byte = BITS_PER_BYTE - bit_pos;
    unsigned bits_left = width;
    unsigned frag_width, frag_shift;
    uint8_t frag, frag_mask;
    uint64_t value = 0;

    assert(width <= sizeof(value) * BITS_PER_BYTE);

    while (bits_left > 0) {
        unsigned bits_in_curr_byte = MIN(bits_left_in_byte, bits_left);
        assert(byte_pos < size);
        assert(bit_pos < BITS_PER_BYTE);
        assert(BITS_PER_BYTE - bit_pos >= bits_in_curr_byte);

        frag_width = bits_in_curr_byte;
        frag_mask = ~(0xff >> frag_width);
        frag_shift = BITS_PER_BYTE - frag_width;
        frag = ((buf[byte_pos] << bit_pos) & frag_mask) >> frag_shift;

        value |= (uint64_t)frag << (width - val_pos - frag_width);
        val_pos += bits_in_curr_byte;
        bit_pos = (bit_pos + bits_in_curr_byte) % BITS_PER_BYTE;
        bits_left_in_byte -= bits_in_curr_byte;
        if (bits_left_in_byte == 0) {
            byte_pos += 1;
            bits_left_in_byte = BITS_PER_BYTE;
        }
        *pos += bits_in_curr_byte;
        bits_left -= bits_in_curr_byte;
    }
    return value;
}

void pack_header(uint8_t *buf, unsigned size, unsigned *pos,
                 const RioPkt *pkt)
{
    field_push(buf, size, pos, PKT_FIELD_PRIO, pkt->prio);
    field_push(buf, size, pos, PKT_FIELD_TTYPE, pkt->ttype);
    field_push(buf, size, pos, PKT_FIELD_FTYPE, pkt->ftype);
    field_push(buf, size, pos, dev_id_width(pkt->ttype), pkt->dest_id);
    field_push(buf, size, pos, dev_id_width(pkt->ttype), pkt->src_id);
}

int unpack_header(RioPkt *pkt, uint8_t *buf, unsigned size,
                  unsigned *pos)
{
    assert(pos && *pos == 0);
    pkt->prio = field_pop(buf, size, pos, PKT_FIELD_PRIO);
    pkt->ttype = field_pop(buf, size, pos, PKT_FIELD_TTYPE);
    pkt->ftype = field_pop(buf, size, pos, PKT_FIELD_FTYPE);
    pkt->dest_id = field_pop(buf, size, pos, dev_id_width(pkt->ttype));
    pkt->src_id = field_pop(buf, size, pos, dev_id_width(pkt->ttype));
    return 0;
}

static void pack_payload(uint8_t *buf, unsigned size, unsigned *pos,
                         const RioPkt *pkt)
{
    int i;
    for (i = 0; i < pkt->payload_len; ++i) {
        field_push(buf, size, pos, sizeof(uint64_t) * BITS_PER_BYTE,
                   pkt->payload[i]);
    }
}

static int unpack_payload(RioPkt *pkt, uint8_t *buf, unsigned size,
                          unsigned *pos)
{
    pkt->payload_len = 0;
    while (div_ceil(*pos, BITS_PER_BYTE) + sizeof(uint64_t) <= size) {
        pkt->payload[pkt->payload_len++] =
            field_pop(buf, size, pos, sizeof(uint64_t) * BITS_PER_BYTE);
    }
    return 0;
}

static void pack_addr(uint8_t *buf, unsigned size, unsigned *pos,
                      const RioPkt *pkt, uint8_t wdptr)
{
    unsigned ext_addr_width = pkt->addr_width - 34;
    field_push(buf, size, pos, ext_addr_width,
               pkt->dw_addr >> PKT_FIELD_ADDR);
    field_push(buf, size, pos, PKT_FIELD_ADDR,
                /* part of */ pkt->dw_addr);
    field_push(buf, size, pos, PKT_FIELD_WDPTR, wdptr);
    field_push(buf, size, pos, PKT_FIELD_XAMSBS,
               pkt->dw_addr >> (29 + ext_addr_width));
}

static int unpack_addr(RioPkt *pkt, uint8_t *wdptr,
                       uint8_t *buf, unsigned size,
                       unsigned *pos, unsigned width)
{
    uint32_t ext_addr = field_pop(buf, size, pos, width - 34);
    uint32_t addr = field_pop(buf, size, pos, PKT_FIELD_ADDR);
    uint8_t wdptr_field = field_pop(buf, size, pos, PKT_FIELD_WDPTR);
    uint8_t xamsbs = field_pop(buf, size, pos, PKT_FIELD_XAMSBS);
    /* byte address:
       xambs[2] | ext_addr[0 or 16 or 32] | addr[29] | wdptr[1] | [2] */
    pkt->dw_addr = (xamsbs << (width - 3 - PKT_FIELD_XAMSBS)) |
                      (ext_addr << PKT_FIELD_ADDR) | addr;
    *wdptr = wdptr_field;
    return 0;
}

unsigned pack_pkt(uint8_t *buf, unsigned size, const RioPkt *pkt)
{
    unsigned pos = 0;

    bzero(buf, size);
    pack_header(buf, size, &pos, pkt);
    pack_body(buf, size, &pos, pkt);

    return div_ceil(pos, BITS_PER_BYTE);
}

void pack_body(uint8_t *buf, unsigned size, unsigned *pos, const RioPkt *pkt)
{
    AccessType access;
    uint8_t rdwr_size, wdptr;
    uint8_t seg_size_field;

    switch (pkt->ftype) {
      case RIO_FTYPE_MAINT:
        field_push(buf, size, pos, PKT_FIELD_TRANSACTION, pkt->transaction);

        switch (pkt->transaction) {
            case RIO_TRANS_MAINT_REQ_READ:
            case RIO_TRANS_MAINT_REQ_WRITE:
                access = pkt->transaction == RIO_TRANS_MAINT_REQ_READ
                            ? ACCESS_READ : ACCESS_WRITE;
                rdwr_span_to_fields(&wdptr, &rdwr_size, access,
                                    pkt->rdwr_bytes, pkt->dw_offset);
                field_push(buf, size, pos, PKT_FIELD_RDWRSIZE, rdwr_size);
                field_push(buf, size, pos, PKT_FIELD_SRC_TID, pkt->src_tid);
                field_push(buf, size, pos, PKT_FIELD_HOP_COUNT, pkt->hop_count);
                field_push(buf, size, pos, PKT_FIELD_CONFIG_OFFSET,
                           pkt->dw_addr);
                field_push(buf, size, pos, PKT_FIELD_WDPTR, wdptr);
                *pos += 2; /* reserved bits (see Figure 4-4 in spec */

                if (pkt->transaction == RIO_TRANS_MAINT_REQ_WRITE) {
                    pack_payload(buf, size, pos, pkt);
                }
                break;
            case RIO_TRANS_MAINT_RESP_READ:
            case RIO_TRANS_MAINT_RESP_WRITE:
                field_push(buf, size, pos, PKT_FIELD_STATUS, pkt->status);
                field_push(buf, size, pos, PKT_FIELD_TARGET_TID, pkt->target_tid);
                field_push(buf, size, pos, PKT_FIELD_HOP_COUNT, pkt->hop_count);
                *pos += 24; /* reserved bits (see Figure 4-5 in spec */

                if (pkt->transaction == RIO_TRANS_MAINT_RESP_READ) {
                    pack_payload(buf, size, pos, pkt);
                }
                break;
            default:
                qemu_log("RIO: ERROR: unsupported MAINT transaction: %x\r\n",
                         pkt->transaction);
                abort();
        }
        break;
      case RIO_FTYPE_MSG:
        field_push(buf, size, pos, PKT_FIELD_MSG_LEN, pkt->msg_len - 1);

        seg_size_field = msg_seg_size_field(pkt->seg_size);
        field_push(buf, size, pos, PKT_FIELD_SEG_SIZE, seg_size_field);

        field_push(buf, size, pos, PKT_FIELD_LETTER, pkt->letter);
        field_push(buf, size, pos, PKT_FIELD_MBOX, pkt->mbox & 0b11);

        if (pkt->msg_len > 0) {
            assert(pkt->mbox < (1 << PKT_FIELD_MBOX));
            field_push(buf, size, pos, PKT_FIELD_MSGSEG_XMBOX, pkt->msg_seg);
        } else {
            assert(pkt->mbox < (1 << (PKT_FIELD_MBOX + PKT_FIELD_MSGSEG_XMBOX)));
            field_push(buf, size, pos, PKT_FIELD_MSGSEG_XMBOX,
                       (pkt->mbox >> 2) & 0b1111);
        }
        pack_payload(buf, size, pos, pkt);
        break;
      case RIO_FTYPE_DOORBELL:
        *pos += 8; /* reserved (see Figure 4-1 in Spec) */
        field_push(buf, size, pos, PKT_FIELD_SRC_TID, pkt->src_tid);
        field_push(buf, size, pos, PKT_FIELD_INFO, pkt->info);
        break;
      case RIO_FTYPE_READ:
      case RIO_FTYPE_WRITE:
        access = pkt->ftype == RIO_FTYPE_READ ? ACCESS_READ : ACCESS_WRITE;
        rdwr_span_to_fields(&wdptr, &rdwr_size, access,
                            pkt->rdwr_bytes, pkt->dw_offset);

        field_push(buf, size, pos, PKT_FIELD_TRANSACTION, pkt->transaction);
        field_push(buf, size, pos, PKT_FIELD_RDWRSIZE, rdwr_size);
        field_push(buf, size, pos, PKT_FIELD_SRC_TID, pkt->src_tid);

        pack_addr(buf, size, pos, pkt, wdptr);
        if (pkt->ftype == RIO_FTYPE_WRITE) {
            pack_payload(buf, size, pos, pkt);
        }
        break;
      case RIO_FTYPE_STREAM_WRITE:
        assert(pkt->dw_offset == 0);
        assert(pkt->rdwr_bytes >= sizeof(uint64_t)); /* at least one word */
        assert(pkt->rdwr_bytes % sizeof(uint64_t) == 0); /* whole dwords */
        pack_addr(buf, size, pos, pkt, /* wdptr (reserved field) */ 0);
        pack_payload(buf, size, pos, pkt);
        break;
      case RIO_FTYPE_RESP:
        field_push(buf, size, pos, PKT_FIELD_TRANSACTION, pkt->transaction);
        field_push(buf, size, pos, PKT_FIELD_STATUS, pkt->status);
        switch (pkt->transaction) {
            case RIO_TRANS_RESP_MSG:
                field_push(buf, size, pos, PKT_FIELD_MBOX, pkt->mbox);
                field_push(buf, size, pos, PKT_FIELD_LETTER, pkt->letter);
                field_push(buf, size, pos, PKT_FIELD_MSGSEG_XMBOX, pkt->msg_seg);
                break;
            case RIO_TRANS_RESP_WITHOUT_PAYLOAD:
                field_push(buf, size, pos, PKT_FIELD_TARGET_TID,
                           pkt->target_tid);
                break;
            default:
                qemu_log("RIO: ERROR: ftype %u trans 0x%x not supported\r\n",
                         pkt->ftype, pkt->transaction);
                abort();
        }
        break;
      default:
        qemu_log("RIO: ERROR: packet ftype %u not supported\r\n", pkt->ftype);
        abort();
    }
}

int unpack_body(RioPkt *pkt, uint8_t *buf, unsigned size, unsigned *pos)
{
    uint8_t rdwrsize, wdptr;
    uint8_t seg_size_field, msg_len_field, xmbox_field;
    unsigned exp_payload_len;
    AccessType access;
    int rc;

    switch (pkt->ftype) {
        case RIO_FTYPE_MAINT:
            pkt->transaction = field_pop(buf, size, pos, PKT_FIELD_TRANSACTION);
            switch (pkt->transaction) {
                case RIO_TRANS_MAINT_REQ_READ:
                case RIO_TRANS_MAINT_REQ_WRITE:
                    access = pkt->transaction == RIO_TRANS_MAINT_REQ_READ ?
                                ACCESS_READ : ACCESS_WRITE;
                    rdwrsize = field_pop(buf, size, pos, PKT_FIELD_RDWRSIZE);
                    pkt->src_tid = field_pop(buf, size, pos,
                                             PKT_FIELD_SRC_TID);
                    pkt->hop_count = field_pop(buf, size, pos,
                                               PKT_FIELD_HOP_COUNT);
                    pkt->dw_addr = field_pop(buf, size, pos,
                                             PKT_FIELD_CONFIG_OFFSET);
                    wdptr = field_pop(buf, size, pos, PKT_FIELD_WDPTR);
                    rc = rdwr_span_from_fields(&pkt->rdwr_bytes, &pkt->dw_offset,
                                               access, wdptr, rdwrsize);
                    if (rc)
                        return rc;
                    *pos += 2; /* reserved bits (see Figure 4-4 in Spec) */

                    if (pkt->transaction == RIO_TRANS_MAINT_REQ_WRITE) {
                        rc = unpack_payload(pkt, buf, size, pos);
                        exp_payload_len = div_ceil(pkt->rdwr_bytes,
                                                   sizeof(uint64_t));
                        if (pkt->payload_len != exp_payload_len) {
                            qemu_log("RIO: NOTICE: payload does not match req size: "
                                     "payload dwords %u != rdwr bytes / 8 = %u\r\n",
                                     pkt->payload_len, exp_payload_len);
                        }
                        return rc;
                    } else {
                        pkt->payload_len = 0;
                    }
                    return 0;
                case RIO_TRANS_MAINT_RESP_READ:
                case RIO_TRANS_MAINT_RESP_WRITE:
                    pkt->status = field_pop(buf, size, pos, PKT_FIELD_STATUS);
                    pkt->target_tid = field_pop(buf, size, pos,
                                                PKT_FIELD_TARGET_TID);
                    pkt->hop_count = field_pop(buf, size, pos,
                                               PKT_FIELD_HOP_COUNT);
                    *pos += 24; /* reserved bits (see Figure 4-5 in Spec) */

                    if (pkt->transaction == RIO_TRANS_MAINT_RESP_READ) {
                        /* Don't have a field to cross-check the size against */
                        return unpack_payload(pkt, buf, size, pos);
                    } else {
                        pkt->payload_len = 0;
                    }
                    return 0;
                default:
                    qemu_log("RIO: invalid field in MAINT packet: transaction %x\r\n",
                             pkt->transaction);
                    return 1;
            }
            break;
        case RIO_FTYPE_MSG:
            msg_len_field = field_pop(buf, size, pos, PKT_FIELD_MSG_LEN);
            pkt->msg_len = msg_len_field + 1;
            seg_size_field = field_pop(buf, size, pos, PKT_FIELD_SEG_SIZE);
            pkt->seg_size = msg_seg_size(seg_size_field);
            if (pkt->seg_size == MSG_SEG_SIZE_INVALID) {
                qemu_log("RIO: NOTICE: invalid seg size in message pkt: %x\r\n",
                         seg_size_field);
                return 1;
            }

            pkt->letter = field_pop(buf, size, pos, PKT_FIELD_LETTER);
            pkt->mbox = field_pop(buf, size, pos, PKT_FIELD_MBOX);

            if (pkt->msg_len > 1) {
                pkt->msg_seg = field_pop(buf, size, pos,
                                         PKT_FIELD_MSGSEG_XMBOX);
                assert(pkt->mbox < (1 << PKT_FIELD_MBOX));
            } else {
                xmbox_field = field_pop(buf, size, pos, PKT_FIELD_MSGSEG_XMBOX);
                pkt->mbox |= xmbox_field << 2;
                assert(pkt->mbox <
                        (1 << (PKT_FIELD_MBOX + PKT_FIELD_MSGSEG_XMBOX)));
            }
            rc = unpack_payload(pkt, buf, size, pos);
            if (rc)
                return rc;
            exp_payload_len = div_ceil(pkt->seg_size, sizeof(uint64_t));
            if (pkt->payload_len > exp_payload_len) {
                qemu_log("RIO: NOTICE: payload size does not match seg size: "
                         "payload %u > seg size %u\r\n", pkt->payload_len,
                         exp_payload_len);
                return 1;
            }
            return 0;
      case RIO_FTYPE_DOORBELL:
        *pos += 8; /* reserved (see Figure 4-1 in Spec) */
        pkt->src_tid = field_pop(buf, size, pos, PKT_FIELD_SRC_TID);
        pkt->info = field_pop(buf, size, pos, PKT_FIELD_INFO);
        return 0;
      case RIO_FTYPE_READ:
      case RIO_FTYPE_WRITE:
            access = pkt->ftype == RIO_FTYPE_READ ?  ACCESS_READ : ACCESS_WRITE;
            pkt->transaction = field_pop(buf, size, pos,
                                         PKT_FIELD_TRANSACTION);

            rdwrsize = field_pop(buf, size, pos, PKT_FIELD_RDWRSIZE);
            pkt->src_tid = field_pop(buf, size, pos, PKT_FIELD_SRC_TID);

            assert(pkt->addr_width);
            rc = unpack_addr(pkt, &wdptr, buf, size, pos, pkt->addr_width);
            if (rc)
                return rc;

            rc = rdwr_span_from_fields(&pkt->rdwr_bytes, &pkt->dw_offset,
                                       access, wdptr, rdwrsize);
            if (rc)
                return rc;

            if (pkt->ftype == RIO_FTYPE_WRITE) {
                rc = unpack_payload(pkt, buf, size, pos);
                if (rc)
                    return rc;
                exp_payload_len = div_ceil(pkt->rdwr_bytes, sizeof(uint64_t));
                if (pkt->payload_len != exp_payload_len) {
                    qemu_log("RIO: NOTICE: payload size does not match req size: "
                             "payload %u > rdwr bytes / 8 = %u\r\n", pkt->payload_len,
                             exp_payload_len);
                    return 1;
                }
            } else {
                pkt->payload_len = 0;
            }
            return 0;
      case RIO_FTYPE_RESP:
        pkt->transaction = field_pop(buf, size, pos, PKT_FIELD_TRANSACTION);
        pkt->status = field_pop(buf, size, pos, PKT_FIELD_STATUS);
        switch (pkt->transaction) {
            case RIO_TRANS_RESP_MSG:
                pkt->mbox = field_pop(buf, size, pos, PKT_FIELD_MBOX);
                pkt->letter = field_pop(buf, size, pos, PKT_FIELD_LETTER);
                pkt->msg_seg = field_pop(buf, size, pos, PKT_FIELD_MSGSEG_XMBOX);
                break;
            case RIO_TRANS_RESP_WITHOUT_PAYLOAD:
            case RIO_TRANS_RESP_WITH_PAYLOAD:
                pkt->target_tid = field_pop(buf, size, pos,
                                            PKT_FIELD_TARGET_TID);
                if (pkt->transaction == RIO_TRANS_RESP_WITH_PAYLOAD) {
                    rc = unpack_payload(pkt, buf, size, pos);
                    if (rc)
                        return rc;
                }
                break;
            default:
                qemu_log("RIO: ERROR: ftype %u trans 0x%x not supported\r\n",
                         pkt->ftype, pkt->transaction);
                abort();
                return 1; /* unreeachable */
        }
        return 0;
      default:
            qemu_log("RIO: ERROR: unpacking packet type %x not implemented\r\n",
                     pkt->ftype);
            abort();
            return 1; /* unreeachable */
    }
}

int unpack_pkt(RioPkt *pkt, uint8_t *buf, unsigned size)
{
    unsigned pos = 0;
    bzero(pkt, sizeof(RioPkt));
    return unpack_header(pkt, buf, size, &pos) ||
           unpack_body(pkt, buf, size, &pos);
}
