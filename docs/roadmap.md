# Roadmap

Versões indicam marcos técnicos, não prazos. Cada marco exige código revisado,
testes pertinentes e evidências reproduzíveis antes de avançar. A ordem pode
mudar se uma dependência real exigir; registre a decisão no development log.

| Marco | Entrega | Critério de saída proposto |
| --- | --- | --- |
| **v0.0.1** | Boot, framebuffer, terminal, logs, serial, panic, mapa | Baseline validado; tag preservado em 273e476 |
| v0.0.2 (absorvido) | Plano antigo de validar o baseline | Boot real confirmado; evolucao autorizada diretamente para v0.1 |
| **v0.1 / 0.1.0** | GDT/TSS/IST, IDT, excecoes, PIC/PIT, PS2, shell de kernel | Host/ELF/QEMU headless aprovados; input real e visual pendentes de validacao manual |
| v0.1.1 | ACPI/RSDP, descoberta MADT/APIC e topologia | Tabelas e comprimentos validados; fallback documentado |
| **v0.2** | Physical memory manager, bitmap allocator, paging, virtual memory | Alocação/liberação de frames sem dupla posse; reservas preservadas |
| v0.2.1 | Page tables próprias, permissões, guard pages, descarte seguro do bootloader | Page faults esperados e ausência de aliases com permissões indevidas |
| **v0.3** | Kernel heap, `kmalloc`, `kfree`, memory debugging | Alinhamento, OOM, double-free, overflow e fragmentação exercitados |
| **v0.4** | Threads, context switching e scheduler | Trocas repetidas preservam registradores/pilhas; idle funciona |
| v0.4.1 | SMP, dados por CPU, sincronização e TLB shootdown | Dois ou mais CPUs sem corridas conhecidas no PMM/log/scheduler |
| **v0.5** | Ring 3, processos, ELF loader, syscalls, primeiro `init` | Processo isolado não lê/escreve memória de outro nem do kernel |
| v0.5.1 | ABI inicial documentada, cópia user/kernel, erros e handles | Ponteiros inválidos de userspace geram erro/fault isolado |
| **v0.6** | VFS, RAM filesystem, importação initramfs, FAT32 | Parsers e operações por handles validados; RAMFS antes de persistência |
| v0.6.1 | Shell de userspace, utilitarios e libc parcial | Evoluir a shell integrada ao kernel v0.1 para comandos em processos reais |
| **v0.7** | PCI/PCIe, subsistema de storage, AHCI, exploração NVMe | Enumerar dispositivos e ler imagens de teste com limites/DMA corretos |
| v0.7.1 | Escrita em imagens descartáveis e recuperação de erros | Integridade e limites verificados antes de dados persistentes importantes |
| **v0.8** | Rede em camadas | Comunicação reproduzível e pacotes malformados rejeitados |
| **v0.9** | Subsistema gráfico básico e window manager experimental | Input, superfícies e ownership definidos; falhas de cliente isoladas |
| **v1.0** | Stable educational experimental OS baseline | Arquitetura/documentação estáveis, testes conhecidos e limitações publicadas |

## Decomposição de v0.8

1. Interface de dispositivo Ethernet, RX/TX, buffers, endereços MAC e limites de MTU.
2. Ethernet e ARP, validação dos tamanhos e expiração de cache.
3. IPv4 e ICMP, checksums, rejeição de combinações inválidas; política explícita
   de fragmentação/reassembly antes de aceitar fragmentos.
4. UDP e sockets mínimos com quotas e timeouts.
5. DHCP para configuração e DNS para resolução, parsers limitados.
6. TCP com máquina de estados, retransmissão, janelas, fechamento e testes de perda.

Usar rede virtual isolada no desenvolvimento. Nenhuma camada será apenas um
stub que finge conectar; cada etapa precisa de parsing, estados e falhas observáveis.

## Dependências e não objetivos

FAT32 poderá começar sobre uma imagem em RAM após VFS; operações em dispositivos
reais dependem do storage de v0.7. Initramfs é um formato de entrada, não substitui
VFS nem implica uma libc de host. Syscalls dependem de memória e scheduler;
SMP depende de sincronização e tratamento de interrupções próprios.

Não há promessa de POSIX completo, compatibilidade binária Linux, hardware
arbitrário, aceleração gráfica, Secure Boot ou uso de produção em v1.0.
Um baseline educacional estável deve ser honesto sobre a plataforma que valida.
