#include <stdint.h>

#include "console.h"
#include "mailbox-link.h"
#include "mailbox-map.h"
#include "hwinfo.h"
#include "gic.h"
#include "command.h"
#include "panic.h"
#include "arm.h"
#include "link.h"
#include "test.h"

int test_rtps_trch_mailbox(struct link *trch_link)
{
    uint32_t arg[] = { CMD_PING, 42 };
    uint32_t reply[sizeof(arg) / sizeof(arg[0])] = {0};
    printf("arg len: %u\r\n", sizeof(arg) / sizeof(arg[0]));
    int rc = trch_link->request(trch_link,
                                CMD_TIMEOUT_MS_SEND, arg, sizeof(arg),
                                CMD_TIMEOUT_MS_RECV, reply, sizeof(reply));
    if (rc <= 0)
        return rc;
    return 0;
}
