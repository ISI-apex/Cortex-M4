#include "panic.h"

#include "list.h"

void list_insert(struct lnode *n, struct lnode *p)
{
    ASSERT(n);
    ASSERT(p);
    n->prev = p;
    n->next = p->next;
    if (n->next)
        n->next->prev = n;
    p->next = n;
}

void list_remove(struct lnode *n)
{
    ASSERT(n);
    ASSERT(n->prev);
    n->prev->next = n->next;
    if (n->next)
        n->next->prev = n->prev;
}
