/* SPDX-License-Identifier: MIT */
#include <utamo/cpu.h>
#include <utamo/scheduler.h>
#include <utamo/irq.h>
#include <utamo/pic.h>
#include <utamo/interrupts.h>
#include <utamo/serial.h>
#include <utamo/terminal.h>
#include <utamo/vmm.h>

uint64_t exception_read_cr2(void);

static struct terminal *exception_terminal;
/* BSP only. IRQ gates clear IF; NMI/fault nesting can still occur. */
static volatile bool exception_active;
static volatile bool recursive_active;

void exception_set_terminal(struct terminal *term)
{
    exception_terminal = term;
}

struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame)
{
    cpu_disable_interrupts();
    if (frame->vector >= UTAMO_PIC_VECTOR_BASE &&
        frame->vector < UTAMO_PIC_VECTOR_BASE + UTAMO_PIC_IRQ_COUNT) {
        irq_dispatch((uint8_t)(frame->vector - UTAMO_PIC_VECTOR_BASE));
        return scheduler_on_interrupt(frame);
    }
    if (frame->vector == UTAMO_SCHEDULE_VECTOR) {
        return scheduler_on_interrupt(frame);
    }
    if (exception_active) {
        if (!recursive_active) {
            recursive_active = true;
            (void)serial_write_string("\nUTAMO: recursive CPU exception; halted\n");
        }
        cpu_halt();
    }
    exception_active = true;
    const uint64_t cr2 = frame->vector == 14u ? exception_read_cr2() : 0u;

    /* Complete serial report FIRST: a bad framebuffer cannot truncate it. */
    exception_format(serial_sink, NULL, frame, cr2);
    /*
     * Walk only AFTER the complete serial register dump. Query is read-only,
     * nonallocating, and validates table frames before HHDM access. A corrupt
     * translation causing a nested exception still leaves that first report.
     */
    struct vmm_mapping mapping = {0};
    bool mapping_available = false;
    if (frame->vector == 14u) {
        mapping_available = vmm_query_page(cr2, &mapping);
        exception_format_memory(serial_sink, NULL, mapping_available, &mapping);
    }
    if (exception_terminal != NULL) {
        struct framebuffer *const fb = exception_terminal->framebuffer;
        if (terminal_init(exception_terminal, fb)) {
            exception_format(terminal_sink, exception_terminal, frame, cr2);
            if (frame->vector == 14u) {
                exception_format_memory(terminal_sink, exception_terminal,
                                        mapping_available, &mapping);
            }
        }
    }
    cpu_halt();
}
