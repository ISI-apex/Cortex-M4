#include <stdint.h>
#include <unistd.h>

#include "panic.h"
#include "command.h"
#include "sleep.h"
#include "pwrmgr.h"
#include "console.h"
#include "reset.h"

struct pm_state {
    uint32_t power_state;
    uint32_t addr_low;
    uint32_t addr_high;
};

static struct pm_state hpsc_nodes[PM_API_MAX];

static int pm_node_to_cpu_id(uint32_t pm_node_id, comp_t * cpu_id)
{
    int id;
    if (pm_node_id > NODE_RPU) {
        id = pm_node_id - NODE_RPU_0;
        if (id >= RTPS_R52_NUM_CORES + RTPS_A53_NUM_CORES) 
            return -1;
        *cpu_id = 0x1 << (COMP_CPUS_SHIFT_RTPS + id);
    } else if (pm_node_id >= NODE_APU_0) {
        id = pm_node_id - NODE_APU_0;
        if (id >= HPPS_NUM_CORES) return -1;
        *cpu_id = 0x1 << (COMP_CPUS_SHIFT_HPPS + id);
printf("%s: id(%d), cpu_id(0x%x)\r\n", __func__, id, *cpu_id);
    } else {
        return -1;
    }
    return 0;
}

static int pm_self_suspend(uint32_t sender, uint32_t * data)
{
    uint32_t target = data[0];
    uint32_t latency = data[1]; (void)latency;
    uint32_t state = data[2]; (void)state;
    uint32_t addr_low = data[3];
    uint32_t addr_high = data[4];
    comp_t cpu_id;
    printf("%s: sender(%d), target(%d), latency(%d), state(%d), addr_low(0x%x), addr_high(0x%x)\r\n", __func__, 
		sender, target, latency, state, addr_low, addr_high);
    if (addr_low & 0x1) { /* save address */
        ASSERT(target < sizeof(hpsc_nodes) / sizeof(hpsc_nodes[0]));
        hpsc_nodes[target].addr_low = addr_low;
        hpsc_nodes[target].addr_high = addr_high;
    }

    if (pm_node_to_cpu_id(target, &cpu_id) < 0) 
        return 0;
    mdelay(30);
    reset_assert(cpu_id);
    return 0;
}


static int pm_req_suspend(uint32_t sender, uint32_t * data)
{
    uint32_t target = data[0];
    uint32_t ack = data[1]; (void)ack;
    uint32_t latency = data[2]; (void)latency;
    uint32_t state = data[3]; (void)state;
    comp_t cpu_id;

    printf("%s: sender(%d), target(%d), latency(%d), ack(%d), state(0x%x)\r\n", __func__, 
		sender, target, latency, ack, state);
    if (pm_node_to_cpu_id(target, &cpu_id) < 0) 
        return 0;
    mdelay(30);
    reset_assert(cpu_id);

    return 0;
}

static int pm_req_wakeup(uint32_t sender, uint32_t * data)
{
    uint32_t target = data[0];
    uint32_t addr_low = data[1]; (void)addr_low;
    uint32_t addr_high = data[2]; (void)addr_high;
    uint32_t ack = data[3]; (void)ack;
    comp_t cpu_id;
    printf("%s: is called: target(%d), addr_low(0x%x), addr_high(0x%x), ack(%d)\r\n", 
		__func__, target, addr_low, addr_high, ack);

    if (pm_node_to_cpu_id(target, &cpu_id) < 0) 
        return 0;
    reset_release(cpu_id);
    return 0;
}

static int pm_system_shutdown(uint32_t sender, uint32_t *data)
{
    uint32_t type = data[0]; (void)type;
    uint32_t sub_type = data[1]; (void)sub_type;
    printf("%s: not implemented yet: type(%d), subtype(%d)\r\n", 
		__func__, type, sub_type);
    return 0;
}

#define PSCI_ARG_OFFSET 2
/* handle_psci()
   packet format:
      cmd->msg[0]: CMD_PSCI
      cmd->msg[1]: sender node id
      cmd->msg[2]: PSCI command
      cmd->msg[3..]: argument of the PSCI command
*/
int handle_psci(struct cmd * cmd, uint8_t *reply_buf, unsigned reply_sz)
{
    uint32_t *data, *arg_data;
    uint32_t sender;
    unsigned reply_len;
    int rc;
    unsigned req;
 
    data = (uint32_t *) &(cmd->msg[CMD_MSG_PAYLOAD_OFFSET]);
    /* TODO: do the work and return the right results */
    int i;
    for (i = 0; i < 7; ++i) {
        printf("0x%x ", data[i]);
    }
    printf("\r\n");
    sender = data[0];
    req = data[1];
    arg_data = &data[PSCI_ARG_OFFSET];
    switch (req) {
        case PM_REQ_SUSPEND:
            rc = pm_req_suspend(sender, arg_data);
            break;
        case PM_SELF_SUSPEND:
            rc = pm_self_suspend(sender, arg_data);
            break;
        case PM_REQ_WAKEUP:
            rc = pm_req_wakeup(sender, arg_data);
            break;
        case PM_SYSTEM_SHUTDOWN:
            rc = pm_system_shutdown(sender, arg_data);
            break;
	default:
            printf("WARNING: unknown PSCI request: 0x%x", req);
            rc = 1;
    }
    reply_len = 0;
    ASSERT(reply_sz > reply_len);
    /* TODO: hack for HPPS until mailbox code in ATF fixed to ACK reply */
    if (!(NODE_APU_0 <= sender && sender <= NODE_APU_7)) {
        reply_buf[reply_len++] = rc;
    }
    return reply_len;
}
