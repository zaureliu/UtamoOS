# Roadmap

Versões indicam marcos técnicos, não prazos. v0.0.1 é o baseline de boot
validado; v0.1.0 acrescenta interrupções, teclado e shell, com testes
automatizados e aceite manual registrado. Todos os marcos a partir de
v0.2.0 são planos, sujeitos a revisão conforme dependências e evidências.

| Marco | Entrega | Estado / critério de saída |
| --- | --- | --- |
| **v0.0.1** | Boot, framebuffer, terminal, logging, serial, panic e mapa físico | Baseline validado; tag preservado |
| **v0.1.0** | GDT/TSS/IST, IDT, exceções, PIC/PIT, PS/2 e shell de kernel | Host/ELF/QEMU headless aprovados; aceite manual QEMU/VNC confirmado em 2026-09-14 |
| **v0.2.0** | Physical and virtual memory management | PMM com reservas explícitas, alocação/liberação sem dupla posse e page tables próprias |
| **v0.3.0** | Kernel heap, `kmalloc`, `kfree` e diagnóstico de memória | Alinhamento, OOM, double-free, overflow e fragmentação exercitados |
| **v0.4.0** | Threads, context switching e scheduler | Trocas repetidas preservam registradores/pilhas; idle funciona |
| **v0.5.0** | Ring 3, processos, ELF loader e syscalls | Memória de userspace isolada; ABI e cópias user/kernel documentadas |
| **v0.6.0** | VFS, userspace, initramfs e filesystem em RAM | Parsers e operações por handles validados; utilitários executam como processos |
| **v0.7.0** | PCI/PCIe e armazenamento | Enumerar dispositivos e ler imagens de teste com limites/DMA corretos |
| **v0.8.0** | Networking | Comunicação reproduzível e pacotes malformados rejeitados |
| **v0.9.0** | Gráficos e window system experimental | Input, superfícies e ownership definidos; falhas de clientes isoladas |
| **v1.0.0** | Baseline experimental estabilizado | Arquitetura/documentação estáveis, plataformas testadas e limitações publicadas |

## Próximo milestone: v0.2.0

1. Registrar os mappings herdados, endereço físico/virtual do kernel e todas
   as reservas antes de disponibilizar frames.
2. Implementar PMM com bitmap, começando com todas as páginas ocupadas e
   liberando apenas regiões utilizáveis elegíveis.
3. Criar page tables próprias, permissões por mapping e guard pages.
4. Remover dependências do bootloader antes de recuperar suas páginas.
5. Exercitar limites, falta de memória, reservas e page faults esperados.

O [layout de memória](memory-layout.md) detalha as dependências atuais.

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
