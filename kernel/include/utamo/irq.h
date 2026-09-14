/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_IRQ_H
#define UTAMO_IRQ_H
#include <stdint.h>
/* Entry with IF=0 on BSP. No logging, allocation, waits, or nested IRQs. */
void irq_dispatch(uint8_t irq);
#endif
