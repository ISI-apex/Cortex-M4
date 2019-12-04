#include "mem-map.h"
#include "console.h"
#include "shmem.h"

#include "test.h"

static char *msg = "Test Message";
// double the size just to make sure it works
static char buf[HPSC_SHMEM_REGION_SZ] = {0};
// a dummy shared memory region
static char shmem_reg[HPSC_SHMEM_REGION_SZ] = {0};

int test_shmem()
{
    size_t sz;
    int ret = 0;
    uint32_t status;
    struct shmem *shm = shmem_open((uintptr_t)shmem_reg);
    if (!shm)
        return 1;
    // no flags should be set at this point
    status = shmem_get_status(shm);
    if (status) {
        printf("ERROR: TEST: shmem: initial status failed\r\n");
        ret = 1;
        goto out;
    }
    sz = shmem_send(shm, msg, sizeof(msg));
    if (sz != sizeof(msg)) {
        printf("ERROR: TEST: shmem: send failed\r\n");
        ret = 1;
        goto out;
    }
    shmem_set_new(shm, true);
    // NEW flag should be set
    if (!shmem_is_new(shm)) {
        printf("ERROR: TEST: shmem: set NEW status failed\n");
        return 1;
    }
    sz = shmem_recv(shm, buf, sizeof(buf));
    if (sz != SHMEM_MSG_SIZE) {
        printf("ERROR: TEST: shmem: recv failed\r\n");
        ret = 1;
        goto out;
    }
    shmem_set_new(shm, false);
    // NEW flag should be off
    if (shmem_is_new(shm)) {
        printf("ERROR: TEST: shmem: clear NEW status failed\n");
        return 1;
    }
    shmem_set_ack(shm, true);
    // ACK flag should be set
    if (!shmem_is_ack(shm)) {
        printf("ERROR: TEST: shmem: set ACK status failed\n");
        return 1;
    }
    shmem_set_ack(shm, false);
    // no flags should be set anymore
    status = shmem_get_status(shm);
    if (status) {
        printf("ERROR: TEST: shmem: final status failed\n");
        ret = 1;
        goto out;
    }
    printf("TEST: shmem: success\r\n");

out:
    shmem_close(shm);
    return ret;
}
