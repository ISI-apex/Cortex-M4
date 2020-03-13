#include <stdint.h>

#include "console.h"
#include "mem-map.h"
#include "test.h"

int test_rt_mmu()
{
    // Translated by MMU via identity map (in HPPS LOW DRAM)
    volatile uint32_t *addr = (volatile uint32_t *)RT_MMU_TEST_DATA_LO_1_ADDR;
    uint32_t valref = 0xf00dcafe;
    printf("%p <- %08x\r\n", addr, valref);
    *addr = valref;
    uint32_t val = *addr;
    if (val != valref) {
        printf("mmu test: value at %p differs from written value: %x != %x\r\n",
               addr, val, valref);
        return 1;
    }
    printf("%p -> %08x\r\n", addr, val);

    // Translated by MMU (test configured to HPPS HIGH DRAM, 0x100000000)
    addr = (volatile uint32_t *)RT_MMU_TEST_DATA_HI_0_WIN_ADDR;

    valref = 0xbeeff00d; // written test code on TRCH
    val = *addr;
    printf("%p -> %08x\r\n", addr, val);
    if (val != valref) {
        printf("mmu test: value at %p differs from expected value: %x != %x\r\n",
               addr, val, valref);
        return 1;
    }

    valref = 0xdeadbeef;
    printf("%p <- %08x\r\n", addr, valref);
    *addr = valref;
    val = *addr;
    if (val != valref) {
        printf("mmu test: value at %p differs from written value: %x != %x\r\n",
               addr, val, valref);
        return 1;
    }
    printf("%p -> %08x\r\n", addr, val);

    // Write TRCH's value back, so that this test is idempotent (useful with reboots)
    valref = 0xbeeff00d;
    printf("%p <- %08x\r\n", addr, valref);
    *addr = valref;
    val = *addr;
    if (val != valref) {
        printf("mmu test: value at %p differs from written value: %x != %x\r\n",
               addr, val, valref);
        return 1;
    }
    printf("%p -> %08x\r\n", addr, val);

    return 0;
}
