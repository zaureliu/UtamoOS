# Gerenciamento de memória — UTAMO OS v0.2

O v0.2 acrescenta PMM e VMM ao kernel existente. Mantém Limine v8.7.0, a
árvore de paginação do handoff, o kernel higher half e as interfaces de
interrupção, vídeo e shell. Não há heap, scheduler, userspace ou alocação em IRQ.
O estado descrito aqui é o contrato do código; resultados de execução são
registrados separadamente no [development log](development-log.md).

## Fronteiras

| Camada | Responsabilidade |
| --- | --- |
| `boot.c` / `boot.h` | Copiar memory map, offset HHDM e bases física/virtual do executável |
| `memory_helpers.c` / `memory.h` | Alinhamento, canonicalidade, índices e máscara física |
| `hhdm.c` / `hhdm.h` | Aritmética validada de conversão HHDM, sem dereferenciar endereços |
| `pmm_core.c` / `pmm_core.h` | Planejamento e bitmaps de frames, sem acesso privilegiado |
| `pmm.c` / `pmm.h` | Instância BSP e serialização das transações por IF |
| `vmm_core.c` / `vmm_core.h` | Walk de quatro níveis e transações sobre tabelas fornecidas por callbacks |
| `vmm.c` / `vmm.h` | Bootstrap, CR3 herdado, PMM/HHDM, permissões e política da arena |
| `memory_selftest.c` | Testes limitados, somente quando solicitados pelo shell/debugger |
| `paging.asm` / `paging.h` | CPUID, registradores/MSRs, INVLPG e acessos de teste privilegiados |

As structs são explícitas e não escondem ownership. Um endereço físico retornado
pelo PMM não é ponteiro C. Conversão HHDM fornece acesso a um mapping existente;
ela não aloca, não cria mapping e não transfere a posse do frame.

## Handoff, HHDM e reservas antes do primeiro frame

O adaptador pede HHDM e executable address com revision 0, além dos requests
já existentes. A base revision continua 3; paginação continua restrita a quatro
níveis, conforme o [protocolo Limine fixado](https://github.com/limine-bootloader/limine/blob/v8.7.0/PROTOCOL.md)
e o [header v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/limine.h). A base virtual informada deve coincidir com `__kernel_start`; a base
física e o offset HHDM devem estar alinhados. CPUID fornece MAXPHYADDR, aceito
na faixa de 32 a 52 bits. CR4.LA57 ativo é rejeitado.

Somente quatro tipos do mapa pertencem ao direct map usado pelo kernel:
USABLE, BOOTLOADER_RECLAIMABLE, KERNEL_AND_MODULES e FRAMEBUFFER.
A conversão exige extensão não vazia, inteira dentro de uma única região,
sem overflow e com endereços virtuais canônicos. Duas regiões adjacentes não
formam implicitamente uma extensão única. Reserved, ACPI e bad memory não se
tornam ponteiros apenas por somar o offset. O framebuffer já recebido como
ponteiro virtual não recebe o offset uma segunda vez.

O VMM lê CR3 e conserva seu valor durante a inicialização. Antes de escrever
qualquer bitmap, percorre todas as tabelas alcançáveis do handoff, com
profundidade máxima de quatro e orçamento de 65.536 visitas. Cada frame de
tabela herdada deve pertencer a BOOTLOADER_RECLAIMABLE; ciclos e entradas
incompatíveis são rejeitados. Assim, uma tabela descrita contraditoriamente
como USABLE não pode ser sobrescrita pelo primeiro bitmap.

Também verifica as traduções lineares do kernel, de todas as extensões HHDM
admitidas e do framebuffer. O HHDM esperado deve ser supervisor e gravável,
sem colidir com a arena dinâmica ou a imagem virtual do kernel. O slot PML4
da arena deve conter zero. Esses checks preservam o handoff em vez de trocar
CR3 por uma árvore incompleta.

O mapa copiado continua vivo e imutável. BOOTLOADER_RECLAIMABLE e ACPI
RECLAIMABLE não são recuperados nesta versão. Kernel, módulos, framebuffer,
page tables herdadas e demais tipos não USABLE nunca entram na lista de frames
livres; imagem e framebuffer ainda recebem reservas explícitas defensivas.
Stacks bootstrap/IST, GDT/TSS, IDT e estado estático estão dentro da imagem.

## PMM: dois bits por frame

Frames têm 4096 bytes. O maior fim de região USABLE define `span_frames`,
incluindo buracos físicos abaixo desse limite. Cada bitmap ocupa
`ceil(span_frames / 8)` bytes; dois bitmaps contíguos são arredondados juntos
para páginas inteiras. Um first-fit procura armazenamento suficiente em uma
região USABLE, sem usar a página física zero. Falta de espaço falha de modo
explícito; não existe array de bitmap com tamanho fixo na imagem.

| eligible | occupied | Estado |
| ---: | ---: | --- |
| 0 | 1 | Permanentemente indisponível: buraco, tipo reservado, metadado ou página fixada |
| 1 | 0 | Frame USABLE livre |
| 1 | 1 | Alocação pertencente ao chamador e liberável |
| 0 | 0 | Não é um estado disponibilizado pela inicialização |

A inicialização começa indisponível e libera apenas frames USABLE.
Reserva a página zero e as páginas completas dos próprios bitmaps.
`total_frames` conta os frames originalmente USABLE; `used_frames`
inclui alocações e reservas feitas dentro desse conjunto.
Sempre vale `total = used + free`. `span_frames` não é RAM instalada e
pode ser maior que `total_frames` por incluir lacunas físicas.

`pmm_alloc_pages(count, out)` procura um intervalo físico contíguo por next-fit.
Se necessário, refaz a busca desde zero, considerando também intervalos que
cruzam a posição anterior do cursor. Não existe intervalo circular que una o
fim físico ao início. A busca é O(span_frames) no pior caso; só altera bits,
contadores, cursor e saída após encontrar todo o intervalo.

`pmm_free_pages` valida todos os frames antes de modificar qualquer bit.
Contagem zero, desalinhamento, range inválido, página já livre, metadado ou
reserva permanente rejeitam a operação inteira. `pmm_reserve_range`
arredonda limites para fora e rejeita atomicamente qualquer alocação já
pertencente a um chamador. `pmm_pin_page` converte uma alocação em reserva
permanente, sem diminuir o total usado; é o destino das novas page tables.

Os frames de dados não são zerados automaticamente. Quem os aloca deve
inicializar seu conteúdo e remover todos os mappings antes de liberá-los.
O PMM não conta referências de aliases e não sabe se um frame continua mapeado.

## VMM: consulta e mutação têm escopos diferentes

A camada HHDM pura é usada por vmm.c e exercitada nos testes host.
Ela não substitui a verificação das traduções reais no bootstrap.

A decomposição do endereço virtual de quatro níveis é:

| Bits do endereço | Campo |
| --- | --- |
| 63–48 | Extensão de sinal do bit 47; canonicalidade |
| 47–39 | Índice PML4 |
| 38–30 | Índice PDPT |
| 29–21 | Índice PD |
| 20–12 | Índice PT |
| 11–0 | Offset na página de 4 KiB |

Cada índice tem nove bits. Uma folha de 2 MiB usa offset 20–0; uma de
1 GiB usa offset 29–0. LA57/five-level não é aceito nesta implementação.

O walk percorre PML4 → PDPT → PD → PT, com 512 entradas por nível.
Valida canonicalidade, largura física, alinhamento de folhas grandes, ciclos
e possibilidade de acessar cada frame de tabela. A consulta não aloca e
reconhece folhas herdadas de 4 KiB, 2 MiB e 1 GiB. A tradução inclui o offset
do byte consultado; RW e USER refletem a interseção dos ancestrais, enquanto
NX se acumula ao longo do caminho.

`vmm_query_page` aceita qualquer endereço canônico cuja árvore possa ser
inspecionada com segurança. Retorno `true` com `mapped=false` significa
ausência; `false` significa indisponibilidade, endereço inválido ou walk
inseguro. Não se inventa endereço físico para uma página ausente.

As APIs públicas de map/unmap/protect só modificam o slot PML4 384:

~~~text
[0xffffc00000000000, 0xffffc08000000000)  arena dinâmica de 512 GiB
[0xffffc00000000000, 0xffffc00000400000)  primeiros 4 MiB para selftests
~~~

Um map exige endereço virtual/físico alinhado a 4 KiB, PRESENT, flags
admitidas e um frame USABLE atualmente alocado ao chamador pelo PMM.
Não cria aliases de kernel, bootloader, MMIO ou framebuffer. Cache-disable e
write-through são rejeitados pela API pública: novos mappings usam RAM WB,
sem mudar a política PAT dos mappings herdados.

Mappings presentes nunca são substituídos implicitamente. Entradas não
presentes com conteúdo não zero também são rejeitadas. Folhas de 2 MiB/1 GiB
não são divididas por map, unmap ou protect. Permissões incompatíveis com
ancestrais não são ampliadas silenciosamente. O bit USER é representável e
exercitado pelo selftest, mas não existe ring 3 nesta versão.

### Bits e permissões das entradas

| Bit(s) | Significado usado no walk |
| --- | --- |
| 0 | PRESENT |
| 1 | RW; precisa ser permitido por todos os ancestrais |
| 2 | USER; precisa ser permitido por todos os ancestrais |
| 3 / 4 | PWT / PCD, política de cache |
| 5 | Accessed, atualizado pelo hardware |
| 6 | Dirty nas folhas |
| 7 | PS em PD/PDPT; PAT numa PTE de 4 KiB |
| 8 | GLOBAL nas folhas |
| 9–11 | Disponíveis para software, sem política própria nesta versão |
| 12 até MAXPHYADDR−1 | Endereço físico de tabela/folha, sujeito ao alinhamento |
| 63 | NX, somente quando EFER.NXE está habilitado |

O contexto do nível é obrigatório: bit 7 de PT não significa huge page.
Em folhas de 2 MiB/1 GiB, PAT está no bit 12; esse bit é tratado como atributo,
não como parte da base física, e não invalida o alinhamento da folha.
A máscara física deriva de MAXPHYADDR; bits de endereço acima dela são
rejeitados, em vez de truncar silenciosamente o frame.

Map/protect aceitam uma máscara explícita de permissões.
Protect preserva endereço e bits fora dessa máscara, incluindo Accessed,
Dirty, PAT e bits de software já existentes. Isso não implementa uma política
de software, proteção por chaves ou novas extensões da CPU.
A API pública não oferece alteração PAT/PWT/PCD para contornar a política WB.
Essas regras se apoiam nas estruturas do código e nos capítulos de paginação
do [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

## Publicação, rollback e vida das tabelas

Quando faltam níveis, o VMM aloca até três frames PMM e zera cada página.
Monta o novo ramo fora da árvore ativa. Falha de alocação ou de acesso
devolve todos os frames temporários, preservando todas as entradas existentes.
Só publica o ramo completo com um store de 64 bits com semântica release.
Em seguida, fixa cada frame de tabela no PMM e atualiza sua contagem.
A leitura usa acquire. Falha impossível de ownership no rollback/pin é panic.

Cada map/unmap/protect invalida o endereço com INVLPG. Não há reload global
de CR3 nem TLB shootdown entre CPUs. Os wrappers salvam IF, executam a
transação com IRQs desabilitadas e restauram IF. IRQs/NMIs não alocam nem
modificam page tables; não há contrato SMP.

`vmm_unmap_page` remove a tradução, mas não libera o frame de dados.
Tabelas intermediárias vazias permanecem fixadas para reutilização.
A primeira execução de `vmmtest` pode aumentar `used_frames` exatamente
pelo número de tabelas novas; repetições no mesmo intervalo devem estabilizar
esse custo e devolver todos os frames de dados. Não há destruição de espaços
virtuais, recuperação de tabelas vazias ou heap.

## Permissões e seus limites

CPUID anuncia NX; o código habilita e relê EFER.NXE quando suportado.
Se NX não existe, não escreve bits NX e informa essa limitação.
CR0.WP é habilitado e conferido, para que ring 0 respeite páginas somente leitura.

Após verificar toda a extensão da imagem, o VMM aplica no mapping principal
do kernel: text RX, rodata RO/NX, dados/BSS/requests RW/NX.
A aplicação exige folhas herdadas de 4 KiB e permissões compatíveis nos
ancestrais. Caso isso não seja possível, reporta proteção adiada, sem dividir
folhas grandes apenas para aplicar a política.

Essas permissões não constituem W^X global. Os aliases HHDM herdados podem
continuar graváveis e executáveis, inclusive para frames que compõem a imagem.
Os probes RO/NX demonstram a proteção do endereço testado; não demonstram
isolamento contra todos os aliases. Guard pages, revisão dos aliases e política
de execução para todo o espaço virtual permanecem fora deste milestone.

## Shell, diagnóstico e selftests

| Comando | Efeito |
| --- | --- |
| `pmm` | Frames totais/livres/usados, tamanho de página e localização/custo dos bitmaps |
| `vmm` | CR3, HHDM, base do kernel, MAXPHYADDR, NX e tabelas próprias |
| `mapinfo hex` | Consulta de endereço canônico, presença, tradução, flags efetivas e folha |
| `mem` | Mapa original, estatísticas PMM e configuração VMM |
| `pmmtest` | Stress limitado de 64 frames, marcadores, contiguidade, free e double-free |
| `vmmtest` | 16 páginas, fronteira de 2 MiB, aliases, protect, unmap/remap e contabilidade |
| `fault vmm` | Aloca/mapeia, remove/libera e provoca acesso à página ausente |

`mapinfo` recebe hexadecimal, com prefixo `0x` opcional, sem sinal ou
argumentos extras. `fault ud2/div0/pf` continua disponível.
Os probes internos `memory_fault_readonly` e `memory_fault_nx` são
selecionados pelo harness/GDB; não são comandos `fault ro` e `fault nx`.

Uma exceção conserva o dump original completo na serial primeiro.
Depois, para page fault, acrescenta uma consulta VMM somente leitura e sem
alocação: mapping, físico, flags efetivas e tamanho de folha, ou indisponibilidade.
A saída gráfica é tentada depois. Uma falha durante o walk pode causar
exceção recursiva, mas não apaga o dump serial original. Não há recuperação
de page fault nem page-in automático.

Nenhum selftest ou probe fatal roda no boot normal. O harness e os limites de
evidência estão em [debugging](debugging.md) e [tests/README](../tests/README.md).
O próximo milestone é v0.3, kernel heap, usando estes contratos de páginas.

## Matriz observada da imagem 0.2.0

A validação final registrou 11.224 checks host, 1.439 checks ELF/ABI e
1.550 checks QEMU: 14.213 checks, zero falhas.
Foram 13 VMs headless sequenciais, todas encerradas e recolhidas.
A matriz incluiu 64/256/512 MiB, CPU sem NX, probes de memória e regressões
UD2/div0/PF/shell. Esses resultados pertencem aos hashes e configurações do
[relatório v0.2](v0.2-implementation-report.md) e do
[índice de evidências](validation-v0.2.json), sem afirmar hardware físico,
UEFI ou aparência gráfica.
