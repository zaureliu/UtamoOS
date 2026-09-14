# Roadmap

Versões indicam marcos técnicos, não prazos. v0.0.1/v0.1.0 são baselines
preservados; v0.1.0 tem aceite manual registrado. Os gates locais da campanha
Astra aprovaram PMM/VMM, heap, threads, processos, VFS/ELF e armazenamento
até v0.8 core. A campanha está concluída; a referência de aceitação
e LAST_KNOWN_GOOD é o [estado da campanha](astra-campaign-state.md).

| Marco | Entrega | Estado / critério de saída |
| --- | --- | --- |
| **v0.0.1** | Boot, framebuffer, terminal, logging, serial, panic e mapa físico | Baseline validado; tag preservada |
| **v0.1.0** | GDT/TSS/IST, IDT, exceções, PIC/PIT, PS/2 e shell de kernel | Host/ELF/QEMU headless aprovados; aceite manual QEMU/VNC confirmado em 2026-09-14 |
| **v0.2.0** | Gerenciamento físico e virtual de memória | PMM, HHDM, VMM sobre CR3 herdado, permissões e selftests; baseline GREEN, revisão gráfica/teclado físico separada |
| **v0.3.0** | Kernel heap | GREEN local: 23.679 checks sem falhas; calloc/realloc, crescimento e rollback |
| **v0.4.0** | Threads, context switching e scheduler | GREEN local: 31.681 checks sem falhas; quantum, guards, sleep, reap e registradores |
| **v0.5.0** | Ring 3, processos e syscalls | GREEN local: 40.620 checks; isolamento e fault containment |
| **v0.6.0** | VFS, initramfs e ELF userspace | GREEN local: 39.528 checks; init e programas reais |
| **v0.7.0** | PCI e armazenamento | GREEN local: 44.543 checks; readonly AHCI/block/FAT32 e 37 VMs de gate recolhidas |
| **v0.8.0** | Networking core | GREEN local: 52.652 checks; E1000, Ethernet, ARP, IPv4, ICMP, UDP, DHCP e DNS |
| **v0.9.0** | Gráficos e window system experimental | Planejado fora desta campanha |
| **v1.0.0** | Baseline experimental estabilizado | Futuro; plataformas testadas e limitações explícitas |

Contagens são assertions das execuções registradas, não número de testes
únicos ou cobertura. Cada gate identifica seu próprio ELF/ISO e suas fases;
uma nova versão não herda aprovação por presença do código.

## Milestone concluído: v0.8.0 — networking core

A implementação e seus limites estão em [networking.md](networking.md).
O gate verifica NIC/DMA, pacotes reais, DHCP aprendido, ICMP, UDP e DNS com
fixtures locais, variantes de RAM/NX/subnet/MAC, falhas e regressões.
TCP e HTTP são opcionais e não estão implementados. A campanha concluiu
v0.8 core GREEN, auditoria e [relatório final](astra-campaign-final-report.md);
GUI não pertence à campanha.

## Etapas complementares

ACPI/RSDP, descoberta MADT e APIC/IOAPIC exigem validação de tabelas e
fallback documentado; não estão implementados. SMP depende de estado por
CPU, sincronização e TLB shootdown. Será um marco separado, sem confundir
o código single-BSP atual com suporte multiprocessado.

A shell de kernel executa programas ELF e lê a VFS. A shell de userspace
continua opcional, dependente de uma API de input adequada. A ABI nativa,
cópias verificadas e runtime mínimo já existem. FAT32 é um snapshot readonly
carregado de uma imagem por AHCI. Escritas, partições e persistência mutável
exigem trabalho e gates próprios, começando por imagens descartáveis.

## Decomposição de networking implementada

1. E1000 com RX/TX, buffers próprios, MAC lido do dispositivo e MTU limitado.
2. Ethernet e ARP com validação e expiração de cache.
3. IPv4 e ICMP com checksums; fragmentos e opções IPv4 são rejeitados.
4. Transações UDP com endpoint único e timeout, sem sockets de userspace.
5. DHCP e DNS com parsers e estados limitados.

TCP exigiria máquina de estados, retransmissão, janelas, fechamento e testes
de perda; HTTP depende dessa base. A rede usa polling da thread de
bootstrap/shell, sem worker em background. Comunicação pública e hardware
real não fazem parte da evidência da rede virtual local.

## Limites do plano

Não há promessa de POSIX completo, compatibilidade binária Linux, suporte
a hardware arbitrário, aceleração gráfica, Secure Boot ou uso de produção.
Mesmo v1.0.0 será um baseline experimental com escopo de validação explícito.

O antigo marco v0.0.2 de validação foi absorvido pelo baseline confirmado
antes da evolução direta para v0.1.0. Decisões históricas permanecem no
[development log](development-log.md).
