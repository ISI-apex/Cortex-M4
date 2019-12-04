#ifndef SHMEM_H
#define SHMEM_H

#include <stdbool.h>
#include <unistd.h>

// size aligns with mailbox messages
#define SHMEM_MSG_SIZE 64

// All subsystems must understand this structure and its protocol
#define HPSC_SHMEM_STATUS_BIT_NEW 0x01
#define HPSC_SHMEM_STATUS_BIT_ACK 0x02
struct hpsc_shmem_region {
    uint8_t data[SHMEM_MSG_SIZE];
    uint32_t status;
};

#define HPSC_SHMEM_REGION_SZ sizeof(struct hpsc_shmem_region)

struct shmem;

/**
 * Open a shared memory region.
 */
struct shmem *shmem_open(uintptr_t addr);

/**
 * Close a shared memory region.
 */
void shmem_close(struct shmem *s);

/**
 * Write data to the shared memory region.
 * Returns the number of bytes written
 */
size_t shmem_send(struct shmem *s, void *msg, size_t sz);

/**
 * Read data from the shared memory region.
 * Returns the number of bytes read
 */
size_t shmem_recv(struct shmem *s, void *msg, size_t sz);

/**
 * Read the entire status field so it can be parsed directly.
 */
uint32_t shmem_get_status(struct shmem *s);

/**
 * Returns true if the NEW status bit is set, false otherwise.
 */
bool shmem_is_new(struct shmem *s);

/**
 * Returns true if the ACK status bit is set, false otherwise.
 */
bool shmem_is_ack(struct shmem *s);

/**
 * Set or clear the NEW status bit.
 */
void shmem_set_new(struct shmem *s, bool val);

/**
 * Set or clear the ACK status bit.
 */
void shmem_set_ack(struct shmem *s, bool val);

#endif // SHMEM_H
