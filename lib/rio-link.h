#ifndef LIB_RIO_LINK_H
#define LIB_RIO_LINK_H

#include <stdbool.h>

#include "link.h"
#include "rio-ep.h"

struct link *rio_link_connect(const char *name, bool is_server,
                              struct rio_ep *ep,
                              unsigned mbox_local, unsigned letter_local,
                              rio_devid_t dest,
                              unsigned mbox_remote, unsigned letter_remote,
                              unsigned seg_size);
/* disconnect is a method on the object */

#endif /* LIB_RIO_LINK_H */
