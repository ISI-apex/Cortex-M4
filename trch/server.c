#include <stdint.h>
#include <unistd.h>

#include "printf.h"
#include "reset.h"
#include "command.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "hwinfo.h"

#define ENDPOINT_HPPS 0
#define ENDPOINT_RTPS 1

#define MAX_MBOX_LINKS          8

static struct mbox_link *links[MAX_MBOX_LINKS] = {0};

static int linkp_alloc(struct mbox_link *link)
{
    size_t i = 0;
    while (links[i] && i < MAX_MBOX_LINKS)
        ++i;
    if (i == MAX_MBOX_LINKS)
        return -1;
    links[i] = link;
    return i;
}

static void linkp_free(int index)
{
     links[index] = NULL;
}

int server_process(struct cmd *cmd, uint32_t *reply, size_t reply_size)
{
    size_t i;
    switch (cmd->msg[0]) {
        case CMD_NOP:
            // do nothing and reply nothing command
            return 0;
        case CMD_PING:
            printf("PING ...\r\n");
            reply[0] = CMD_PONG;
            for (i = 1; i < CMD_MSG_LEN && i < reply_size; ++i)
                reply[i] = cmd->msg[i];
            return i;
        case CMD_PONG:
            printf("PONG ...\r\n");
            return 0;
        case CMD_WATCHDOG_TIMEOUT:
            printf("WATCHDOG_TIMEOUT ...\r\n");
            printf("\tCPU = %u\r\n", (unsigned int) cmd->msg[1]);
            return 0;
        case CMD_LIFECYCLE:
            printf("LIFECYCLE ...\r\n");
            printf("\tstatus = %s\r\n", cmd->msg[1] ? "DOWN" : "UP");
            printf("\tinfo = '%s'\r\n", (char*) &cmd->msg[2]);
            return 0;
        case CMD_RESET_HPPS:
            printf("RESET_HPPS ...\r\n");
            reset_component(COMPONENT_HPPS);
            reply[0] = 0;
            return 1;
        case CMD_MBOX_LINK_CONNECT: {
            printf("MBOX_LINK_CONNECT ...\r\n");
            int rc;
            volatile uint32_t *base;
            unsigned irq_base;
            unsigned rcv_int_idx, ack_int_idx;
            switch (cmd->msg[1]) {
                case ENDPOINT_RTPS:
                    base = MBOX_LSIO__BASE;
                    irq_base = MBOX_LSIO__IRQ_START;
                    rcv_int_idx = MBOX_LSIO__TRCH_RCV_INT;
                    ack_int_idx = MBOX_LSIO__TRCH_ACK_INT;
                    break;
                case ENDPOINT_HPPS:
                    base = MBOX_HPPS_TRCH__BASE;
                    irq_base = MBOX_HPPS_TRCH__IRQ_START;
                    rcv_int_idx = MBOX_HPPS_TRCH__TRCH_RCV_INT;
                    ack_int_idx = MBOX_HPPS_TRCH__TRCH_ACK_INT;
                    break;
                default:
                    reply[0] = -1;
                    return 1;
            }

            struct mbox_link *link = mbox_link_connect(base, irq_base,
                            /* from mbox */ cmd->msg[2], /* to mbox */ cmd->msg[3],
                            rcv_int_idx, ack_int_idx,
                            /* server */ 0, /* client */ MASTER_ID_TRCH_CPU);
            if (!link) {
                rc = -2;
            } else {
                rc = linkp_alloc(link);
            }
            printf("link connect rc: %u\r\n", rc);
            reply[0] = rc;
            return 1;
        }
        case CMD_MBOX_LINK_DISCONNECT: {
            printf("MBOX_LINK_DISCONNECT ...\r\n");
            int rc;
            unsigned index = cmd->msg[1];
            if (index >= MAX_MBOX_LINKS) {
                rc = -1;
            } else {
                printf("link disconnect index: %u\r\n", index);
                rc = mbox_link_disconnect(links[index]);
                linkp_free(index);
            }
            printf("link disconnect rc: %u\r\n", rc);
            reply[0] = rc;
            return 1;
        }
        case CMD_MBOX_LINK_PING: {
            printf("MBOX_LINK_PING ...\r\n");
            unsigned index = cmd->msg[1];
            if (index >= MAX_MBOX_LINKS) {
                reply[0] = -1;
                return 1;
            }

            struct mbox_link *link = links[index];
            uint32_t msg[] = { CMD_PING, 43 };
            uint32_t reply[1];
            int rc = mbox_link_request(link, msg, 2, reply, 1 + 1 /* cmd + arg */);
            if (rc) {
                reply[0] = -2;
                return 1;
            }

            reply[0] = 0;
            return 1;
        }
        default:
            printf("ERROR: unknown cmd: %x\r\n", cmd->msg[0]);
            return -1;
    }
}
