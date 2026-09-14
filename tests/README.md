# Testes do UTAMO OS 0.1.0

Os testes existentes do baseline v0.0.1 foram preservados e executados antes
das alterações. O desenvolvimento v0.1 acrescenta testes host da lógica de
interrupções/input/shell e validação headless do kernel real. Consulte o
[relatório da implementação](../docs/v0.1-implementation-report.md) para a
contagem final, comandos, hashes e resultados observados; uma cobertura
implementada não é automaticamente uma validação de hardware.

## Resultados registrados de v0.1.0

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
| `test_shell_commands.c` | Shell real com efeitos de hardware simulados: comandos, limites de linha, saídas, clear/backspace e ações fatais |

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

## ELF e kernel em QEMU

A validação do binário é separada dos testes host:

~~~sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
python3 scripts/test-qemu.py --marker "utamo> " --name boot-review \
    --check-gdt --check-idt --check-timer
~~~

`make inspect` inclui a inspeção Python, que verifica os bytes do ELF realmente ligado, incluindo as 256
entradas da tabela relativa dos stubs, a distinção entre error code da CPU e
sintético, destinos dos jumps, preservação/restauração de registradores,
alinhamento antes do CALL e IRETQ. Ela não executa esses caminhos.
A execução no QEMU é que permite observar GDT/IDT carregadas e ticks avançando.

Todos os modos do harness usam `-display none`, uma única VM por vez e
timeout. Os probes de exceção usam GDB e instruções reais; o boot normal não
dispara testes fatais. O [guia de debugging](../docs/debugging.md) descreve
os comandos para UD2/div0/page fault e o breakpoint `cpu_wait_interrupt`
antes de HLT. A validação registrada observou UD2, divisão por zero, page fault e avanço
do PIT; o relatório identifica a imagem final e os resultados de cada execução.

Os artefatos ficam em `build/validation/<name>/`: serial, report JSON,
registros GDB/HMP e, quando solicitado, log interno do QEMU.
O relatório inclui os hashes do ELF/ISO e confirma se o processo foi
encerrado. Use nomes novos para não sobrescrever evidências.
`make clean` remove também esses diretórios; uma contagem copiada sem
identificar a execução/artefato não é suficiente como evidência.

## Validação manual e cobertura adicional

O usuário confirmou em 2026-09-14 testes manuais no QEMU/VNC de teclado PS/2,
digitação de caracteres, Enter, Backspace, comandos do shell, clear e halt.
A release foi aceita com base nessa confirmação e nas evidências automatizadas
anteriores. Veja as [notas da release](../docs/releases/v0.1.0.md).

Os testes automatizados mantiveram a restrição headless. A suíte `--suite`,
`--fault` via teclado e `--capture-framebuffer` não foi executada e não entra nas contagens de checks.
O aceite informado pelo usuário é uma categoria separada de evidência.
UEFI, hardware físico e casos de input/saída não citados explicitamente
continuam sem comprovação específica.

## Cobertura futura

- PMM/bitmap allocator, listas e estruturas futuras: fronteiras, overflow,
  regiões vazias, falta de recursos e entradas inválidas.
- VMM: mapeamento, proteção e gerenciamento explícito de falhas; v0.1 somente
  diagnostica page faults.
- Fuzzing e sanitizers em harnesses isolados. Funções com nomes de libc podem
  interferir nos interceptadores; adotar aliases de teste antes de interpretar
  resultados desses instrumentos.
- Casos adicionais de hardware, firmware e condições adversas, mantendo
  separados os resultados de host, ELF, QEMU e observação manual.
