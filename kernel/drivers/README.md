# Drivers

- video/: framebuffer, terminal e fonte bitmap existentes.
- timer/: PIT canal 0, nominal 100 Hz; IRQ0 atualiza ticks.
- input/: i8042/PS2, set 1, IRQ1 coloca bytes no buffer em kernel/input/.

COM1 permanece em arch/x86_64/serial.c. Não há PCI, storage, USB, mouse ou rede.
