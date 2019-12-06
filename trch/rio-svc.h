#ifndef RIO_SVC_H
#define RIO_SVC_H

#include <stdbool.h>

#include "rio-ep.h"
#include "rio-switch.h"

#define RIO_SVC_NUM_ENDPOINTS 2

struct rio_svc {
    struct rio_switch sw;
    struct rio_ep eps[RIO_SVC_NUM_ENDPOINTS];
};

int rio_svc_init(struct rio_svc *s, bool master);
void rio_svc_destroy(struct rio_svc *svc);

#endif /* RIO_SVC_H */
