#ifndef LIB_OOP_H
#define LIB_OOP_H

#define container_of(type, member, ptr) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})

#endif /* LIB_OOP_H */
