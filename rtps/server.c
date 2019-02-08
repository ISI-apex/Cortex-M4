#include <stdint.h>
#include <unistd.h>

#include "command.h"
#include "mailbox-link.h"
#include "panic.h"
#include "printf.h"
#include "server.h"

int server_init(struct mbox_link_dev *devs, size_t ndevs)
{
    return 0;
}

int server_process(struct cmd *cmd, void *reply, size_t reply_sz)
{
    size_t i;
    uint8_t *reply_u8 = (uint8_t *)reply;
    ASSERT(reply_sz <= CMD_MSG_SZ);
    switch (cmd->msg[0]) {
        case CMD_NOP:
            // do nothing and reply nothing command
            return 0;
        case CMD_PING:
            printf("PING ...\r\n");
            reply_u8[0] = CMD_PONG;
            for (i = 1; i < CMD_MSG_PAYLOAD_OFFSET && i < reply_sz; i++)
                reply_u8[i] = 0;
            for (i = CMD_MSG_PAYLOAD_OFFSET; i < reply_sz; i++)
                reply_u8[i] = cmd->msg[i];
            return reply_sz;
        case CMD_PONG:
            printf("PONG ...\r\n");
            return 0;
        default:
            printf("ERROR: unknown cmd: %x\r\n", cmd->msg[0]);
            return -1;
    }
}
