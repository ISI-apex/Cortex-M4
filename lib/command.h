#ifndef COMMAND_H
#define COMMAND_H

#include <stdbool.h>
#include <unistd.h>
#include "link.h"

#define CMD_MSG_SZ 64
#define CMD_MSG_PAYLOAD_OFFSET 4
#define CMD_MSG_PAYLOAD_SIZE (CMD_MSG_SZ - CMD_MSG_PAYLOAD_OFFSET)

#define CMD_NOP                         0
#define CMD_PING                        1
#define CMD_PONG                        2
#define CMD_PSCI                        3
#define CMD_WATCHDOG_TIMEOUT            11
#define CMD_LIFECYCLE                   13
#define CMD_ACTION                      14
#define CMD_MBOX_LINK_CONNECT           200
#define CMD_MBOX_LINK_DISCONNECT        201
#define CMD_MBOX_LINK_PING              202

#define CMD_ACTION_RESET_HPPS           1

#define CMD_TIMEOUT_MS_SEND 1000
#define CMD_TIMEOUT_MS_RECV 1000
// wait up to 30 seconds for replies - a timeout prevent hangs when remotes fail
#define CMD_TIMEOUT_MS_REPLY 30000

struct cmd {
    // the first byte of the message is the type, the next 3 bytes are reserved
    // the remainder of the msg is available for the payload
    uint8_t msg[CMD_MSG_SZ];
    unsigned len; /* the populated bytes in 'msg' */
    struct link *link;
};

struct cmd_lifecycle {
    uint32_t status;
    char info[CMD_MSG_PAYLOAD_SIZE - sizeof(uint32_t)];
};

struct cmd_mbox_link_connect {
    // TODO: The use of mbox_dev_idx means that the remote has more detailed
    // knowledge of our inner workings than should be allowed
    // It also means they have the endpoint hardcoded, when it should be dynamic
    // based on our boot configuration
    // Perhaps use CPU or IP Block ID instead, which we can map to a subsystem?
    uint8_t mbox_dev_idx;
    uint8_t idx_from;
    uint8_t idx_to;
};

typedef int (cmd_handler_t)(struct cmd *cmd, void *reply, size_t reply_sz);

void cmd_handler_register(cmd_handler_t *cb);
void cmd_handler_unregister();

void cmd_handle(struct cmd *cmd);

int cmd_enqueue(struct cmd *cmd);
int cmd_dequeue(struct cmd *cmd);
bool cmd_pending();

#endif // COMMAND_H
