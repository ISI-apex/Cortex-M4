#ifndef LINKS_H
#define LINKS_H

struct link;

/* panics on failure */
void links_init();

#if CONFIG_RTPS_TRCH_MAILBOX
struct link *links_get_trch_mbox_link();
#endif /* CONFIG_RTPS_TRCH_MAILBOX */

#endif /* LINKS_H */
