/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_KEYBOARD_H
#define UTAMO_KEYBOARD_H

#include <stdbool.h>

/* Boot only, IF=0 and IRQ1 masked. Leaves PIC policy to the caller. */
bool keyboard_init(void);
/* IRQ1 only. Reads one hardware byte; no formatting or line editing. */
void keyboard_on_irq(void);
/* Main only. Pop under saved IF=0; decoding occurs after IF restoration. */
bool keyboard_read_char(char *character);
bool keyboard_has_pending(void);

#endif
