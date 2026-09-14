# Drivers

- video/: framebuffer, terminal e fonte bitmap.
- timer/: PIT canal 0, nominal 100 Hz; IRQ0 atualiza ticks.
- input/: i8042/PS2, set 1; IRQ1 coloca bytes no buffer em kernel/input/.
- pci.c/pci_core.c: inventário PCI, configuração e descoberta de BARs.
- block.c e ahci.c/ahci_core.c: interface de blocos e leitura AHCI com DMA próprio.
- e1000.c/e1000_ring.c: NIC 82540EM, descritores RX/TX e polling limitado.

COM1 permanece em arch/x86_64/serial.c. Armazenamento e rede usam mappings
supervisor UC e reservam DMA após publicação. IRQs de dispositivo ficam
mascaradas; somente PIT e PS/2 têm handlers ativos. USB e mouse não estão
implementados. Veja [armazenamento](../../docs/storage.md),
[rede](../../docs/networking.md) e os [gates](../../docs/astra-campaign-state.md).
