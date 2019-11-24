#ifndef LIB_LIST_H
#define LIB_LIST_H

struct lnode {
    struct lnode *prev;
    struct lnode *next;
};

void list_insert(struct lnode *n, struct lnode *p);
void list_remove(struct lnode *n);

#endif /* LIB_LIST_H */
