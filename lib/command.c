#include <stdint.h>

#include "mailbox.h"
#include "mem.h"
#include "console.h"
#include "panic.h"

#include "command.h"

#define CMD_QUEUE_LEN 4
#define REPLY_SIZE CMD_MSG_SZ

static size_t cmdq_head = 0;
static size_t cmdq_tail = 0;
static struct cmd cmdq[CMD_QUEUE_LEN];

static cmd_handler_t *cmd_handler = NULL;

void cmd_handler_register(cmd_handler_t cb)
{
    cmd_handler = cb;
}

void cmd_handler_unregister()
{
    cmd_handler = NULL;
}

int cmd_enqueue(struct cmd *cmd)
{
    size_t i;

    if (cmdq_head + 1 % CMD_QUEUE_LEN == cmdq_tail) {
        printf("command: enqueue failed: queue full\r\n");
        return 1;
    }
    cmdq_head = (cmdq_head + 1) % CMD_QUEUE_LEN;

    // cmdq[cmdq_head] = *cmd; // can't because GCC inserts a memcpy
    cmdq[cmdq_head].link = cmd->link;
    for (i = 0; i < CMD_MSG_SZ; ++i)
        cmdq[cmdq_head].msg[i] = cmd->msg[i];

    printf("command: enqueue (tail %u head %u): cmd %u arg %u...\r\n",
           cmdq_tail, cmdq_head,
           cmdq[cmdq_head].msg[0], cmdq[cmdq_head].msg[CMD_MSG_PAYLOAD_OFFSET]);

    // TODO: SEV (to prevent race between queue check and WFE in main loop)

    return 0;
}

int cmd_dequeue(struct cmd *cmd)
{
    size_t i;

    if (cmdq_head == cmdq_tail)
        return 1;

    cmdq_tail = (cmdq_tail + 1) % CMD_QUEUE_LEN;

    // *cmd = cmdq[cmdq_tail].cmd; // can't because GCC inserts a memcpy
    cmd->link = cmdq[cmdq_tail].link;
    for (i = 0; i < CMD_MSG_SZ; ++i)
        cmd->msg[i] = cmdq[cmdq_tail].msg[i];
    printf("command: dequeue (tail %u head %u): cmd %u arg %u...\r\n",
           cmdq_tail, cmdq_head,
           cmdq[cmdq_tail].msg[0], cmdq[cmdq_tail].msg[CMD_MSG_PAYLOAD_OFFSET]);
    return 0;
}

bool cmd_pending()
{
    return !(cmdq_head == cmdq_tail);
}

void cmd_handle(struct cmd *cmd)
{
    uint8_t reply[REPLY_SIZE];
    int reply_sz;
    size_t rc;

    ASSERT(cmd);
    printf("command: handle: cmd %u arg %u...\r\n",
           cmd->msg[0], cmd->msg[CMD_MSG_PAYLOAD_OFFSET]);

    if (!cmd_handler) {
        printf("command: handle: no handler registered\r\n");
        return;
    }

    bzero(reply, sizeof(reply));
    reply_sz = cmd_handler(cmd, reply, sizeof(reply));
    if (reply_sz < 0) {
        printf("ERROR: command: handle: server failed to process request\r\n");
        return;
    }
    if (!reply_sz) {
        printf("command: handle: server did not produce a reply\r\n");
        return;
    }

    ASSERT(cmd->link);
    printf("command: handle: %s: reply %u arg %u...\r\n", cmd->link->name,
           reply[0], reply[CMD_MSG_PAYLOAD_OFFSET]);

    rc = cmd->link->send(cmd->link, CMD_TIMEOUT_MS_REPLY, reply, sizeof(reply));
    if (rc)
        printf("command: handle: %s: reply sent and ACK'd\r\n", cmd->link->name);
    else
        printf("command: handle: %s: failed to send reply\r\n", cmd->link->name);
}
