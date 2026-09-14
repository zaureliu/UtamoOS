# Padrão de código

## Linguagem e formato

- C17, UTF-8 e LF; quatro espaços, sem tabs em C. Receitas de Make usam tabs.
- Funções, variáveis, membros e tags de structs/enums em `snake_case`.
- Macros e constantes globais em `UTAMO_*`; `LOG_*` e `PANIC` são exceções
  intencionais de interface. Nomes do Limine permanecem os oficiais.
- Evite typedefs de structs. O typedef de callback do formatter nomeia um
  contrato repetido; não oculta ownership nem ponteiros de dados.
- Chaves na linha seguinte a definições de função, na mesma linha de `if`/loops.
- `static` para símbolos privados, `const` quando não há mutação, include guards
  por header. O próprio header deve ser incluído no arquivo que o implementa.
- Cada arquivo próprio contém identificador de licença quando apropriado.

Nomes de código e mensagens de boot são ASCII/inglês para não exigir Unicode
do terminal. Documentação em português pode usar acentos. A fonte bitmap está
incorporada como dados C e tem sua origem descrita em `assets/font/README.md`.

## Tipos, limites e ownership

Use `uint64_t` para endereços físicos e comprimentos do protocolo, `uintptr_t`
para conversões explícitas de endereços virtuais e `size_t` para índices de
objetos C. Não compare ponteiros de objetos diferentes para ordenar endereços.
Conversões por `uintptr_t` pressupõem endereçamento linear do target documentado.

Valide operações antes de fazê-las: para soma, `length <= MAX - base`; para
produto, primeiro prove que o divisor não é zero e compare com `MAX / factor`.
Shifts exigem contagem menor que a largura do operando. Não negue o menor
inteiro assinado; converta a magnitude por aritmética sem sinal.

Um ponteiro não nulo não prova validade de mapeamento ou de comprimento.
O chamador é dono de garantir objetos válidos e vida útil. APIs de memória
recebem intervalos válidos, e strings recebem terminador NUL acessível. As
primitivas não fazem validação de page tables. Registre em headers se uma API
aceita nulo, zero, aliasing ou sobreposição.

Não introduza alocação implícita, arrays variáveis na pilha, buffers sem limite
ou callbacks que chamem de volta subsistemas não reentrantes. Nenhum estado
de boot poderá ser usado por IRQs ou APs antes de documentar sincronização.

## Erros e logs

Erros previstos retornam `bool` ou um tipo de resultado próprio. O chamador
decide se deve abortar a etapa. Nunca retorne sucesso para uma funcionalidade
ainda não implementada e nunca transforme warning em sucesso silencioso.
`PANIC("mensagem literal")` informa origem e encerra; não é mecanismo de fluxo normal.

O formatter suporta `%s`, `%c`, `%d`, `%u`, `%x`, `%p`, `%%`, `%lld`, `%llu`
e `%llx`. Não aceita width, precision, `%zu`, `%ld`, floats, `%n` ou ANSI.
Tipos de varargs devem corresponder exatamente: use `(unsigned long long)` para
`uint64_t`/`size_t` com `%llu` e `(void *)` para `%p`. `%d` recebe `int`;
`%u`/`%x` recebem `unsigned int`; `%s` consome `const char *`, portanto passe
um ponteiro desse tipo, inclusive ao usar literais como argumento variádico.
Uma sequência desconhecida é preservada
literalmente e não consome argumento; ela representa erro do chamador a corrigir.

Não anote como printf completo uma API com grammar diferente. Use sempre
`LOG_INFO("%s", input)` para imprimir texto externo, nunca `LOG_INFO(input)`.
Os sinks recebem caracteres, não um buffer intermediário de tamanho fixo.

## Build e revisão

Warnings configurados no Makefile são erros. Não desabilite grupos de warnings
para ocultar um problema; documente qualquer exceção específica justificada.
Não adicione dependências de libc/CRT ao kernel para resolver símbolos sem
examinar a causa. As configurações de target do kernel não se aplicam aos testes
de host, que são executáveis normais e não executam instruções privilegiadas.

Toda alteração deve explicar contrato, comportamento de falha e efeito em boot.
Use testes de unidades para lógica pura e testes de QEMU para hardware/ABI.
Nesta entrega só se revisaram fontes; qualquer frase que afirme resultado real
deve apontar a evidência obtida posteriormente.
