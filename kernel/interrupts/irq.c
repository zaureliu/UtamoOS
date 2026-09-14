/* SPDX-License-Identifier: MIT */
#include <utamo/irq.h>
#include <utamo/pic.h>
#include <utamo/pit.h>
#include <utamo/keyboard.h>
void irq_dispatch(uint8_t irq)
{
    if (!pic_begin_irq(irq)) {
        return;
    }
    /* Undriven sources remain masked, including unexpected real IRQ7/15. */
    if (irq == 0u) {
        pit_on_irq();
    } else if (irq == 1u) {
        keyboard_on_irq();
    } else {
        pic_mask(irq);
    }
    pic_send_eoi(irq);
}
