#ifndef LINKS_H
#define LINKS_H

struct link;

/* panics on failure */
void links_init();
struct link *links_get_trch_mbox_link();

#endif /* LINKS_H */
