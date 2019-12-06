#include "regops.h"
#include "bit.h"

/* TODO: confirm that HW in front of RIO IP block does not swap automatically */

#define RIO_BYTE_ORDER __ORDER_BIG_ENDIAN__ /* fixed by RIO HW IP block */

/* Note: there's no good way to make this generic (and hide it in regops.h): we
 * can't parametrize a #if pre-processor directive, and the alternative of
 * having to specify an endianness argument to each REG_*() call is too verbose,
 * especially given that very few IP blocks are expected to have a mismatched
 * endianness to the CPUs. So, we require each driver for each such module to
 * conditionally define macros like below. */
#if __BYTE_ORDER__ == RIO_BYTE_ORDER /* target CPU endianness matches */
#define RIO_REG_READ32 REG_READ32
#define RIO_REG_WRITE32 REG_WRITE32
#define RIO_REG_READ64 REG_READ64
#define RIO_REG_WRITE64 REG_WRITE64
#define RIO_REGB_READ32 REGB_READ32
#define RIO_REGB_WRITE32 REGB_WRITE32
#define RIO_REGBO_READ32 REGBO_READ32
#define RIO_REGBO_WRITE32 REGBO_WRITE32
#else /* target CPU endianness differs */
#define RIO_REG_READ32(reg) byte_swap32(REG_READ32(reg))
#define RIO_REG_WRITE32(reg, val) REG_WRITE32(reg, byte_swap32(val))
#define RIO_REG_READ64(reg) byte_swap64(REG_READ64(reg))
#define RIO_REG_WRITE64(reg, val) REG_WRITE64(reg, byte_swap64(val))
#define RIO_REGB_READ32(base, reg) byte_swap32(REGB_READ32(base, reg))
#define RIO_REGB_WRITE32(base, reg, val) \
        REGB_WRITE32(base, reg, byte_swap32(val))
#define RIO_REGBO_READ32(base, reg, offset) \
        byte_swap32(REGBO_READ32(base, reg, offset))
#define RIO_REGBO_WRITE32(base, reg, offset, val) \
        REGBO_WRITE32(base, reg, offset, byte_swap32(val))
/* Note: don't define RIO_REG*_F* field accessors -- cannot flip endianness */
#endif /* __BYTE_ORDER__ */
