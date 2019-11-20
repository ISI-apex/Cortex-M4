#ifndef LIB_EVENT_H
#define LIB_EVENT_H

#include <unistd.h>
#include <stdbool.h>

#define EV_QUEUE_LEN 16

struct ev_actor {
    const char *name;
    void (*func)(struct ev_actor *a, struct ev_actor *sender, void *event);
};

struct ev_slot {
    struct ev_actor *actor;
    struct ev_actor *sender; /* may be NULL */
    void *event;
};

struct ev_loop {
    const char *name;
    size_t evq_head;
    size_t evq_tail;
    struct ev_slot evq[EV_QUEUE_LEN];
};

void ev_loop_init(struct ev_loop *el, const char *name);
/* returns non-zero if no events */
int ev_loop_process(struct ev_loop *el);
bool ev_loop_pending(struct ev_loop *el);
void ev_post(struct ev_loop *el, struct ev_actor *sender,
        struct ev_actor *act, void *event);

#endif /* LIB_EVENT_H */
