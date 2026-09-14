/* SPDX-License-Identifier: MIT */
#include <utamo/cpu.h>
#include <utamo/io.h>
#include <utamo/pic.h>
#define UTAMO_PIC_MASTER UINT16_C(0x20)
#define UTAMO_PIC_SLAVE UINT16_C(0xa0)
static uint8_t master_mask = 0xff;
static uint8_t slave_mask = 0xff;
static void pic_write(uint16_t port, uint8_t value)
{
    io_out8(port, value);
    io_out8(0x80, 0); /* Legacy IO delay, no dependency on an active timer. */
}
void pic_remap(void)
{
    const uint64_t flags = cpu_irq_save();
    pic_write(UTAMO_PIC_MASTER + 1u, 0xff);
    pic_write(UTAMO_PIC_SLAVE + 1u, 0xff);
    pic_write(UTAMO_PIC_MASTER, 0x11); /* edge triggered, cascaded, ICW4 */
    pic_write(UTAMO_PIC_SLAVE, 0x11);
    pic_write(UTAMO_PIC_MASTER + 1u, 0x20);
    pic_write(UTAMO_PIC_SLAVE + 1u, 0x28);
    pic_write(UTAMO_PIC_MASTER + 1u, 0x04); /* slave on IRQ2 */
    pic_write(UTAMO_PIC_SLAVE + 1u, 0x02);
    pic_write(UTAMO_PIC_MASTER + 1u, 0x01); /* 8086, explicit EOI */
    pic_write(UTAMO_PIC_SLAVE + 1u, 0x01);
    master_mask = 0xff;
    slave_mask = 0xff;
    pic_write(UTAMO_PIC_MASTER + 1u, master_mask);
    pic_write(UTAMO_PIC_SLAVE + 1u, slave_mask);
    cpu_irq_restore(flags);
}
void pic_init(void)
{
    pic_remap();
}
void pic_mask(uint8_t irq)
{
    if (irq >= UTAMO_PIC_IRQ_COUNT) {
        return;
    }
    const uint64_t flags = cpu_irq_save();
    if (irq < 8u) {
        master_mask |= (uint8_t)(1u << irq);
    } else {
        slave_mask |= (uint8_t)(1u << (irq - 8u));
        io_out8(UTAMO_PIC_SLAVE + 1u, slave_mask);
        if (slave_mask == 0xffu) {
            master_mask |= 0x04u;
        }
    }
    io_out8(UTAMO_PIC_MASTER + 1u, master_mask);
    cpu_irq_restore(flags);
}
void pic_unmask(uint8_t irq)
{
    if (irq >= UTAMO_PIC_IRQ_COUNT) {
        return;
    }
    const uint64_t flags = cpu_irq_save();
    if (irq < 8u) {
        master_mask &= (uint8_t)~(1u << irq);
    } else {
        slave_mask &= (uint8_t)~(1u << (irq - 8u));
        master_mask &= (uint8_t)~0x04u;
        io_out8(UTAMO_PIC_SLAVE + 1u, slave_mask);
    }
    io_out8(UTAMO_PIC_MASTER + 1u, master_mask);
    cpu_irq_restore(flags);
}
bool pic_begin_irq(uint8_t irq)
{
    if (irq >= UTAMO_PIC_IRQ_COUNT) {
        return false;
    }
    if (irq == 7u) {
        io_out8(UTAMO_PIC_MASTER, 0x0b);
        return (io_in8(UTAMO_PIC_MASTER) & 0x80u) != 0;
    }
    if (irq == 15u) {
        io_out8(UTAMO_PIC_SLAVE, 0x0b);
        if ((io_in8(UTAMO_PIC_SLAVE) & 0x80u) == 0) {
            /* Master cascade was real; slave IRQ15 was spurious. */
            io_out8(UTAMO_PIC_MASTER, 0x20);
            return false;
        }
    }
    return true;
}
void pic_send_eoi(uint8_t irq)
{
    if (irq >= UTAMO_PIC_IRQ_COUNT) {
        return;
    }
    if (irq >= 8u) {
        io_out8(UTAMO_PIC_SLAVE, 0x20);
    }
    io_out8(UTAMO_PIC_MASTER, 0x20);
}
