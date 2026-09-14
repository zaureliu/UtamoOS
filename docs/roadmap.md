# Roadmap

Versões indicam marcos técnicos, não prazos. v0.0.1/v0.1.0 são baselines
preservados; v0.1.0 tem aceite manual registrado. O v0.2 implementa PMM/VMM
sobre essa arquitetura; sua validação host/ELF/QEMU está registrada no development log.
Heap v0.3 e threads/scheduler v0.4 passaram gates locais da campanha Astra.
Processos v0.5 e VFS/ELF v0.6 também passaram gates locais.
Marcos a partir de v0.7 são planos, sujeitos às evidências.

| Marco | Entrega | Estado / critério de saída |
| --- | --- | --- |
| **v0.0.1** | Boot, framebuffer, terminal, logging, serial, panic e mapa físico | Baseline validado; tag preservado |
| **v0.1.0** | GDT/TSS/IST, IDT, exceções, PIC/PIT, PS/2 e shell de kernel | Host/ELF/QEMU headless aprovados; aceite manual QEMU/VNC confirmado em 2026-09-14 |
| **v0.2.0** | Gerenciamento físico e virtual de memória | PMM, HHDM, VMM sobre CR3 herdado, permissões e selftests; host/ELF/QEMU aprovados, 14.213 checks sem falhas; revisão gráfica/teclado físico separada |
| **v0.3.0** | Kernel heap, `kmalloc`, `kfree` e diagnóstico de memória | GREEN local: 23.679 checks sem falhas, 14 VMs recolhidas; calloc/realloc, crescimento e rollback |
| **v0.4.0** | Threads, context switching e scheduler | GREEN local: 31.681 checks sem falhas; 18 VMs em fases candidata/final; quantum, guards, sleep, reap e registradores exercitados |
| **v0.5.0** | Ring 3, processos e syscalls | GREEN local: 40.620 checks; isolamento e fault containment |
| **v0.6.0** | VFS, initramfs e ELF userspace | GREEN local: 39.528 checks; init e programas reais |
| **v0.7.0** | PCI/PCIe e armazenamento | Enumerar dispositivos e ler imagens de teste com limites/DMA corretos |
| **v0.8.0** | Networking | Comunicação reproduzível e pacotes malformados rejeitados |
| **v0.9.0** | Gráficos e window system experimental | Input, superfícies e ownership definidos; falhas de clientes isoladas |
| **v1.0.0** | Baseline experimental estabilizado | Arquitetura/documentação estáveis, plataformas testadas e limitações publicadas |

## Próximo milestone: v0.7.0 — PCI e armazenamento

Descobrir PCI/BARs, definir block devices, implementar leitura AHCI com
ownership DMA explícito e validar FAT32 somente leitura em imagens descartáveis.
O gate exige corrupção/bounds rejeitados e leitura real no QEMU q35.
A base preservada está em [campaign state](astra-campaign-state.md).

## Etapas complementares

ACPI/RSDP, descoberta MADT e APIC/IOAPIC exigem validação de tabelas e
fallback documentado; não estão implementados. Sua posição no plano pode
ser revista sem antecipá-los ao PMM/VMM.

SMP depende de estado por CPU, sincronização e TLB shootdown. Será um marco
separado depois de contratos adequados no PMM, IRQ, log e scheduler, sem
confundir o código single-core atual com suporte multiprocessado.

A shell v0.1.0 é integrada ao kernel. A shell de userspace, ABI, tratamento
de ponteiros inválidos e uma libc parcial dependem de processos e syscalls.
FAT32 pode começar sobre uma imagem em RAM após VFS; acesso persistente
depende de armazenamento. Escritas devem começar em imagens descartáveis,
com integridade e recuperação de erros verificadas.

## Decomposição proposta de networking

1. Interface Ethernet, RX/TX, buffers, endereços MAC e limites de MTU.
2. Ethernet e ARP com validação de tamanho e expiração de cache.
3. IPv4 e ICMP, checksums e política explícita de fragmentação/reassembly.
4. UDP e sockets mínimos com quotas e timeouts.
5. DHCP e DNS com parsers limitados.
6. TCP com máquina de estados, retransmissão, janelas, fechamento e testes
   de perda.

O desenvolvimento usará rede virtual isolada. Cada camada precisa de
parsing, estados e falhas observáveis antes de ser anunciada como funcional.

## Limites do plano

Não há promessa de POSIX completo, compatibilidade binária Linux, suporte
a hardware arbitrário, aceleração gráfica, Secure Boot ou uso de produção.
Mesmo v1.0.0 será um baseline experimental com escopo de validação explícito.

O antigo marco v0.0.2 de validação foi absorvido pelo baseline confirmado
antes da evolução direta para v0.1.0. Decisões históricas permanecem no
[development log](development-log.md).
