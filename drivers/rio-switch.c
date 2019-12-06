#include <stdint.h>

#include "console.h"
#include "panic.h"
#include "regops.h"
#include "rio-pkt.h"
#include "rio-dev.h"
#include "rio-ep.h"

#include "rio-switch.h"

/* The datasheet indexes the MSB as Bit 0 (see User Guide 1.1.1), whereas
 * in context of shifts in C, Bit 0 is LSB. However, the significance of bits
 * in a bitfield is the same in both cases: a bitfield starts at a less signficant
 * bit and ends in a more significat bit. This doesn't have anything to do with
 * endianness, and there's no conversion, no flipping being done here. There is
 * no abiguity about where the bitfields are in register words (unambigously
 * defined by LSB/MSB). The question is just cosmetically how we specify that
 * unabiguous location in the following field declaration. We could ignore the
 * indexing convention in the datasheet altogether, however then the literal
 * values used in the datashet vs in these field definitions would not be easily
 * cross-checkable, at a glance.  So, we adopt a pattern here such that the
 * 'start' bit index value (and width value) matches the tables in the
 * datasheet verbatim:
 *      32 -  start - width, width
 * For example, for bitfield in Table 120 "Priority":
 *    Datasheet:  Bits 2-3
 *    Field def:  32 - 2 - 2, 2
 *                     ^ this value is the same verbatim
 */

REG32(PORT_MAPPING_TABLE_ENTRY_0,   0x102000)
REG32(PORT_MAPPING_TABLE_ENTRY_255, 0x1023FC)
    FIELD(PORT_MAPPING_TABLE_ENTRY, VALID,     32 -  0 -  1, 1)
    FIELD(PORT_MAPPING_TABLE_ENTRY, TYPE,      32 -  1 -  2, 2)
    FIELD(PORT_MAPPING_TABLE_ENTRY, PORT_MAP,  32 - 24 -  8, 8)
#define PORT_MAPPING_TABLE_ENTRY_MASK \
    (R_PORT_MAPPING_TABLE_ENTRY_VALID_MASK | \
     R_PORT_MAPPING_TABLE_ENTRY_TYPE_MASK | \
     R_PORT_MAPPING_TABLE_ENTRY_PORT_MAP_MASK)

/* If local is true, then base is considered valid */
int rio_switch_init(struct rio_switch *s, const char *name, uintptr_t base,
        bool local)
{
    ASSERT(name);
    s->name = name;
    s->local = local;
    s->base = base;
    printf("RIO SW %s: created switch", s->name); 
    if (s->local)
        printf(" @0x%x\r\n", s->base);
    return 0;
}

void rio_switch_release(struct rio_switch *s)
{
    ASSERT(s);
    printf("RIO SW %s: destroy\r\n", s->name);
}

/* TODO: Q: what is the role of UNICAST/MULTICAST type, given bitmask? */
static uint32_t make_mapping_entry(enum rio_mapping_type type, uint8_t port_map)
{
    uint32_t map_entry = 0;
    map_entry = FIELD_DP32(map_entry, PORT_MAPPING_TABLE_ENTRY, VALID, 1);
    map_entry = FIELD_DP32(map_entry, PORT_MAPPING_TABLE_ENTRY, TYPE, type);
    map_entry = FIELD_DP32(map_entry, PORT_MAPPING_TABLE_ENTRY, PORT_MAP,
                           port_map);
    return map_entry;
}

static inline uint32_t mapping_reg_addr(rio_devid_t dest)
{
    return PORT_MAPPING_TABLE_ENTRY_0 + dest * sizeof(uint32_t);
}

void rio_switch_map_local(struct rio_switch *s, rio_devid_t dest,
                          enum rio_mapping_type type, uint8_t port_map)
{
    ASSERT(s);
    ASSERT(s->local);
    uint32_t map_entry = make_mapping_entry(type, port_map);
    RIO_REGBO_WRITE32(s->base, PORT_MAPPING_TABLE_ENTRY_0, dest, map_entry);
}

void rio_switch_unmap_local(struct rio_switch *s, rio_devid_t dest)
{
    ASSERT(s);
    ASSERT(s->local);
    RIO_REGBO_WRITE32(s->base, PORT_MAPPING_TABLE_ENTRY_0, dest, 0);
}

int rio_switch_map_remote(struct rio_ep *ep, rio_dest_t switch_dest,
                          rio_devid_t route_dest, enum rio_mapping_type type,
                          uint8_t port_map)
{
    uint32_t map_entry = make_mapping_entry(type, port_map);
    return rio_ep_write_csr32(ep, map_entry, switch_dest,
                              mapping_reg_addr(route_dest));
}

int rio_switch_unmap_remote(struct rio_ep *ep, rio_dest_t switch_dest,
                            rio_devid_t route_dest)
{
    return rio_ep_write_csr32(ep, 0, switch_dest, mapping_reg_addr(route_dest));
}
