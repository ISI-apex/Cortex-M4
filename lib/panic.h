#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

#include "console.h"

/* Note: printing via "%s" to allow '%' in conditions */
#define ASSERT(cond) \
        if (!(cond)) { \
                printf("ASSERT: %s:%u: %s\r\n", __FILE__, __LINE__, #cond); \
                panic("ASSERT"); \
        }
#define PANIC(msg) do { \
    printf("PANIC: %s:%u\r\n", __FILE__, __LINE__); \
    panic(msg); \
} while (0);


// Define DEBUG to 1 in the source file that you want to debug
// before the #include statement for this header.
#ifndef DEBUG
#define DEBUG 0
#endif // undef DEBUG

#if DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

void panic(const char *msg);
void dump_buf(const char *name, uint32_t *buf, unsigned words);

#endif // PANIC_H
