# Roadmap

Versões indicam marcos técnicos, não prazos. v0.0.1/v0.1.0 são baselines
preservados; v0.1.0 tem aceite manual registrado. O v0.2 implementa PMM/VMM
sobre essa arquitetura; sua validação host/ELF/QEMU está registrada no development log.
Heap v0.3 e threads/scheduler v0.4 passaram gates locais da campanha Astra.
Marcos a partir de v0.5.0 são planos, sujeitos a revisão pelas evidências.

| Marco | Entrega | Estado / critério de saída |
| --- | --- | --- |
| **v0.0.1** | Boot, framebuffer, terminal, logging, serial, panic e mapa físico | Baseline validado; tag preservado |
| **v0.1.0** | GDT/TSS/IST, IDT, exceções, PIC/PIT, PS/2 e shell de kernel | Host/ELF/QEMU headless aprovados; aceite manual QEMU/VNC confirmado em 2026-09-14 |
| **v0.2.0** | Gerenciamento físico e virtual de memória | PMM, HHDM, VMM sobre CR3 herdado, permissões e selftests; host/ELF/QEMU aprovados, 14.213 checks sem falhas; revisão gráfica/teclado físico separada |
| **v0.3.0** | Kernel heap, `kmalloc`, `kfree` e diagnóstico de memória | GREEN local: 23.679 checks sem falhas, 14 VMs recolhidas; calloc/realloc, crescimento e rollback |
| **v0.4.0** | Threads, context switching e scheduler | GREEN local: 31.681 checks sem falhas; 18 VMs em fases candidata/final; quantum, guards, sleep, reap e registradores exercitados |
| **v0.5.0** | Ring 3, processos, ELF loader e syscalls | Memória de userspace isolada; ABI e cópias user/kernel documentadas |
| **v0.6.0** | VFS, userspace, initramfs e filesystem em RAM | Parsers e operações por handles validados; utilitários executam como processos |
| **v0.7.0** | PCI/PCIe e armazenamento | Enumerar dispositivos e ler imagens de teste com limites/DMA corretos |
| **v0.8.0** | Networking | Comunicação reproduzível e pacotes malformados rejeitados |
| **v0.9.0** | Gráficos e window system experimental | Input, superfícies e ownership definidos; falhas de clientes isoladas |
| **v1.0.0** | Baseline experimental estabilizado | Arquitetura/documentação estáveis, plataformas testadas e limitações publicadas |

## Próximo milestone: v0.5.0 — Ring 3, processos e syscalls

1. Definir ownership e teardown de address spaces isolados, mantendo o kernel
   protegido e compartilhado conforme um contrato explícito.
2. Ampliar GDT/TSS, entrada/saída de privilégio e frames sem quebrar threads
   de kernel, IRQs, guards ou a ABI de interrupções.
3. Definir syscalls, validação de ponteiros e cópias user/kernel limitadas;
   erros de processo não podem corromper nem encerrar o kernel.
4. Validar isolamento, lifecycle, argumentos inválidos e regressões por host,
   inspeção ELF e QEMU antes de promover o marco.

As bases GREEN estão em [memória](memory-management.md), [heap](heap.md) e
[scheduler](scheduler.md). [A evidência v0.4](validation-astra-v0.4.json)
distingue os 15 QEMU candidatos com stamp 0.3.0 dos três QEMU finais 0.4.0.
Reclaim de bootloader/ACPI e revisão dos aliases HHDM continuam separados;
as guard pages de threads não estabelecem W^X global.

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
