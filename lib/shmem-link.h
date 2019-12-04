#ifndef SHMEM_LINK_H
#define SHMEM_LINK_H

#include "link.h"

struct link *shmem_link_connect(const char *name, uintptr_t addr_out,
                                uintptr_t addr_in);

#endif // SHMEM_LINK_H
