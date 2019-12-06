#ifndef LINKS_H
#define LINKS_H

#include "syscfg.h"

 /* panics on failure */
void links_init(enum rtps_mode rtps_mode);
int links_poll();

#endif /* LINKS_H */
