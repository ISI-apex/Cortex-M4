#ifndef MAILBOX_H
#define MAILBOX_H

#define MBOX_IRQ 162 /* External IRQ numbering (i.e. vector #16 has index 0). */

#define MBOX_BASE 0xF9240000

#define MBOX_REG_MAIL0_READ 0x80
#define MBOX_REG_MAIL1_READ 0xA0

#endif // MAILBOX_H
