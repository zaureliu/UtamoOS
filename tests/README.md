# Testes do UTAMO OS v0.2

Os testes dos baselines v0.0.1/v0.1.0 são preservados.
O v0.2 acrescenta PMM/VMM, HHDM, comandos e suíte QEMU de memória.
Cobertura implementada não é automaticamente evidência de hardware:
contagens e hashes pertencem às execuções registradas no
[development log](../docs/development-log.md).

## Matriz final observada: 0.2.0

| Camada | Checks | Falhas |
| --- | ---: | ---: |
| Host | 11224 | 0 |
| ELF/ABI | 1439 | 0 |
| QEMU headless | 1550 | 0 |
| Total | 14213 | 0 |

Foram 13 VMs sequenciais, todas aprovadas e recolhidas: boot, quatro suítes
de memória (64/256/512 MiB e CPU sem NX), quatro PFs de memória,
UD2/div0 e regressões do shell/PF pelo harness original.
A matriz está vinculada à imagem nos [resultados v0.2](../docs/v0.2-implementation-report.md)
e no [índice de evidências](../docs/validation-v0.2.json).
Ela não inclui revisão gráfica, UEFI ou hardware físico.

## Histórico preservado: resultados de v0.1.0

| Camada | Checks aprovados |
| --- | ---: |
| Testes host | 5214 |
| Inspeção ELF/ABI | 1303 |
| QEMU headless | 126 |
| **Total** | **6643** |

A validação registrada terminou com zero falhas. As contagens identificam
execuções e artefatos específicos, descritos no relatório e no
[índice de evidências](../docs/validation-v0.1.json); não são uma promessa
para futuras revisões ou outros ambientes. O aceite manual do usuário é
registrado separadamente e não acrescenta checks automatizados.

## Execução

Em Linux/WSL2 Ubuntu, a partir da raiz do checkout:

~~~sh
make test-host
~~~

O alvo compila os executáveis host separadamente com C17 e os warnings do
projeto, incluindo `-Wall -Wextra -Wpedantic -Werror`. Um retorno zero de
cada executável significa que suas verificações executadas passaram.
`-fno-builtin -fno-tree-loop-distribute-patterns` evita que o compilador
substitua loops das funções de memória pelas próprias funções testadas.
O GCC nativo é usado apenas aqui; o kernel usa o cross compiler x86_64-elf.

| Arquivo | Cobertura e limite |
| --- | --- |
| `test_main.c` | Implementações reais de string/format e memory map; usa referências independentes para comparar texto/memória |
| `test_video.c` | Framebuffer, terminal e fonte sobre buffers RAM; não prova MMIO nem aparência no guest |
| `test_gdt.c` | Layout, selectors, descriptors e campos TSS; não executa LGDT/LTR |
| `test_interrupts.c` | Layout IDT/frame, nomes e flags de exceção, diagnóstico formatado; não executa LIDT/IRETQ |
| `test_pic.c` | Sequência de programação 8259, máscaras, cascade, EOI e IRQs espúrias com modelo host de portas/IF |
| `test_pit.c` | Portas/divisor/modo, contador e conversão temporal com modelo host de portas/IF |
| `test_input_shell.c` | Decoder set 1, modificadores, fila circular e edição/tokenização da linha |
| `test_keyboard.c` | Inicialização/controlador PS/2 e tratamento de entrada com portas simuladas |
| `test_shell_commands.c` | Comandos reais com efeitos simulados, inclusive PMM/VMM/mapinfo/selftests e fault vmm |
| `test_memory_helpers.c` | Alinhamento, canonicalidade, máscara física, índices e conversões HHDM |
| `test_pmm.c` | Bitmaps, reservas, ownership, contiguidade, falta de recursos e transações sem efeitos parciais |
| `test_vmm.c` | Walk de quatro níveis, folhas grandes, permissões, rollback, publicação e map/protect/unmap |

As funções privilegiadas são substituídas por modelos explícitos nos testes
que precisam delas. Nenhuma instrução de IO, CLI/STI, LGDT/LIDT ou HLT é
executada no processo host. Os executáveis de teste podem usar a libc apenas
como infraestrutura de relatório/oráculo; ela não entra no kernel.

A cobertura detalhada de vídeo está em [video-tests.md](video-tests.md).
Os contratos de biblioteca abaixo continuam válidos para os testes herdados.


## Cobertura da biblioteca preservada

- Cópia, preenchimento, comparação por bytes sem sinal e guardas nas bordas.
- `memmove` nos dois sentidos de sobreposição, origem igual ao destino e
  intervalos sem sobreposição.
- Intervalos de tamanho zero sem acesso à memória; strings vazias, prefixos,
  bytes acima de `0x7f` e `strncmp` sobre um intervalo sem terminador.
- Formatação decimal, hexadecimal, ponteiros, percentuais e strings nulas.
- `INT_MIN`, `INT_MAX`, `UINT_MAX`, `LLONG_MIN`, `LLONG_MAX` e `ULLONG_MAX`,
  comparados com `snprintf` do host para as conversões compatíveis.
- Buffer exato, truncamento com guardas, capacidade zero/um e medição sem
  destino. NUL emitido por `%c` conta como caractere.
- Sequências desconhecidas, `%n` sem efeito de escrita e formatos incompletos.
- Callbacks, funções variádicas e preservação do `va_list` do chamador.
- Mapa de memória: todos os tipos, soma separada de memória utilizável e
  recuperável do bootloader, ordem crescente, adjacência, sobreposições,
  alinhamento, tamanho zero, tipos desconhecidos, overflow de extremidade e
  capacidade exata de 512 entradas. Inserções rejeitadas preservam o estado.
- Sobreposições de regiões reservadas são aceitas; sobreposição com memória
  utilizável ou recuperável é rejeitada, inclusive quando o conflito está
  numa entrada anterior que não seja a predecessora imediata.

## Contratos e limites

Os formatos aceitos são `%s`, `%c`, `%d`, `%u`, `%x`, `%p`, `%%`, `%lld`,
`%llu` e `%llx`. Não há flags, largura, precisão, floats ou `%n`. Os formatos
desconhecidos permanecem literais e não consomem argumentos: `%08x` produz
`%08x`; `%ld` produz `%ld`; `%q/%d`, com argumento `7`, produz `%q/7`.
O chamador deve fornecer exatamente os tipos de argumento documentados em
`format.h`; para tipos como `uint64_t`, usar cast explícito para
`unsigned long long` e `%llu`/`%llx`. `%p` recebe `void *` e produz `0x0` para
ponteiro nulo. `%s` consome `const char *`, apontando para uma string legível e
terminada em NUL ou representando um ponteiro nulo. Use cast explícito para
`const char *` em literais e argumentos `char *`: o ellipsis não acrescenta
qualificadores de ponteiro. Um formato nulo produz `(null)`.

O retorno de `ksnprintf` é `size_t`, representa a quantidade de caracteres
necessária sem o NUL final e satura em `SIZE_MAX`. Um buffer nulo serve apenas
para medição, independentemente da capacidade. Um buffer não nulo com
capacidade positiva sempre recebe NUL. O formatter não valida o mapeamento de
ponteiros, não detecta todas as incompatibilidades em variádicos e não oferece
um limite de tempo para uma string de entrada muito longa. Formatos devem ser
constantes controladas pelo kernel. Origem e destino não podem se sobrepor.

As funções de memória exigem objetos válidos para intervalos não vazios;
`memcpy` exige ausência de sobreposição ou origem e destino exatamente iguais,
caso também exigido pelo GCC freestanding. `memmove` assume endereçamento linear
e `uintptr_t` representativo dos endereços, como no x86_64 e no host Linux alvo.
`strlen` e `strcmp` exigem strings válidas e terminadas. As funções contadas
não desreferenciam ponteiros quando o tamanho é zero. Não há allocator nem
dependência da libc no código de `kernel/lib`.

O mapa de memória considera intervalos semiabertos `[base, base + length)`;
rejeita soma de extremidade não representável em `uint64_t`. Os testes chegam
ao maior total utilizável alinhado representável, sem alterar contadores
internos artificialmente: overflow da soma utilizável não é alcançável por
inserções válidas de regiões exclusivas sem sobreposição. As verificações
defensivas dos contadores permanecem no código para evitar wraparound.

## ELF, kernel e QEMU

~~~sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
python3 scripts/test-qemu.py --marker "utamo> " --name boot-v02 --check-gdt --check-idt --check-timer
python3 scripts/test-memory-qemu.py --suite --name memory-v02 --timeout 180
~~~

Execute VMs sequencialmente. Os harnesses usam display none, pastas novas
de evidência, timeout e cleanup do próprio subprocesso.
O harness de memória envia teclas QMP ao dispositivo PS/2 emulado;
essa entrada foi autorizada no desenvolvimento v0.2.
Veja [scripts](../scripts/README.md) e [debugging](../docs/debugging.md).

A suíte observa PMM/VMM, CR3/CR0/CR4/EFER, seções, HHDM e arena.
Repete pmmtest/vmmtest, compara accounting e reutilização de tabelas,
depois verifica shell, input, PIT, clear e halt.
Stress do kernel usa 64 frames PMM e 16 páginas VMM através de fronteira
de 2 MiB. O primeiro VMM test pode reter tabelas; repetições devem estabilizar
esse custo e liberar todos os frames de dados.

Modos --fault vmm/ro/nx/pf são execuções independentes. Conferem instruções
reais, CR2/error code, registradores e dump original seguido de snapshot VMM.
RO/NX protege o endereço selecionado; aliases HHDM impedem inferir W^X global.

## Evidência e limites

Cada build/validation/<name>/report.json identifica hashes ELF/ISO,
Git, configuração, checks e cleanup. A serial e os registros suportam a revisão.
Make clean remove esses arquivos; preserve a evidência necessária no projeto.
A matriz final acima pertence aos hashes registrados; outras imagens exigem nova execução.

Host não executa hardware. HHDM puro valida aritmética/tipos/ranges,
mas a VM verifica as page tables reais. QMP/PS2 emulado confirma o caminho
IRQ/input quando observado; não prova digitação em teclado físico.
Clear serial não confirma aparência gráfica.
O aceite manual v0.1 permanece histórico; UEFI, hardware físico e legibilidade
da nova imagem exigem evidência específica.

## Próximos testes

v0.3 deverá testar kernel heap: alinhamento, overflow, OOM, double-free,
fragmentação, ownership entre blocos/páginas e estabilidade sob stress.
Guard pages, reclaim, aliases e SMP exigem casos separados.
Fuzzing/sanitizers podem ampliar a cobertura pura; funções com nomes de libc
precisam de atenção aos interceptadores do host.
