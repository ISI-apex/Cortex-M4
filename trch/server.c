#include <stdint.h>
#include <unistd.h>

#include "boot.h"
#include "command.h"
#include "hwinfo.h"
#include "link.h"
#include "mailbox-link.h"
#include "panic.h"
#include "console.h"
#include "server.h"
#include "psci.h"

#define MAX_MBOX_LINKS          8

static struct link *links[MAX_MBOX_LINKS] = {0};

static int linkp_alloc(struct link *link)
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

int server_process(struct cmd *cmd, void *reply, size_t reply_sz)
{
    size_t i;
    int rc;
    uint8_t *reply_u8 = (uint8_t *)reply;
    ASSERT(reply_sz <= CMD_MSG_SZ);
    switch (cmd->msg[0]) {
        case CMD_NOP:
            printf("NOP ...\r\n");
            // do nothing and reply nothing command
            return 0;
        case CMD_PING:
            printf("PING ...\r\n");
            reply_u8[0] = CMD_PONG;
            for (i = 1; i < CMD_MSG_PAYLOAD_OFFSET && i < reply_sz; i++)
                reply_u8[i] = 0;
            for (i = CMD_MSG_PAYLOAD_OFFSET; i < cmd->len; i++)
                reply_u8[i] = cmd->msg[i];
            return cmd->len;
        case CMD_PONG:
            printf("PONG ...\r\n");
            return 0;
        case CMD_PSCI: {
#if CONFIG_CONSOLE
            printf("PSCI ...\r\n");
            uint32_t *action = (uint32_t *) &(cmd->msg[CMD_MSG_PAYLOAD_OFFSET]);
            printf("\t");
            for (i = 0; i < 6; ++i) {
                 printf("0x%x ", action[i]);
            }
            printf("\r\n");
#endif /* CONFIG_CONSOLE */
            return handle_psci(cmd, reply_u8, reply_sz);
        }
        case CMD_WATCHDOG_TIMEOUT: {
#if CONFIG_CONSOLE
            unsigned int cpu =
                *((unsigned int *)(&cmd->msg[CMD_MSG_PAYLOAD_OFFSET]));
            printf("WATCHDOG_TIMEOUT ...\r\n");
            printf("\tCPU = %u\r\n", cpu);
#endif /* CONFIG_CONSOLE */
            return 0;
        }
        case CMD_LIFECYCLE: {
#if CONFIG_CONSOLE
            struct cmd_lifecycle *pl =
                (struct cmd_lifecycle *)(&cmd->msg[CMD_MSG_PAYLOAD_OFFSET]);
            printf("LIFECYCLE ...\r\n");
            printf("\tstatus = %s\r\n", pl->status ? "DOWN" : "UP");
            printf("\tinfo = '%s'\r\n", pl->info);
#endif /* CONFIG_CONSOLE */
            return 0;
        }
        case CMD_ACTION: {
            uint8_t action = cmd->msg[CMD_MSG_PAYLOAD_OFFSET];
            printf("ACTION ...\r\n");
            switch (action) {
                case CMD_ACTION_RESET_HPPS:
                    printf("\tRESET_HPPS ...\r\n");
                    boot_request(SUBSYS_HPPS);
                    break;
                default:
                    printf("\tUnknown action: %u\r\n", action);
                    return -1;
            }
            return 0;
        }
        case CMD_MBOX_LINK_CONNECT: {
            struct cmd_mbox_link_connect *pl =
                (struct cmd_mbox_link_connect *)(&cmd->msg[CMD_MSG_PAYLOAD_OFFSET]);
            printf("MBOX_LINK_CONNECT ...\r\n");
            printf("\tmbox_dev_idx = %u\r\n", pl->mbox_dev_idx);
            printf("\tindex_from = %u\r\n", pl->idx_from);
            printf("\tindex_to = %u\r\n", pl->idx_to);

            if (pl->mbox_dev_idx >= MBOX_DEV_COUNT) {
                reply_u8[0] = -1;
                return 1;
            }
            struct mbox_link_dev *mldev = mbox_link_dev_get(pl->mbox_dev_idx);
            if (!mldev) {
                // requested a device that isn't initialized
                reply_u8[0] = -2;
                return 1;
            }
            struct link *link = mbox_link_connect("CMD_MBOX_LINK", mldev,
                            pl->idx_from, pl->idx_to,
                            /* server */ 0, /* client */ MASTER_ID_TRCH_CPU);
            if (!link) {
                rc = -2;
            } else {
                rc = linkp_alloc(link);
            }
            printf("link connect rc: %u\r\n", rc);
            // TODO: Use a real return status message with its own message type
            reply_u8[0] = rc;
            return 1;
        }
        case CMD_MBOX_LINK_DISCONNECT: {
            printf("MBOX_LINK_DISCONNECT ...\r\n");
            uint8_t index = cmd->msg[CMD_MSG_PAYLOAD_OFFSET];
            if (index >= MAX_MBOX_LINKS) {
                rc = -1;
            } else {
                printf("link disconnect index: %u\r\n", index);
                rc = links[index]->disconnect(links[index]);
                linkp_free(index);
            }
            printf("link disconnect rc: %u\r\n", rc);
            // TODO: Use a real return status message with its own message type
            reply_u8[0] = rc;
            return 1;
        }
        case CMD_MBOX_LINK_PING: {
            uint8_t index = cmd->msg[CMD_MSG_PAYLOAD_OFFSET];
            printf("MBOX_LINK_PING ...\r\n");
            printf("\tindex = %u\r\n", index);
            if (index >= MAX_MBOX_LINKS) {
                reply_u8[0] = -1;
                return 1;
            }

            struct link *link = links[index];
            uint8_t msg[] = { CMD_PING, 0, 0, 0, 43 };
            uint8_t reqr[8];
            printf("request: cmd %x arg %x..\r\n",
                   msg[0], msg[CMD_MSG_PAYLOAD_OFFSET]);
            rc = link->request(link,
                               CMD_TIMEOUT_MS_SEND, msg, sizeof(msg),
                               CMD_TIMEOUT_MS_RECV, reqr, sizeof(reqr));
            if (rc <= 0) {
                reply_u8[0] = -2;
                return 1;
            }
            // TODO: Use a real return status message with its own message type
            reply_u8[0] = 0;
            return 1;
        }
        default:
            printf("ERROR: unknown cmd: %x\r\n", cmd->msg[0]);
            return -1;
    }
}
