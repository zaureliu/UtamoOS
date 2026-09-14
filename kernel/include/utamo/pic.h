/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PIC_H
#define UTAMO_PIC_H
#include <stdbool.h>
#include <stdint.h>
#define UTAMO_PIC_VECTOR_BASE 32u
#define UTAMO_PIC_IRQ_COUNT 16u
void pic_init(void);
/* Fixed mapping 0x20/0x28, all IRQs masked, preserves caller IF. */
void pic_remap(void);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
/* Called with IF=0 from IRQ dispatch; filters spurious IRQ7/15. */
bool pic_begin_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);
#endif
