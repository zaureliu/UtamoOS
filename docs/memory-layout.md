# Layout de memória — base v0.2 e extensões da campanha

As seções até «Extensões da campanha» preservam o contrato do baseline v0.2;
suas ausências de heap, guards e processos descrevem aquele marco. O kernel
atual acrescenta esses subsistemas, com ownership e endereços abaixo.
Consulte [arquitetura atual](architecture.md) e [estado dos gates](astra-campaign-state.md).

O kernel ELF64 permanece ligado em `0xffffffff80000000`. Limine informa a
base física real e o offset HHDM por requests explícitos; nenhuma base física
ou identidade físico/virtual é presumida. O VMM mantém o CR3 e a árvore de
quatro níveis do boot, validando e estendendo essa árvore em região própria.
O contrato completo está em [memory-management.md](memory-management.md).

## Imagem e stacks

| Região | Alinhamento | ELF | Mapping principal após proteção, com NX |
| --- | --- | --- | --- |
| `.limine_requests` | Requests em 8 bytes; segmento em página | RW | RW/NX |
| `.text` | 4096 bytes | RX | RX |
| `.rodata` | 4096 bytes | R | RO/NX |
| `.data` e `.bss` | Início em página | RW | RW/NX |
| `.debug_*` | Fora dos segmentos carregados | Não ALLOC | Somente arquivo ELF no host |

O linker exporta limites de kernel, text, rodata, data e BSS para inspeção e
proteção. `PT_GNU_STACK` não é executável. A aplicação de permissões é
registrada como aplicada ou adiada; não presume poder dividir huge pages herdadas.

Stack bootstrap de 64 KiB, três stacks IST de 16 KiB, GDT/TSS e IDT continuam
na imagem estática do kernel. O build usa `-mno-red-zone` e frame pointers.
A região física KERNEL_AND_MODULES não é distribuída pelo PMM.
Não há guard pages ou heap em v0.2.

## HHDM e mapa físico

O mapa próprio conserva até 512 regiões, com intervalos semiabertos, validação
de overflow, ordenação e reservas. Tipos desconhecidos do protocolo continuam
reservados. Somente regiões USABLE fornecem frames livres.
BOOTLOADER_RECLAIMABLE e ACPI RECLAIMABLE continuam indisponíveis para alocação.

Conversões HHDM aceitam apenas USABLE, BOOTLOADER_RECLAIMABLE,
KERNEL_AND_MODULES e FRAMEBUFFER, segundo o contrato da base revision 3.
Exigem range inteiro dentro de uma região e endereços canônicos. O VMM
verifica os mappings reais antes de inicializar os bitmaps.
Page tables herdadas devem ser BOOTLOADER_RECLAIMABLE, evitando sobreposição
com a primeira escrita de metadados do PMM.

O framebuffer permanece acessado por seu ponteiro virtual, pitch e máscaras
RGB originais; o VMM confere a correspondência física do range completo.
Não cria outro alias com cache policy diferente. Os limites de 8192 pixels
por eixo e 256 MiB, incluindo padding, continuam em `framebuffer.h`.

## Metadados e arena dinâmica

| Região | Contrato |
| --- | --- |
| Página física zero | Reservada; nunca retornada pelo PMM |
| Bitmaps PMM | Dois bits por frame do span; storage alinhado, escolhido em USABLE e reservado |
| Tabelas herdadas | Conservadas, sem reclaim do bootloader |
| Novas page tables | Frames PMM zerados, publicados e fixados permanentemente |
| `[0xffffc00000000000, 0xffffc08000000000)` | Slot PML4 384, 512 GiB para mutações públicas |
| `[0xffffc00000000000, 0xffffc00000400000)` | Primeiros 4 MiB dedicados aos selftests |

A arena começa vazia e deve ser disjunta de HHDM/kernel.
Só recebe mappings de 4 KiB sobre RAM USABLE alocada pelo PMM.
A consulta aceita outros endereços canônicos, inclusive folhas herdadas de
2 MiB/1 GiB. As operações públicas não alteram mappings do bootloader nem
fazem split dessas folhas.

Unmap remove a tradução; o chamador ainda deve liberar o frame de dados.
Tabelas intermediárias vazias permanecem fixadas e reutilizáveis.
Não há alocador de endereços virtuais, destruição de address space ou contagem
automática de aliases.

## Limite de proteção

O mapping principal usa RX/RO/NX/RW/NX conforme as seções quando compatível.
CR0.WP está habilitado e NX depende de CPUID/EFER.NXE.
Aliases HHDM herdados podem continuar graváveis e executáveis.
Logo, permissões dos segmentos e probes RO/NX não demonstram W^X global.

A etapa v0.3 introduzirá kernel heap; revisão dos aliases, guard pages,
recuperação de tabelas, processos e TLB shootdown exigem trabalho separado.

## Extensões da campanha

| Região atual | Contrato |
| --- | --- |
| Heap a partir de 0xffffc00001000000 | 64 KiB iniciais, crescimento de 64 KiB até 64 MiB; supervisor RW/NX quando disponível |
| Stacks a partir de 0xffffc00040000000 | 64 slots; stride 69.632 bytes, guard inferior de 4 KiB e 64 KiB de stack |
| User VM [0x10000, 0x0000800000000000) | Até 128 páginas privadas; até 64 tabelas incluindo PML4; NX obrigatório e W^X |
| Stack ELF terminando em 0x70000000 | 16 páginas privadas, guard inferior e argumento copiado; raiz privada |
| MMIO a partir de 0xffffc00080000000 | Janela de 4 MiB, supervisor UC; até 1 MiB por aperture |

As tabelas de kernel permanecem fixadas. As tabelas privadas de processos são
recolhidas com suas páginas quando a raiz está inativa; não pertencem ao ledger
de tabelas permanentes. O scheduler troca CR3 e configura TSS/RSP0 antes do
retorno a CPL3. Heap e pilhas dinâmicas compartilham o subtree superior de kernel.

O boot module initramfs e o snapshot FAT32 montado permanecem vivos durante
a execução. AHCI reserva quatro páginas DMA e E1000 reserva seis após
publicação; falha ambígua de dispositivo não devolve esses frames ao PMM.
Veja [heap](heap.md), [scheduler](scheduler.md), [processos](processes.md),
[ELF](elf-loader.md) e [rede](networking.md).

## Janela MMIO introduzida em v0.7

PCI devices use supervisor UC mappings in the 4 MiB window beginning at
0xffffc00080000000, beyond the existing heap/thread arenas. Ordinary RAM
map/protect/unmap operations reject this window. Physical RAM/HHDM types are
never admitted as device apertures. Mappings and table frames remain permanent.
See [storage](storage.md) for PAT validation and DMA ownership.
