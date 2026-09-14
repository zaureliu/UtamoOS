# Drivers

`video/` implementa framebuffer e terminal próprios. COM1 reside em
`arch/x86_64/serial.c` porque ainda é um dispositivo de bootstrap fixo, acessado
por portas x86. No futuro, descoberta ACPI/PCI, IRQs, DMA, dispositivos de bloco,
input e rede terão contratos próprios. Não há PCI, AHCI ou NVMe implementados.
