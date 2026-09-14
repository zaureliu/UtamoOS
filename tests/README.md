# Testes do UTAMO OS

Estado: infraestrutura implementada; testes pendentes no ambiente de
desenvolvimento. Nenhum teste foi executado no computador corporativo.

`test_main.c` exercita as implementações reais de `kernel/lib/string.c` e
`kernel/lib/format.c` e `kernel/memory/memory_map.c`. O executável de testes é
um programa de host, usa a libc
do host apenas para o relatório e uma referência numérica de `snprintf`, e não
integra o kernel. Os resultados de memória e texto são comparados por uma
rotina independente, para não usar `memcmp`/`strcmp` como oráculos de si mesmos.

`test_video.c` é um segundo programa de host, com `main` próprio, que exercita
framebuffer, terminal e fonte usando buffers em RAM. Os dois programas são
compilados separadamente e executados pelo mesmo target `make test-host`.
Consulte a [cobertura de vídeo](video-tests.md) para seus casos e limitações.

## Execução futura

No ambiente Linux pessoal, a partir da raiz do repositório:

```sh
make test-host
```

O target deve compilar com GCC de host, C17 e os includes de `kernel/include`.
As opções `-fno-builtin -fno-tree-loop-distribute-patterns` impedem que o
compilador substitua os loops das funções de memória pelas próprias funções.
Os mesmos cuidados valem para o build freestanding do kernel. Um retorno zero
de cada executável significa que suas verificações executadas passaram; a geração
dos fontes, por si só, não fornece esse resultado.

## Cobertura preparada

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

## Etapas seguintes

1. Executar estes testes no computador pessoal, registrando comando, versões
   de ferramentas, saída completa e código de retorno.
2. Executar também a suíte de vídeo, conferindo seus resultados reais com
   [video-tests.md](video-tests.md); buffers em RAM não validam mappings de MMIO.
3. Cobrir bitmap allocator, listas e parsers quando forem implementados, com
   casos vazios, fronteiras, overflow, falta de recursos e entradas inválidas.
4. Acrescentar fuzzing e sanitizers em harnesses isolados. As funções com nomes
   de libc podem interferir nos interceptadores dos sanitizers; adotar aliases
   de teste antes de interpretar tais resultados.
5. Validar boot, serial, framebuffer e halt em QEMU BIOS/UEFI; estes testes de
   host não provam correção de boot, protocolo, ABI, paginação ou hardware.

Registrar os resultados reais conforme o [roteiro de validação](boot-validation.md),
preservando relatórios e evidências selecionadas em `docs/validation/` quando
essa pasta for criada no ambiente pessoal. Até lá, não apresentar os testes
preparados como testes aprovados.
