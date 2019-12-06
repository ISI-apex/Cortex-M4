#include <stdint.h>
#include <unistd.h>

#include "console.h"
#include "subsys.h"
#include "syscfg.h"

#if CONFIG_CONSOLE
static const char *rtps_mode_name(unsigned m)
{
    switch (m) {
        case SYSCFG__RTPS_MODE__LOCKSTEP:   return "LOCKSTEP";
        case SYSCFG__RTPS_MODE__SMP:        return "SMP";
        case SYSCFG__RTPS_MODE__SPLIT:      return "SPLIT";
        default:                            return "?";
    };
}
#endif /* CONFIG_CONSOLE */

const char *memdev_name(enum memdev d)
{
    switch (d) {
        case MEMDEV_TRCH_SMC_SRAM:       return "TRCH_SMC_SRAM";
        case MEMDEV_TRCH_SMC_NAND:       return "TRCH_SMC_NAND";
        case MEMDEV_HPPS_SMC_SRAM:       return "HPPS_SMC_SRAM";
        case MEMDEV_HPPS_SMC_NAND:       return "HPPS_SMC_NAND";
        case MEMDEV_HPPS_DRAM:           return "HPPS_DRAM";
        case MEMDEV_RTPS_DRAM:           return "RTPS_DRAM";
        case MEMDEV_RTPS_TCM:            return "RTPS_TCM";
        case MEMDEV_TRCH_SRAM:           return "TRCH_SRAM";
        default:                         return "?";
    };
}

/* Note: we don't have to make copy; but copying keeps things simpler overall */
static ssize_t parse_slist(char *raw, size_t raw_sz,
                           const char **array, size_t array_sz,
                           const char *src)
{
    size_t strings = 0;
    size_t len = 0;
    const char *s = raw;
    while (*src != '\0') {
        if (len++ == raw_sz)
            return -1;
        *raw++ = *src++;
        if (*src == '\0') {
            *raw++ = *src++; /* copy the null */
            if (strings == array_sz)
                return -2;
            array[strings++] = s; /* add the string that just ended */
            s = raw; /* record start of the new string */
        }
    }
    array[strings] = NULL;
    return len;
}

static void print_str_array(const char **sa)
{
    for (int i = 0; sa[i]; ++i)
        printf("%s ", sa[i]);
}

void syscfg_print(struct syscfg *cfg)
{
    printf("SYSTEM CONFIG:\r\n"
           "\thave sfs offset:\t%u\r\n"
           "\tsfs offset:\t0x%x\r\n"
           "\tload_binaries:\t%u\r\n"
           "\tsubsystems:\t%s\r\n"
           "\trtps mode:\t%s\r\n"
           "\trtps cores bitmask:\t0x%x\r\n"
           "\trio: master: \t%u\r\n",
           cfg->have_sfs_offset, cfg->sfs_offset,
           cfg->load_binaries,
           subsys_name(cfg->subsystems),
           rtps_mode_name(cfg->rtps_mode), cfg->rtps_cores,
           cfg->rio.master);
    printf("\trtps r52 blobs: ");
    print_str_array(cfg->rtps_r52.blobs);
    printf("\r\n");
    printf("\trtps a53 blobs: ");
    print_str_array(cfg->rtps_a53.blobs);
    printf("\r\n");
    printf("\thpps blobs: ");
    print_str_array(cfg->hpps.blobs);
    printf("\r\n");
}

int syscfg_load(struct syscfg *cfg, uint8_t *addr)
{
    uint32_t *waddr = (uint32_t *)addr;
    uint32_t word0;
    ssize_t n;

    uint32_t *wp = waddr;

    word0 = *wp++;
    printf("SYSCFG: @%p word0: %x\r\n", addr, word0);

    /* TODO: use field macros from lib/ */
    cfg->rtps_mode = (word0 & SYSCFG__RTPS_MODE__MASK) >> SYSCFG__RTPS_MODE__SHIFT;
    cfg->rtps_cores = (word0 & SYSCFG__RTPS_CORES__MASK)
                            >> SYSCFG__RTPS_CORES__SHIFT;
    cfg->subsystems = (word0 & SYSCFG__SUBSYS__MASK) >> SYSCFG__SUBSYS__SHIFT;
    cfg->hpps.rootfs_loc = (word0 & SYSCFG__HPPS_ROOTFS_LOC__MASK)
                                >> SYSCFG__HPPS_ROOTFS_LOC__SHIFT;
    cfg->have_sfs_offset = (word0 & SYSCFG__HAVE_SFS_OFFSET__MASK)
                                >> SYSCFG__HAVE_SFS_OFFSET__SHIFT;
    cfg->load_binaries = (word0 & SYSCFG__LOAD_BINARIES__MASK)
                                >> SYSCFG__LOAD_BINARIES__SHIFT;
    cfg->sfs_offset = *wp++;

    n = parse_slist(cfg->rtps_r52.blobs_raw, sizeof(cfg->rtps_r52.blobs_raw),
            cfg->rtps_r52.blobs, sizeof(cfg->rtps_r52.blobs) / sizeof(char *),
            (const char *)(waddr + SYSCFG__RTPS_R52_BLOBS__WORD));
    if (n < 0)
        return 1;
    n = parse_slist(cfg->rtps_a53.blobs_raw, sizeof(cfg->rtps_a53.blobs_raw),
            cfg->rtps_a53.blobs, sizeof(cfg->rtps_a53.blobs) / sizeof(char *),
            (const char *)(waddr + SYSCFG__RTPS_A53_BLOBS__WORD));
    if (n < 0)
        return 2;
    n = parse_slist(cfg->hpps.blobs_raw, sizeof(cfg->hpps.blobs_raw),
            cfg->hpps.blobs, sizeof(cfg->hpps.blobs) / sizeof(char *),
            (const char *)(waddr + SYSCFG__HPPS_BLOBS__WORD));
    if (n < 0)
        return 3;

    cfg->rio.master = (word0 & SYSCFG__RIO_MASTER__MASK)
                        >> SYSCFG__RIO_MASTER__SHIFT;
    cfg->test.rio_onchip = (word0 & SYSCFG__TEST_RIO_ONCHIP__MASK)
                                >> SYSCFG__TEST_RIO_ONCHIP__SHIFT;
    cfg->test.rio_offchip = (word0 & SYSCFG__TEST_RIO_OFFCHIP__MASK)
                                >> SYSCFG__TEST_RIO_OFFCHIP__SHIFT;

    syscfg_print(cfg);
    return 0;
}
