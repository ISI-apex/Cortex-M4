#include <stdint.h>
#include <stdbool.h>

#include "hwinfo.h"
#include "regops.h"
#include "object.h"
#include "mem.h"
#include "dma.h"
#include "bit.h"

#define PL330_DEBUG_MCGEN

// Type renaming to bridge to Linux driver source

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef struct pl330_dmac dma;
typedef uint8_t * dma_addr_t;

#define __iomem volatile
#define cpu_to_le32(addr) addr
#define le32_to_cpu(addr) addr
#define cpu_to_le16(addr) addr
#define le16_to_cpu(addr) addr

#define writel(v, a) REG_WRITE32(a, v)
#define readl(a)  REG_READ32(a)

#define EINVAL 1

#define BIT(n) (1 << (n))
#define PL330_QUIRK_BROKEN_NO_FLUSHP BIT(0)

enum pl330_cachectrl {
	CCTRL0,		/* Noncacheable and nonbufferable */
	CCTRL1,		/* Bufferable only */
	CCTRL2,		/* Cacheable, but do not allocate */
	CCTRL3,		/* Cacheable and bufferable, but do not allocate */
	INVALID1,	/* AWCACHE = 0x1000 */
	INVALID2,
	CCTRL6,		/* Cacheable write-through, allocate on writes only */
	CCTRL7,		/* Cacheable write-back, allocate on writes only */
};

enum pl330_byteswap {
	SWAP_NO,
	SWAP_2,
	SWAP_4,
	SWAP_8,
	SWAP_16,
};

/* Register and Bit field Definitions */
#define DS			0x0
#define DS_ST_STOP		0x0
#define DS_ST_EXEC		0x1
#define DS_ST_CMISS		0x2
#define DS_ST_UPDTPC		0x3
#define DS_ST_WFE		0x4
#define DS_ST_ATBRR		0x5
#define DS_ST_QBUSY		0x6
#define DS_ST_WFP		0x7
#define DS_ST_KILL		0x8
#define DS_ST_CMPLT		0x9
#define DS_ST_FLTCMP		0xe
#define DS_ST_FAULT		0xf

#define DPC			0x4
#define INTEN			0x20
#define ES			0x24
#define INTSTATUS		0x28
#define INTCLR			0x2c
#define FSM			0x30
#define FSC			0x34
#define FTM			0x38

#define _FTC			0x40
#define FTC(n)			(_FTC + (n)*0x4)

#define _CS			0x100
#define CS(n)			(_CS + (n)*0x8)
#define CS_CNS			(1 << 21)

#define _CPC			0x104
#define CPC(n)			(_CPC + (n)*0x8)

#define _SA			0x400
#define SA(n)			(_SA + (n)*0x20)

#define _DA			0x404
#define DA(n)			(_DA + (n)*0x20)

#define _CC			0x408
#define CC(n)			(_CC + (n)*0x20)

#define CC_SRCINC		(1 << 0)
#define CC_DSTINC		(1 << 14)
#define CC_SRCPRI		(1 << 8)
#define CC_DSTPRI		(1 << 22)
#define CC_SRCNS		(1 << 9)
#define CC_DSTNS		(1 << 23)
#define CC_SRCIA		(1 << 10)
#define CC_DSTIA		(1 << 24)
#define CC_SRCBRSTLEN_SHFT	4
#define CC_DSTBRSTLEN_SHFT	18
#define CC_SRCBRSTSIZE_SHFT	1
#define CC_DSTBRSTSIZE_SHFT	15
#define CC_SRCCCTRL_SHFT	11
#define CC_SRCCCTRL_MASK	0x7
#define CC_DSTCCTRL_SHFT	25
#define CC_DRCCCTRL_MASK	0x7
#define CC_SWAP_SHFT		28

#define _LC0			0x40c
#define LC0(n)			(_LC0 + (n)*0x20)

#define _LC1			0x410
#define LC1(n)			(_LC1 + (n)*0x20)

#define DBGSTATUS		0xd00
#define DBG_BUSY		(1 << 0)

#define DBGCMD			0xd04
#define DBGINST0		0xd08
#define DBGINST1		0xd0c

#define CR0			0xe00
#define CR1			0xe04
#define CR2			0xe08
#define CR3			0xe0c
#define CR4			0xe10
#define CRD			0xe14

#define PERIPH_ID		0xfe0
#define PERIPH_REV_SHIFT	20
#define PERIPH_REV_MASK		0xf
#define PERIPH_REV_R0P0		0
#define PERIPH_REV_R1P0		1
#define PERIPH_REV_R1P1		2

#define CR0_PERIPH_REQ_SET	(1 << 0)
#define CR0_BOOT_EN_SET		(1 << 1)
#define CR0_BOOT_MAN_NS		(1 << 2)
#define CR0_NUM_CHANS_SHIFT	4
#define CR0_NUM_CHANS_MASK	0x7
#define CR0_NUM_PERIPH_SHIFT	12
#define CR0_NUM_PERIPH_MASK	0x1f
#define CR0_NUM_EVENTS_SHIFT	17
#define CR0_NUM_EVENTS_MASK	0x1f

#define CR1_ICACHE_LEN_SHIFT	0
#define CR1_ICACHE_LEN_MASK	0x7
#define CR1_NUM_ICACHELINES_SHIFT	4
#define CR1_NUM_ICACHELINES_MASK	0xf

#define CRD_DATA_WIDTH_SHIFT	0
#define CRD_DATA_WIDTH_MASK	0x7
#define CRD_WR_CAP_SHIFT	4
#define CRD_WR_CAP_MASK		0x7
#define CRD_WR_Q_DEP_SHIFT	8
#define CRD_WR_Q_DEP_MASK	0xf
#define CRD_RD_CAP_SHIFT	12
#define CRD_RD_CAP_MASK		0x7
#define CRD_RD_Q_DEP_SHIFT	16
#define CRD_RD_Q_DEP_MASK	0xf
#define CRD_DATA_BUFF_SHIFT	20
#define CRD_DATA_BUFF_MASK	0x3ff

#define PART			0x330
#define DESIGNER		0x41
#define REVISION		0x0
#define INTEG_CFG		0x0
#define PERIPH_ID_VAL		((PART << 0) | (DESIGNER << 12))

#define PL330_STATE_STOPPED		(1 << 0)
#define PL330_STATE_EXECUTING		(1 << 1)
#define PL330_STATE_WFE			(1 << 2)
#define PL330_STATE_FAULTING		(1 << 3)
#define PL330_STATE_COMPLETING		(1 << 4)
#define PL330_STATE_WFP			(1 << 5)
#define PL330_STATE_KILLING		(1 << 6)
#define PL330_STATE_FAULT_COMPLETING	(1 << 7)
#define PL330_STATE_CACHEMISS		(1 << 8)
#define PL330_STATE_UPDTPC		(1 << 9)
#define PL330_STATE_ATBARRIER		(1 << 10)
#define PL330_STATE_QUEUEBUSY		(1 << 11)
#define PL330_STATE_INVALID		(1 << 15)

#define PL330_STABLE_STATES (PL330_STATE_STOPPED | PL330_STATE_EXECUTING \
				| PL330_STATE_WFE | PL330_STATE_FAULTING)

#define CMD_DMAADDH		0x54
#define CMD_DMAEND		0x00
#define CMD_DMAFLUSHP		0x35
#define CMD_DMAGO		0xa0
#define CMD_DMALD		0x04
#define CMD_DMALDP		0x25
#define CMD_DMALP		0x20
#define CMD_DMALPEND		0x28
#define CMD_DMAKILL		0x01
#define CMD_DMAMOV		0xbc
#define CMD_DMANOP		0x18
#define CMD_DMARMB		0x12
#define CMD_DMASEV		0x34
#define CMD_DMAST		0x08
#define CMD_DMASTP		0x29
#define CMD_DMASTZ		0x0c
#define CMD_DMAWFE		0x36
#define CMD_DMAWFP		0x30
#define CMD_DMAWMB		0x13

#define SZ_DMAADDH		3
#define SZ_DMAEND		1
#define SZ_DMAFLUSHP		2
#define SZ_DMALD		1
#define SZ_DMALDP		2
#define SZ_DMALP		2
#define SZ_DMALPEND		2
#define SZ_DMAKILL		1
#define SZ_DMAMOV		6
#define SZ_DMANOP		1
#define SZ_DMARMB		1
#define SZ_DMASEV		2
#define SZ_DMAST		1
#define SZ_DMASTP		2
#define SZ_DMASTZ		1
#define SZ_DMAWFE		2
#define SZ_DMAWFP		2
#define SZ_DMAWMB		1
#define SZ_DMAGO		6

#define BRST_LEN(ccr)		((((ccr) >> CC_SRCBRSTLEN_SHFT) & 0xf) + 1)
#define BRST_SIZE(ccr)		(1 << (((ccr) >> CC_SRCBRSTSIZE_SHFT) & 0x7))

#define BYTE_TO_BURST(b, ccr)	((b) / BRST_SIZE(ccr) / BRST_LEN(ccr))
#define BURST_TO_BYTE(c, ccr)	((c) * BRST_SIZE(ccr) * BRST_LEN(ccr))

/* Use this _only_ to wait on transient states */
#define UNTIL(t, s)	while (!(_state(t) & (s)));

#ifdef PL330_DEBUG_MCGEN
static unsigned cmd_line;
#define PL330_DBGCMD_DUMP(off, x...)	do { \
						printf("%x:", cmd_line); \
						printf(x); \
						cmd_line += off; \
					} while (0)
#define PL330_DBGMC_START(addr)		(cmd_line = addr)
#else
#define PL330_DBGCMD_DUMP(off, x...)	do {} while (0)
#define PL330_DBGMC_START(addr)		do {} while (0)
#endif

#define MAX_CHANS               8
#define MCBUFSZ                 128
#define BURST_LEN_BITS          4 // See Table 3-21
#define BURST_LEN               (1 << BURST_LEN_BITS)
#define BURST_SIZE_BITS         4  // See Table 3-21
#define BURST_SIZE              (1 << BURST_SIZE_BITS)
#define TX_BURST_BITS           (BURST_LEN_BITS + BURST_SIZE_BITS)

/* Populated by the PL330 core driver for DMA API driver's info */
struct pl330_config {
	u32	periph_id;
#define DMAC_MODE_NS	(1 << 0)
	unsigned int	mode;
	unsigned int	data_bus_width:10; /* In number of bits */
	unsigned int	data_buf_dep:11;
	unsigned int	num_chan:4;
	unsigned int	num_peri:6;
	u32		peri_ns;
	unsigned int	num_events:6;
	u32		irq_ns;
};

/**
 * enum dma_transfer_direction - dma transfer mode and direction indicator
 * @DMA_MEM_TO_MEM: Async/Memcpy mode
 * @DMA_MEM_TO_DEV: Slave mode & From Memory to Device
 * @DMA_DEV_TO_MEM: Slave mode & From Device to Memory
 * @DMA_DEV_TO_DEV: Slave mode & From Device to Device
 */
enum dma_transfer_direction {
	DMA_MEM_TO_MEM,
	DMA_MEM_TO_DEV,
	DMA_DEV_TO_MEM,
	DMA_DEV_TO_DEV,
	DMA_TRANS_NONE,
};


enum desc_status {
	/* In the DMAC pool */
	FREE,
	/*
	 * Allocated to some channel during prep_xxx
	 * Also may be sitting on the work_list.
	 */
	PREP,
	/*
	 * Sitting on the work_list and already submitted
	 * to the PL330 core. Not more than two descriptors
	 * of a channel can be BUSY at any time.
	 */
	BUSY,
	/*
	 * Sitting on the channel work_list but xfer done
	 * by PL330 core
	 */
	DONE,
};

/* The xfer callbacks are made with one of these arguments. */
enum pl330_op_err {
	/* The all xfers in the request were success. */
	PL330_ERR_NONE,
	/* If req aborted due to global error. */
	PL330_ERR_ABORT,
	/* If req failed due to problem with Channel. */
	PL330_ERR_FAIL,
};

enum dmamov_dst {
	SAR = 0,
	CCR,
	DAR,
};

enum pl330_dst {
	SRC = 0,
	DST,
};

enum pl330_cond {
	SINGLE,
	BURST,
	ALWAYS,
};


struct dma_pl330_desc;

struct _pl330_req {
	u32 mc_bus;
	void *mc_cpu;
	struct dma_pl330_desc *desc;
        dma_cb_t cb;
        void *cb_arg;
        int rc;
        struct dma_tx *tx;
};

/* A DMAC Thread */
struct pl330_thread {
	u8 id;
	int ev;
	/* If the channel is not yet acquired by any client */
	bool free;
	/* Parent DMAC */
	struct pl330_dmac *dmac;
	/* Only two at a time */
	struct _pl330_req req[2];
	/* Index of the last enqueued request */
	unsigned lstenq;
	/* Index of the last submitted request or -1 if the DMA is stopped */
	int req_running;
};

struct pl330_dmac {
    struct object obj;
    const char *name; // for debug log

    /* Size of MicroCode buffers for each channel. */
    unsigned mcbufsz;
    /* ioremap'ed address of PL330 registers. */
    void __iomem	*base;
    /* Populated by the PL330 core driver during pl330_add */
    struct pl330_config	pcfg;

    /* Maximum possible events/irqs */
    int			events[32];

    /* BUS address of MicroCode buffer */
    dma_addr_t		mcode_bus;
    /* CPU address of MicroCode buffer */
    void			*mcode_cpu;
    /* List of all Channel threads */
    struct pl330_thread	channels[MAX_CHANS];
    struct pl330_thread	manager_chan;
    /* Pointer to MANAGER thread */
    struct pl330_thread	*manager;
#if 0
    /* State of DMAC operation */
    enum pl330_dmac_state	state;
    /* Holds list of reqs with due callbacks */
    struct list_head        req_done;

    /* Peripheral channels connected to this DMAC */
    unsigned int num_peripherals;
    struct dma_pl330_chan *peripherals; /* keep at end */
#endif
    int quirks;
};

/*
 * One cycle of DMAC operation.
 * There may be more than one xfer in a request.
 */
struct pl330_xfer {
	u32 src_addr;
	u32 dst_addr;
	/* Size to xfer */
	u32 bytes;
};

/**
 * Request Configuration.
 * The PL330 core does not modify this and uses the last
 * working configuration if the request doesn't provide any.
 *
 * The Client may want to provide this info only for the
 * first request and a request with new settings.
 */
struct pl330_reqcfg {
	/* Address Incrementing */
	unsigned dst_inc:1;
	unsigned src_inc:1;

	/*
	 * For now, the SRC & DST protection levels
	 * and burst size/length are assumed same.
	 */
	bool nonsecure;
	bool privileged;
	bool insnaccess;
	unsigned brst_len:5;
	unsigned brst_size:3; /* burst size = 2^(this value) */

	enum pl330_cachectrl dcctl;
	enum pl330_cachectrl scctl;
	enum pl330_byteswap swap;
	struct pl330_config *pcfg;
};

struct dma_pl330_desc {
	/* Xfer for PL330 core */
	struct pl330_xfer px;

	struct pl330_reqcfg rqcfg;

	enum desc_status status;

	int bytes_requested;
	bool last;

	enum dma_transfer_direction rqtype;
	/* Index of peripheral for the xfer. */
	unsigned peri:5;
};

struct _xfer_spec {
    u32 ccr;
    struct dma_pl330_desc *desc;
};

struct dma_tx {
    struct dma_pl330_desc desc;
    struct _pl330_req *req;
};

#define MAX_DMAS  8
#define MAX_TXES  8
static struct pl330_dmac dmas[MAX_DMAS];
static struct dma_tx txes[MAX_TXES];

static inline void store_u16(uint8_t *addr, u16 val)
{
	int i;
	for (i = 0; i < sizeof(val); ++i) {
		addr[i] = val & 0xff;
		val >>= 8;
	}
}

static inline void store_u32(uint8_t *addr, u32 val)
{
	int i;
	for (i = 0; i < sizeof(val); ++i) {
		addr[i] = val & 0xff;
		val >>= 8;
	}
}

static inline u16 load_u16(uint8_t *addr)
{
	u16 val = 0x0;
	int i;
	for (i = sizeof(val) - 1; i >= 0; --i) {
		val <<= 8;
		val |= addr[i];
	}
	return val;
}

static inline u32 load_u32(uint8_t *addr)
{
	u32 val = 0x0;
	int i;
	for (i = sizeof(val) - 1; i >= 0; --i) {
		val <<= 8;
		val |= addr[i];
	}
	return val;
}

static inline bool is_manager(struct pl330_thread *thrd)
{
	return thrd->dmac->manager == thrd;
}

static inline u32 get_revision(u32 periph_id)
{
	return (periph_id >> PERIPH_REV_SHIFT) & PERIPH_REV_MASK;
}

static inline u32 _emit_ADDH(unsigned dry_run, u8 buf[],
		enum pl330_dst da, u16 val)
{
	if (dry_run)
		return SZ_DMAADDH;

	buf[0] = CMD_DMAADDH;
	buf[0] |= (da << 1);
	store_u16(&buf[1], cpu_to_le16(val));

	PL330_DBGCMD_DUMP(SZ_DMAADDH, "\tDMAADDH %s %u\r\n",
		da == 1 ? "DA" : "SA", val);

	return SZ_DMAADDH;
}

static inline u32 _emit_END(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAEND;

	buf[0] = CMD_DMAEND;

	PL330_DBGCMD_DUMP(SZ_DMAEND, "\tDMAEND\r\n");

	return SZ_DMAEND;
}

static inline u32 _emit_FLUSHP(unsigned dry_run, u8 buf[], u8 peri)
{
	if (dry_run)
		return SZ_DMAFLUSHP;

	buf[0] = CMD_DMAFLUSHP;

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAFLUSHP, "\tDMAFLUSHP %u\r\n", peri >> 3);

	return SZ_DMAFLUSHP;
}

static inline u32 _emit_LD(unsigned dry_run, u8 buf[],	enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMALD;

	buf[0] = CMD_DMALD;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMALD, "\tDMALD%c\r\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMALD;
}

static inline u32 _emit_LDP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMALDP;

	buf[0] = CMD_DMALDP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMALDP, "\tDMALDP%c %u\r\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMALDP;
}

static inline u32 _emit_LP(unsigned dry_run, u8 buf[],
		unsigned loop, u8 cnt)
{
	if (dry_run)
		return SZ_DMALP;

	buf[0] = CMD_DMALP;

	if (loop)
		buf[0] |= (1 << 1);

	cnt--; /* DMAC increments by 1 internally */
	buf[1] = cnt;

	PL330_DBGCMD_DUMP(SZ_DMALP, "\tDMALP_%c %u\r\n", loop ? '1' : '0', cnt);

	return SZ_DMALP;
}

struct _arg_LPEND {
	enum pl330_cond cond;
	bool forever;
	unsigned loop;
	u8 bjump;
};

static inline u32 _emit_LPEND(unsigned dry_run, u8 buf[],
		const struct _arg_LPEND *arg)
{
	enum pl330_cond cond = arg->cond;
	bool forever = arg->forever;
	unsigned loop = arg->loop;
	u8 bjump = arg->bjump;

	if (dry_run)
		return SZ_DMALPEND;

	buf[0] = CMD_DMALPEND;

	if (loop)
		buf[0] |= (1 << 2);

	if (!forever)
		buf[0] |= (1 << 4);

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	buf[1] = bjump;

	PL330_DBGCMD_DUMP(SZ_DMALPEND, "\tDMALP%s%c_%c bjmpto_%x\r\n",
			forever ? "FE" : "END",
			cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'),
			loop ? '1' : '0',
			bjump);

	return SZ_DMALPEND;
}

static inline u32 _emit_KILL(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAKILL;

	buf[0] = CMD_DMAKILL;

	return SZ_DMAKILL;
}

static inline u32 _emit_MOV(unsigned dry_run, u8 buf[],
		enum dmamov_dst dst, u32 val)
{
	if (dry_run)
		return SZ_DMAMOV;

	buf[0] = CMD_DMAMOV;
	buf[1] = dst;
	store_u32(&buf[2], cpu_to_le32(val));

	PL330_DBGCMD_DUMP(SZ_DMAMOV, "\tDMAMOV %s 0x%x\r\n",
		dst == SAR ? "SAR" : (dst == DAR ? "DAR" : "CCR"), val);

	return SZ_DMAMOV;
}

static inline u32 _emit_NOP(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMANOP;

	buf[0] = CMD_DMANOP;

	PL330_DBGCMD_DUMP(SZ_DMANOP, "\tDMANOP\r\n");

	return SZ_DMANOP;
}

static inline u32 _emit_RMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMARMB;

	buf[0] = CMD_DMARMB;

	PL330_DBGCMD_DUMP(SZ_DMARMB, "\tDMARMB\r\n");

	return SZ_DMARMB;
}

static inline u32 _emit_SEV(unsigned dry_run, u8 buf[], u8 ev)
{
	if (dry_run)
		return SZ_DMASEV;

	buf[0] = CMD_DMASEV;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	PL330_DBGCMD_DUMP(SZ_DMASEV, "\tDMASEV %u\r\n", ev >> 3);

	return SZ_DMASEV;
}

static inline u32 _emit_ST(unsigned dry_run, u8 buf[], enum pl330_cond cond)
{
	if (dry_run)
		return SZ_DMAST;

	buf[0] = CMD_DMAST;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (1 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (1 << 0);

	PL330_DBGCMD_DUMP(SZ_DMAST, "\tDMAST%c\r\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'A'));

	return SZ_DMAST;
}

static inline u32 _emit_STP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMASTP;

	buf[0] = CMD_DMASTP;

	if (cond == BURST)
		buf[0] |= (1 << 1);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMASTP, "\tDMASTP%c %u\r\n",
		cond == SINGLE ? 'S' : 'B', peri >> 3);

	return SZ_DMASTP;
}

static inline u32 _emit_STZ(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMASTZ;

	buf[0] = CMD_DMASTZ;

	PL330_DBGCMD_DUMP(SZ_DMASTZ, "\tDMASTZ\r\n");

	return SZ_DMASTZ;
}

static inline u32 _emit_WFE(unsigned dry_run, u8 buf[], u8 ev,
		unsigned invalidate)
{
	if (dry_run)
		return SZ_DMAWFE;

	buf[0] = CMD_DMAWFE;

	ev &= 0x1f;
	ev <<= 3;
	buf[1] = ev;

	if (invalidate)
		buf[1] |= (1 << 1);

	PL330_DBGCMD_DUMP(SZ_DMAWFE, "\tDMAWFE %u%s\r\n",
		ev >> 3, invalidate ? ", I" : "");

	return SZ_DMAWFE;
}

static inline u32 _emit_WFP(unsigned dry_run, u8 buf[],
		enum pl330_cond cond, u8 peri)
{
	if (dry_run)
		return SZ_DMAWFP;

	buf[0] = CMD_DMAWFP;

	if (cond == SINGLE)
		buf[0] |= (0 << 1) | (0 << 0);
	else if (cond == BURST)
		buf[0] |= (1 << 1) | (0 << 0);
	else
		buf[0] |= (0 << 1) | (1 << 0);

	peri &= 0x1f;
	peri <<= 3;
	buf[1] = peri;

	PL330_DBGCMD_DUMP(SZ_DMAWFP, "\tDMAWFP%c %u\r\n",
		cond == SINGLE ? 'S' : (cond == BURST ? 'B' : 'P'), peri >> 3);

	return SZ_DMAWFP;
}

static inline u32 _emit_WMB(unsigned dry_run, u8 buf[])
{
	if (dry_run)
		return SZ_DMAWMB;

	buf[0] = CMD_DMAWMB;

	PL330_DBGCMD_DUMP(SZ_DMAWMB, "\tDMAWMB\r\n");

	return SZ_DMAWMB;
}

struct _arg_GO {
	u8 chan;
	u32 addr;
	unsigned ns;
};

static inline u32 _emit_GO(unsigned dry_run, u8 buf[],
		const struct _arg_GO *arg)
{
	u8 chan = arg->chan;
	u32 addr = arg->addr;
	unsigned ns = arg->ns;

	if (dry_run)
		return SZ_DMAGO;

	buf[0] = CMD_DMAGO;
	buf[0] |= (ns << 1);

	buf[1] = chan & 0x7;

	store_u32(&buf[2], cpu_to_le32(addr));

	return SZ_DMAGO;
}

static inline void _execute_DBGINSN(struct pl330_thread *thrd,
		u8 insn[], bool as_manager)
{
	void __iomem *regs = thrd->dmac->base;
	u32 val;

	val = (insn[0] << 16) | (insn[1] << 24);
	if (!as_manager) {
		val |= (1 << 0);
		val |= (thrd->id << 8); /* Channel Number */
	}
	writel(val, regs + DBGINST0);

	val = le32_to_cpu(load_u32(&insn[2]));
	writel(val, regs + DBGINST1);

#if 0
	/* If timed out due to halted state-machine */
	if (_until_dmac_idle(thrd)) {
		dev_err(thrd->dmac->ddma.dev, "DMAC halted!\r\n");
		return;
	}
#endif

	/* Get going */
	writel(0, regs + DBGCMD);
}

static inline u32 _state(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	u32 val;

	if (is_manager(thrd))
		val = readl(regs + DS) & 0xf;
	else
		val = readl(regs + CS(thrd->id)) & 0xf;

	switch (val) {
	case DS_ST_STOP:
		return PL330_STATE_STOPPED;
	case DS_ST_EXEC:
		return PL330_STATE_EXECUTING;
	case DS_ST_CMISS:
		return PL330_STATE_CACHEMISS;
	case DS_ST_UPDTPC:
		return PL330_STATE_UPDTPC;
	case DS_ST_WFE:
		return PL330_STATE_WFE;
	case DS_ST_FAULT:
		return PL330_STATE_FAULTING;
	case DS_ST_ATBRR:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_ATBARRIER;
	case DS_ST_QBUSY:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_QUEUEBUSY;
	case DS_ST_WFP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_WFP;
	case DS_ST_KILL:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_KILLING;
	case DS_ST_CMPLT:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_COMPLETING;
	case DS_ST_FLTCMP:
		if (is_manager(thrd))
			return PL330_STATE_INVALID;
		else
			return PL330_STATE_FAULT_COMPLETING;
	default:
		return PL330_STATE_INVALID;
	}
}

static void _stop(struct pl330_thread *thrd)
{
	void __iomem *regs = thrd->dmac->base;
	u8 insn[6] = {0, 0, 0, 0, 0, 0};

	if (_state(thrd) == PL330_STATE_FAULT_COMPLETING)
		UNTIL(thrd, PL330_STATE_FAULTING | PL330_STATE_KILLING);

	/* Return if nothing needs to be done */
	if (_state(thrd) == PL330_STATE_COMPLETING
		  || _state(thrd) == PL330_STATE_KILLING
		  || _state(thrd) == PL330_STATE_STOPPED)
		return;

	_emit_KILL(0, insn);

	/* Stop generating interrupts for SEV */
	writel(readl(regs + INTEN) & ~(1 << thrd->ev), regs + INTEN);

	_execute_DBGINSN(thrd, insn, is_manager(thrd));
}

static inline int _ldst_memtomem(unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;
	struct pl330_config *pcfg = pxs->desc->rqcfg.pcfg;

	/* check lock-up free version */
	if (get_revision(pcfg->periph_id) >= PERIPH_REV_R1P0) {
		while (cyc--) {
			off += _emit_LD(dry_run, &buf[off], ALWAYS);
			off += _emit_ST(dry_run, &buf[off], ALWAYS);
		}
	} else {
		while (cyc--) {
			off += _emit_LD(dry_run, &buf[off], ALWAYS);
			off += _emit_RMB(dry_run, &buf[off]);
			off += _emit_ST(dry_run, &buf[off], ALWAYS);
			off += _emit_WMB(dry_run, &buf[off]);
		}
	}

	return off;
}

static inline int _ldst_devtomem(struct pl330_dmac *pl330, unsigned dry_run,
				 u8 buf[], const struct _xfer_spec *pxs,
				 int cyc)
{
	int off = 0;
	enum pl330_cond cond;

	if (pl330->quirks & PL330_QUIRK_BROKEN_NO_FLUSHP)
		cond = BURST;
	else
		cond = SINGLE;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], cond, pxs->desc->peri);
		off += _emit_LDP(dry_run, &buf[off], cond, pxs->desc->peri);
		off += _emit_ST(dry_run, &buf[off], ALWAYS);

		if (!(pl330->quirks & PL330_QUIRK_BROKEN_NO_FLUSHP))
			off += _emit_FLUSHP(dry_run, &buf[off],
					    pxs->desc->peri);
	}

	return off;
}

static inline int _ldst_memtodev(struct pl330_dmac *pl330,
				 unsigned dry_run, u8 buf[],
				 const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;
	enum pl330_cond cond;

	if (pl330->quirks & PL330_QUIRK_BROKEN_NO_FLUSHP)
		cond = BURST;
	else
		cond = SINGLE;

	while (cyc--) {
		off += _emit_WFP(dry_run, &buf[off], cond, pxs->desc->peri);
		off += _emit_LD(dry_run, &buf[off], ALWAYS);
		off += _emit_STP(dry_run, &buf[off], cond, pxs->desc->peri);

		if (!(pl330->quirks & PL330_QUIRK_BROKEN_NO_FLUSHP))
			off += _emit_FLUSHP(dry_run, &buf[off],
					    pxs->desc->peri);
	}

	return off;
}

static int _bursts(struct pl330_dmac *pl330, unsigned dry_run, u8 buf[],
		const struct _xfer_spec *pxs, int cyc)
{
	int off = 0;

	switch (pxs->desc->rqtype) {
	case DMA_MEM_TO_DEV:
		off += _ldst_memtodev(pl330, dry_run, &buf[off], pxs, cyc);
		break;
	case DMA_DEV_TO_MEM:
		off += _ldst_devtomem(pl330, dry_run, &buf[off], pxs, cyc);
		break;
	case DMA_MEM_TO_MEM:
		off += _ldst_memtomem(dry_run, &buf[off], pxs, cyc);
		break;
	default:
		off += 0x40000000; /* Scare off the Client */
		break;
	}

	return off;
}

/* Returns bytes consumed and updates bursts */
static inline int _loop(struct pl330_dmac *pl330, unsigned dry_run, u8 buf[],
		unsigned long *bursts, const struct _xfer_spec *pxs)
{
	int cyc, cycmax, szlp, szlpend, szbrst, off;
	unsigned lcnt0, lcnt1, ljmp0, ljmp1;
	struct _arg_LPEND lpend;

	if (*bursts == 1)
		return _bursts(pl330, dry_run, buf, pxs, 1);

	/* Max iterations possible in DMALP is 256 */
	if (*bursts >= 256*256) {
		lcnt1 = 256;
		lcnt0 = 256;
		cyc = *bursts / lcnt1 / lcnt0;
	} else if (*bursts > 256) {
		lcnt1 = 256;
		lcnt0 = *bursts / lcnt1;
		cyc = 1;
	} else {
		lcnt1 = *bursts;
		lcnt0 = 0;
		cyc = 1;
	}

	szlp = _emit_LP(1, buf, 0, 0);
	szbrst = _bursts(pl330, 1, buf, pxs, 1);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 0;
	lpend.bjump = 0;
	szlpend = _emit_LPEND(1, buf, &lpend);

	if (lcnt0) {
		szlp *= 2;
		szlpend *= 2;
	}

	/*
	 * Max bursts that we can unroll due to limit on the
	 * size of backward jump that can be encoded in DMALPEND
	 * which is 8-bits and hence 255
	 */
	cycmax = (255 - (szlp + szlpend)) / szbrst;

	cyc = (cycmax < cyc) ? cycmax : cyc;

	off = 0;

	if (lcnt0) {
		off += _emit_LP(dry_run, &buf[off], 0, lcnt0);
		ljmp0 = off;
	}

	off += _emit_LP(dry_run, &buf[off], 1, lcnt1);
	ljmp1 = off;

	off += _bursts(pl330, dry_run, &buf[off], pxs, cyc);

	lpend.cond = ALWAYS;
	lpend.forever = false;
	lpend.loop = 1;
	lpend.bjump = off - ljmp1;
	off += _emit_LPEND(dry_run, &buf[off], &lpend);

	if (lcnt0) {
		lpend.cond = ALWAYS;
		lpend.forever = false;
		lpend.loop = 0;
		lpend.bjump = off - ljmp0;
		off += _emit_LPEND(dry_run, &buf[off], &lpend);
	}

	*bursts = lcnt1 * cyc;
	if (lcnt0)
		*bursts *= lcnt0;

	return off;
}

static inline int _setup_loops(struct pl330_dmac *pl330,
			       unsigned dry_run, u8 buf[],
			       const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = &pxs->desc->px;
	u32 ccr = pxs->ccr;
	unsigned long c, bursts = BYTE_TO_BURST(x->bytes, ccr);
	int off = 0;

	while (bursts) {
		c = bursts;
		off += _loop(pl330, dry_run, &buf[off], &c, pxs);
		bursts -= c;
	}

	return off;
}

static inline int _setup_xfer(struct pl330_dmac *pl330,
			      unsigned dry_run, u8 buf[],
			      const struct _xfer_spec *pxs)
{
	struct pl330_xfer *x = &pxs->desc->px;
	int off = 0;

	/* DMAMOV SAR, x->src_addr */
	off += _emit_MOV(dry_run, &buf[off], SAR, x->src_addr);
	/* DMAMOV DAR, x->dst_addr */
	off += _emit_MOV(dry_run, &buf[off], DAR, x->dst_addr);

	/* Setup Loop(s) */
	off += _setup_loops(pl330, dry_run, &buf[off], pxs);

	return off;
}

static int _setup_req(struct pl330_dmac *pl330, unsigned dry_run,
		      struct pl330_thread *thrd, unsigned index,
		      struct _xfer_spec *pxs)
{
	struct _pl330_req *req = &thrd->req[index];
	struct pl330_xfer *x;
	u8 *buf = req->mc_cpu;
	int off = 0;

	PL330_DBGMC_START(req->mc_bus);

	/* DMAMOV CCR, ccr */
	off += _emit_MOV(dry_run, &buf[off], CCR, pxs->ccr);

	x = &pxs->desc->px;
	/* Error if xfer length is not aligned at burst size */
	if (x->bytes % (BRST_SIZE(pxs->ccr) * BRST_LEN(pxs->ccr)))
		return -EINVAL;

	off += _setup_xfer(pl330, dry_run, &buf[off], pxs);

	/* DMASEV peripheral/event */
	off += _emit_SEV(dry_run, &buf[off], thrd->ev);
	/* DMAEND */
	off += _emit_END(dry_run, &buf[off]);

	return off;
}

static inline u32 _prepare_ccr(const struct pl330_reqcfg *rqc)
{
	u32 ccr = 0;

	if (rqc->src_inc)
		ccr |= CC_SRCINC;

	if (rqc->dst_inc)
		ccr |= CC_DSTINC;

	/* We set same protection levels for Src and DST for now */
	if (rqc->privileged)
		ccr |= CC_SRCPRI | CC_DSTPRI;
	if (rqc->nonsecure)
		ccr |= CC_SRCNS | CC_DSTNS;
	if (rqc->insnaccess)
		ccr |= CC_SRCIA | CC_DSTIA;

	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_SRCBRSTLEN_SHFT);
	ccr |= (((rqc->brst_len - 1) & 0xf) << CC_DSTBRSTLEN_SHFT);

	ccr |= (rqc->brst_size << CC_SRCBRSTSIZE_SHFT);
	ccr |= (rqc->brst_size << CC_DSTBRSTSIZE_SHFT);

	ccr |= (rqc->scctl << CC_SRCCCTRL_SHFT);
	ccr |= (rqc->dcctl << CC_DSTCCTRL_SHFT);

	ccr |= (rqc->swap << CC_SWAP_SHFT);

	return ccr;
}

/* Initialize the structure for PL330 configuration, that can be used
 * by the client driver the make best use of the DMAC
 */
static void read_dmac_config(struct pl330_dmac *pl330)
{
	void __iomem *regs = pl330->base;
	u32 val;

        pl330->pcfg.periph_id = ((readl(regs + (PERIPH_ID +  0)) & 0xff) <<  0) |
                                ((readl(regs + (PERIPH_ID +  4)) & 0xff) <<  8) |
                                ((readl(regs + (PERIPH_ID +  8)) & 0xff) << 16) |
                                ((readl(regs + (PERIPH_ID + 12)) & 0xff) << 24);

	val = readl(regs + CRD) >> CRD_DATA_WIDTH_SHIFT;
	val &= CRD_DATA_WIDTH_MASK;
	pl330->pcfg.data_bus_width = 8 * (1 << val);

	val = readl(regs + CRD) >> CRD_DATA_BUFF_SHIFT;
	val &= CRD_DATA_BUFF_MASK;
	pl330->pcfg.data_buf_dep = val + 1;

	val = readl(regs + CR0) >> CR0_NUM_CHANS_SHIFT;
	val &= CR0_NUM_CHANS_MASK;
	val += 1;
	pl330->pcfg.num_chan = val;

	val = readl(regs + CR0);
	if (val & CR0_PERIPH_REQ_SET) {
		val = (val >> CR0_NUM_PERIPH_SHIFT) & CR0_NUM_PERIPH_MASK;
		val += 1;
		pl330->pcfg.num_peri = val;
		pl330->pcfg.peri_ns = readl(regs + CR4);
	} else {
		pl330->pcfg.num_peri = 0;
	}

	val = readl(regs + CR0);
	if (val & CR0_BOOT_MAN_NS)
		pl330->pcfg.mode |= DMAC_MODE_NS;
	else
		pl330->pcfg.mode &= ~DMAC_MODE_NS;

	val = readl(regs + CR0) >> CR0_NUM_EVENTS_SHIFT;
	val &= CR0_NUM_EVENTS_MASK;
	val += 1;
	pl330->pcfg.num_events = val;

	pl330->pcfg.irq_ns = readl(regs + CR3);
}
static inline void _reset_thread(struct pl330_thread *thrd)
{
	struct pl330_dmac *pl330 = thrd->dmac;

	thrd->req[0].mc_cpu = (void *)(pl330->mcode_cpu
				+ (thrd->id * pl330->mcbufsz));
	thrd->req[0].mc_bus = (u32)(pl330->mcode_bus
				+ (thrd->id * pl330->mcbufsz));
	thrd->req[0].desc = NULL;

	thrd->req[1].mc_cpu = (void *)(thrd->req[0].mc_cpu
				+ pl330->mcbufsz / 2);
	thrd->req[1].mc_bus = (u32)(thrd->req[0].mc_bus
				+ pl330->mcbufsz / 2);
	thrd->req[1].desc = NULL;

	thrd->req_running = -1;
}

struct dma *dma_create(const char *name, uintptr_t base,
                       uint8_t *mcode_addr, unsigned mcode_sz)
{
    struct pl330_dmac *d = OBJECT_ALLOC(dmas);
    if (!d)
        return NULL;
    d->name = name;
    d->base = (void __iomem *) base;

    // Microcode buffer reachable from DMA and CPU via same addr
    d->mcode_bus = mcode_addr;
    d->mcode_cpu = mcode_addr;
    d->mcbufsz = MCBUFSZ;
    d->manager = &d->manager_chan;

    read_dmac_config(d);

    if (mcode_sz < MCBUFSZ * d->pcfg.num_chan) {
        printf("DMA: microcode space too small: %x <= %\r\n",
               mcode_sz, MCBUFSZ * d->pcfg.num_chan);
        OBJECT_FREE(d);
        return NULL;
    }

    /* Init Channel threads */
    for (unsigned i = 0; i < d->pcfg.num_chan; i++) {
        struct pl330_thread *thrd = &d->channels[i];
        thrd->id = i;
        thrd->dmac = d;
        _reset_thread(thrd);
        thrd->free = true;
    }

    printf("DMA %s: created\r\n", d->name);
    return (struct dma *)d;
}

void dma_destroy(struct dma *dma)
{
    struct pl330_dmac *pl330 = (struct pl330_dmac *)dma; // TODO: rename?

    printf("DMA %s: destroy\r\n", pl330->name);

    // TODO: kill threads?
    OBJECT_FREE(pl330);
}

struct dma_tx *dma_transfer(struct dma *dma, unsigned chan,
                            uint32_t *src, uint32_t *dst, unsigned sz,
                            dma_cb_t cb, void *cb_arg)
{
    ASSERT(dma);
    ASSERT(src && dst);
    struct pl330_dmac *pl330 = (struct pl330_dmac *)dma; // TODO: rename?

    printf("DMA %s: chan %u: %p -> %p sz %x\r\n",
           pl330->name, chan, src, dst, sz);

    if (chan >= pl330->pcfg.num_chan) {
        printf("DMA: invalid channel %u (>= %u)\r\n", chan, pl330->pcfg.num_chan);
        return NULL;
    }

    if (!(ALIGNED(src, TX_BURST_BITS) &&
          ALIGNED(dst, TX_BURST_BITS) &&
          ALIGNED(sz, TX_BURST_BITS))) {
        printf("DMA: ERROR: size %u/src 0x%x/dst 0x%x "
               "not aligned to burst bytes: %u\r\n",
               sz, src, dst, 1 << TX_BURST_BITS);
        return NULL;
    }

    // Only use req 0 ever. NOTE: this is not channel number, this is a request
    // queue (of length 2) from the Linux driver, which we don't use (for
    // simplicity) but keep to avoid changing the code copied from Linux.
    u32 idx = 0;

    struct pl330_thread *thrd = &pl330->channels[chan];
    struct _pl330_req *req = &thrd->req[idx];

    if (thrd->req[idx].desc) {
        printf("DMA: channel %u busy\r\n", idx);
        return NULL;
    }

    void __iomem *regs = pl330->base;

    printf("DMA %s: INS %08x\r\n", pl330->name, readl(regs + CR3));

    struct dma_tx *tx = OBJECT_ALLOC(txes);
    if (!tx)
        return NULL;
    tx->req = req;
    req->tx = tx;

    struct dma_pl330_desc *desc = &tx->desc;
    desc->px.src_addr = (u32)src;
    desc->px.dst_addr = (u32)dst;
    desc->px.bytes = sz;

    desc->rqcfg.dst_inc = 1;
    desc->rqcfg.src_inc = 1;
    desc->rqcfg.nonsecure = 0;
    desc->rqcfg.privileged = 1;
    desc->rqcfg.insnaccess = 1;
    desc->rqcfg.brst_len = BURST_LEN;
    desc->rqcfg.brst_size = BURST_SIZE_BITS;

    desc->rqcfg.dcctl = CCTRL0;
    desc->rqcfg.scctl = CCTRL0;
    desc->rqcfg.swap = SWAP_NO;

    desc->rqcfg.pcfg = &pl330->pcfg;

    desc->status = BUSY;

    desc->bytes_requested = sz; // TODO: unused
    desc->last = 1;             // TODO: unused

    desc->rqtype = DMA_MEM_TO_MEM;
    desc->peri = 0;

    u32 ccr = _prepare_ccr(&desc->rqcfg);

    thrd->ev = chan; // one-to-one thread-event allocation 

    struct _xfer_spec xs;
    xs.ccr = ccr;
    xs.desc = desc;

    /* First dry run to check if req is acceptable */
    int ret = _setup_req(pl330, 1, thrd, idx, &xs);
    if (ret < 0) {
        printf("DMA: failed to construct request\r\n");
        OBJECT_FREE(tx);
        return NULL;
    }

    if (ret > pl330->mcbufsz / 2) {
        printf("DMA: microcode buffer too small for req: %u > %u)\r\n",
               ret, pl330->mcbufsz / 2);
        OBJECT_FREE(tx);
        return NULL;
    }

    thrd->lstenq = idx; // TODO: unused
    thrd->req_running = idx;

    req->desc = desc;
    req->cb = cb;
    req->cb_arg = cb_arg;
    req->rc = -1;

    _setup_req(pl330, 0, thrd, idx, &xs);

    u8 insn[6] = {0, 0, 0, 0, 0, 0};
    struct _arg_GO go;
    go.chan = 0;
    go.addr = (u32)thrd->req[idx].mc_bus;
    go.ns = 0; // all users of this driver run in secure mode
    _emit_GO(0, insn, &go);

    /* Set to generate interrupts for SEV */
    writel(readl(regs + INTEN) | (1 << thrd->ev), regs + INTEN);

    _execute_DBGINSN(thrd, insn, /* as manager */ true);
    return tx;
}

int dma_wait(struct dma_tx *tx)
{
    struct _pl330_req *req = tx->req;
    while (req->desc->status != DONE) {
        printf("DMA: waiting\r\n");
        // TODO: wfe with sev (otherwise there's a race here, but it's not a
        // problem as long as we have the watchdog timer, which will wake us up
        // in case of the race, i.e. sleep after the condition check.)
        asm("wfi");
    }
    int rc = req->rc;
    req->desc = NULL;
    req->tx = NULL;
    OBJECT_FREE(tx);
    printf("DMA: completed: rc %u\r\n", rc);
    return rc;
}

void dma_abort_isr(struct dma *dma)
{
    struct pl330_dmac *pl330 = (struct pl330_dmac *)dma;
    void __iomem *regs = pl330->base;
    u32 val;

    printf("DMA %s: ISR: abort\r\n", pl330->name);

    val = readl(regs + FSM) & 0x1;

    if (val) {
        printf("DMA %s: ISR: abort: FSM %08x\r\n", pl330->name, val);
        _stop(pl330->manager);
    }

    val = readl(regs + FSC) & ((1 << pl330->pcfg.num_chan) - 1);
    if (val) {
        printf("DMA %s: ISR: abort: FSC %08x\r\n", pl330->name, val);
        int i = 0;
        while (i < pl330->pcfg.num_chan) { // TODO: could use CLZ instruction
            if (val & (1 << i)) {
	        struct pl330_thread *thrd = &pl330->channels[i];

                printf("DMA %s: ISR: abort: reset ch %d CS %x FTC %x\r\n",
                        pl330->name, i, readl(regs + CS(i)), readl(regs + FTC(i)));

                int active = thrd->req_running;
                if (active == -1) { // should not happen
                    printf("DMA %s: ISR: abort: ch %i not running\r\n", pl330->name, i);
                    continue;
                }
                struct _pl330_req *req = &thrd->req[active];

                _stop(thrd);

                req->rc = PL330_ERR_FAIL; // TODO: PL330_ERR_ABORT? (in ABORT ISR but no fault?)
                req->desc->status = DONE;
                thrd->req_running = -1;

                if (req->cb) {
                    req->cb(req->cb_arg, req->rc);
                    req->desc = NULL; // release request state
                } // else: channel busy until reaped by dma_wait
            }
            i++;
        }
    }
}

void dma_event_isr(struct dma *dma, unsigned ev)
{
    struct pl330_dmac *pl330 = (struct pl330_dmac *)dma;
    void __iomem *regs = pl330->base;
    int id, active;
    struct pl330_thread *thrd;

    printf("DMA %s: ISR: event %u\r\n", pl330->name, ev);

    u32 inten = readl(regs + INTEN);

    /* Clear the event */
    if (inten & (1 << ev))
        writel(1 << ev, regs + INTCLR);

    /* Disable the interrupt, for symmetry since we enable on tx setup */
    writel(readl(regs + INTEN) & ~(1 << ev), regs + INTEN);

    id = pl330->events[ev];
    thrd = &pl330->channels[id];

    active = thrd->req_running;
    if (active == -1) { // aborted, so req->rc and req_running was set in abort ISR
        printf("DMA %s: ISR: event %u: tx was aborted\r\n",
               pl330->name, ev);
        return;
    }

    /* Detach the req */
    struct _pl330_req *req = &thrd->req[active];
    req->rc = PL330_ERR_NONE;
    req->desc->status = DONE;
    thrd->req_running = -1;

    if (req->cb) {
        req->cb(req->cb_arg, req->rc);
        struct dma_tx *tx = req->tx;
        req->desc = NULL; // release request state
        req->tx = NULL;
        OBJECT_FREE(tx);
    } // else: channel busy until reaped by dma_wait
}

