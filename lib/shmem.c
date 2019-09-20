#include <stdbool.h>
#include <stdint.h>

#include "mem.h"
#include "object.h"
#include "panic.h"
#include "shmem.h"

#define MAX_SHMEMS 16

struct shmem {
    struct object obj;
    volatile struct hpsc_shmem_region *shm;
};

static struct shmem shmems[MAX_SHMEMS] = {0};

#define IS_ALIGNED(p) (((uintptr_t)(const void *)(p) % sizeof(uint32_t)) == 0)

struct shmem *shmem_open(volatile void *addr)
{
    struct shmem *s = OBJECT_ALLOC(shmems);
    ASSERT(IS_ALIGNED(addr));
    if (s)
        s->shm = (volatile struct hpsc_shmem_region *)addr;
    return s;
}

void shmem_close(struct shmem *s)
{
    OBJECT_FREE(s);
}

size_t shmem_send(struct shmem *s, void *msg, size_t sz)
{
    size_t sz_rem = SHMEM_MSG_SIZE - sz;
    ASSERT(IS_ALIGNED(msg));
    ASSERT(sz <= SHMEM_MSG_SIZE);
    vmem_cpy(s->shm->data, msg, sz);
    if (sz_rem)
        vmem_set(s->shm->data + sz, 0, sz_rem);
    s->shm->status |= HPSC_SHMEM_STATUS_BIT_NEW;
    return sz;
}

uint32_t shmem_get_status(struct shmem *s)
{
    return s->shm->status;
}

bool shmem_is_new(struct shmem *s)
{
    return s->shm->status & HPSC_SHMEM_STATUS_BIT_NEW;
}

bool shmem_is_ack(struct shmem *s)
{
    return s->shm->status & HPSC_SHMEM_STATUS_BIT_ACK;
}

void shmem_clear_ack(struct shmem *s)
{
    s->shm->status &= ~HPSC_SHMEM_STATUS_BIT_ACK;
}

size_t shmem_recv(struct shmem *s, void *msg, size_t sz)
{
    ASSERT(sz >= SHMEM_MSG_SIZE);
    ASSERT(IS_ALIGNED(msg));
    mem_vcpy(msg, s->shm->data, SHMEM_MSG_SIZE);
    s->shm->status &= ~HPSC_SHMEM_STATUS_BIT_NEW; // clear new
    s->shm->status |= HPSC_SHMEM_STATUS_BIT_ACK; // set ACK
    return SHMEM_MSG_SIZE;
}
