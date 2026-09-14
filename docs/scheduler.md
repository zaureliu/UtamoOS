# Threads de kernel e scheduler

A v0.4 introduz threads de kernel e preempção sobre a base de memória e heap
preservada. Este guia descreve os contratos implementados. O gate completo da
v0.4 permanece pendente nesta revisão; o último marco GREEN é a v0.3.0.
A presença de código, testes ou harness não equivale a uma execução aprovada.

## Separação de responsabilidades

| Arquivo | Responsabilidade |
| --- | --- |
| `kernel/core/sched_core.c` | Política pura, estados, filas, deadlines e snapshots. |
| `kernel/core/scheduler.c` | TCBs, integração de interrupções, criação, espera e reap. |
| `kernel/core/thread_stack.c` | Frames físicos, guard pages, gerações e frame inicial. |
| `kernel/core/scheduler_selftest.c` | Workloads limitados e contabilidade após cleanup. |
| `kernel/arch/x86_64/thread_test.asm` | Probe de registradores/flags durante INT 240. |
| `kernel/arch/x86_64/interrupt_stubs.asm` | Entrada comum e restauração do frame selecionado. |
| `kernel/include/utamo/scheduler.h` | Interface de threads e observação do scheduler. |

O core não aloca memória, executa instruções privilegiadas nem troca pilhas.
A integração fornece serialização e decide quando o frame selecionado será
restaurado. Os drivers de IRQ continuam pequenos e não alocam TCBs ou pilhas.

`scheduler_init()` é executado uma vez depois de heap/PIT/teclado estarem
prontos, ainda com IF=0. Registra o bootstrap como thread `shell`, TID 1, e
cria a pilha da thread `idle`, TID 0. Ambos os TCBs são estáticos; os 16 frames
da pilha idle são contabilizados pelo PMM, sem uma alocação viva no heap.
O log é `Kernel scheduler initialized (round-robin, 2 ticks)`.

## Política e estados

O registro comporta 64 threads, incluindo shell e idle: até 62 threads
dinâmicas simultâneas. IDs crescem monotonicamente; esgotamento de IDs é
rejeitado, sem wrap nem reutilização silenciosa.

| Estado | Significado |
| --- | --- |
| RUNNING | Thread selecionada para executar nesta CPU. |
| READY | Pode executar; threads normais pertencem à fila FIFO de prontas. |
| BLOCKED | Aguarda evento explícito, como entrada PS/2. |
| SLEEPING | Aguarda um deadline de ticks na fila ordenada de sleepers. |
| ZOMBIE | Terminou; TCB/pilha aguardam liberação por outra thread. |

A seleção usa round-robin e quantum de 2 ticks do PIT nominal de 100 Hz,
aproximadamente 20 ms. Ao esgotar o quantum, o core pede reagendamento;
a integração faz a seleção depois de concluir e reconhecer a IRQ.
Yield voluntário recoloca a thread executável no fim da fila.

Idle só é selecionada quando não há thread normal pronta. Ela aparece como
READY quando shell está RUNNING, mas não ocupa a fila FIFO: por isso
`Ready threads: 0` pode coexistir com a linha `0 READY idle` de `ps`.
Com shell bloqueada, idle executa o reaper e espera usando STI/HLT.

O core valida ownership, IDs, limites, estados e participação nas filas.
O registro é limitado; o validador pode custar O(N²), com N limitado a 64,
devido às buscas e comparações de ownership/IDs nesse registro. Não há
prioridades, afinidade, SMP ou garantia de latência de tempo real.

## Troca de contexto e ABI

O frame de interrupção continua tendo 176 bytes, 22 slots de 8 bytes:

| Offset | Conteúdo |
| --- | --- |
| 0–56 | R15, R14, R13, R12, R11, R10, R9, R8 |
| 64–112 | RBP, RDI, RSI, RDX, RCX, RBX, RAX |
| 120, 128 | Vector, error code normalizado |
| 136, 144, 152 | RIP, CS, RFLAGS |
| 160, 168 | RSP, SS |

Em long mode, SS:RSP fazem parte do frame mesmo sem mudança de privilégio;
IRETQ restaura esse par. Essa convenção segue a seção 8.9 do
[AMD64 Architecture Programmer's Manual, Volume 2](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content).

A entrada Assembly salva os 15 GPRs, limpa DF, passa o frame em RDI e alinha
RSP a 16 bytes antes de CALL. `interrupt_dispatch()` agora retorna um
`struct interrupt_frame *` em RAX. `mov rsp, rax` adota o frame escolhido;
os POPs restauram os registradores da thread selecionada, removem somente
vector/error e executam IRETQ. A pilha anterior fica preservada para retomada.

O retorno pode apontar à mesma pilha ou à pilha de outra thread. O scheduler
valida o frame dentro dos limites da pilha proprietária, selectors de kernel,
RFLAGS e RIP canônico antes da restauração. Não existe anchor em RBX que
force o retorno à pilha antiga; RBX é restaurado como parte do contexto.

As threads compartilham CR3, espaço virtual, FS/GS e privilégios ring 0.
Não há SWAPGS nem salvamento de FPU/SSE/AVX. O kernel mantém a compilação
para registradores gerais; permitir outro estado de CPU exige ampliar o
contrato antes de usá-lo em threads.

## IRQ e yield

IRQ0 atualiza o PIT; IRQ1 coleta scancodes. O EOI do PIC precede a chamada do
scheduler no epílogo da interrupção externa. Somente então o frame selecionado
retorna ao Assembly. A seleção não aloca, não libera pilhas e não imprime logs.

`thread_yield_trap` executa `int 240; ret`. O vector 240 usa gate presente
DPL0, selector 0x08, IST=0 e atributo 0x8E, separado das IRQs PIC 32–47.
É uma chamada interna do kernel, não uma interface de syscall para ring 3.
O stub normaliza error code zero; o scheduler força a seleção quando permitido.

As exceções fatais mantêm diagnóstico e halt; não viram eventos de agendamento.
As stacks IST de exceções críticas continuam separadas das pilhas das threads.

## Pilhas, criação e fim de vida

A faixa de pilhas começa em `0xffffc00040000000`. Cada slot tem 69.632 bytes:
uma guard page de 4 KiB ausente, seguida de 64 KiB mapeados. Existem 64 slots,
com gerações que não dão wrap para rejeitar descritores antigos após reutilização.
As páginas mapeadas são supervisor, RW e NX quando disponível.
As permissões do alias HHDM preservam as limitações do [VMM](memory-management.md).

A pilha bootstrap continua sendo os 64 KiB de BSS delimitados pelos símbolos
`bootstrap_stack_bottom/top`; ela não ganha guard page nesta etapa.
Idle ocupa o primeiro slot novo. Uma thread dinâmica possui seu TCB no heap
e 16 frames de stack no PMM, sem exigir contiguidade física.

Criação valida nome ASCII de 1–23 caracteres, entry e saída, aloca TCB/pilha,
constrói o frame e só então publica a thread na fila. Falha esperada devolve
false e preserva a saída; recursos novos são desfeitos antes de retornar.
Falhas irrecuperáveis de ownership/cleanup causam panic.

O frame inicial é construído através do HHDM na última página da stack.
Tem CS=0x08, SS=0x10, RFLAGS=0x202 e RIP no trampoline de bootstrap.
RSP aponta para top−8, garantindo RSP%16=8 ao entrar em C. O slot nesse
endereço contém uma sentinela Assembly de halt. O trampoline chama a função
da thread com seu argumento e converte seu retorno em `thread_exit()`.

Exit marca ZOMBIE e troca de contexto; nunca libera a pilha que está executando.
`thread_reap()`, em outra thread, retira zombies do registro, desmapeia cada
página, confirma ausência antes de liberar frames e devolve o TCB ao heap.
A pilha atual, idle e bootstrap não são alvos normais de reap.

As páginas de dados das threads são devolvidas. Tabelas vazias do VMM e
páginas já expandidas do heap podem permanecer reservadas. Após todos os
workers terminarem, sem outros consumidores alterando memória:

```text
delta(PMM used frames) =
    delta(heap mapped bytes) / 4096 + delta(VMM table pages)
```

## Espera e preempção

`thread_sleep_ms(ms)` converte para ceil(ms/10) ticks sem overflow no
arredondamento; o deadline também é verificado antes da soma.
Zero equivale a yield. Sleeper volta à fila READY quando o PIT alcança seu
deadline; precisão segue a resolução e cadência do timer nominal.

Somente shell consome o teclado. `scheduler_wait_input()` faz a verificação
de input pendente e a transição a BLOCKED na mesma região IF=0. IRQ1 acorda
shell quando há input, evitando perder um evento entre checar e bloquear.
O ring buffer e sua política de overflow permanecem os existentes.

As operações de fila/ownership usam save/disable/restore de IF na CPU única.
`preempt_disable()/enable()` usam nesting por thread e mantêm IRQs habilitadas
fora da curta atualização do contador. O PIT continua contando, mas a troca
aguarda o último enable; uma solicitação pendente pode ser atendida nesse ponto.
Overflow/underflow do nesting é erro fatal.

Yield, sleep e exit não podem ocorrer em região não preemptível ou contexto
IRQ/NMI. APIs de bloqueio também exigem IF=1; IF local não sincroniza CPUs.
Logging protege suas operações de saída, e o shell protege clear/backspace
que acessam diretamente framebuffer e serial. O heap continua usando IF=0
durante suas transações, portanto pode aumentar a latência de IRQ.

## Shell, testes e evidência

`ps` e `threads` mostram snapshots reais em `TID STATE NAME`.
`schedulerstats` mostra contadores, filas, quantum e integridade.
`sleep <decimal-ms>` rejeita sinal, prefixo, lixo, overflow e argumentos extras.
Só imprime `Sleep completed.` depois de a API retornar sucesso.

`schedtest` é explícito e limitado; não roda no boot normal. Exercita workers
cooperativos, workers sem yield, sleeps, heap/realloc, padrões de stack e
16 batches de criação/exit/reap. O probe Assembly preserva os 15 GPRs e CF/DF
por INT 240. Um worker pronto adicional permanece sem executar enquanto duas
regiões de preempção estão aninhadas, inclusive após o primeiro enable.

Cada execução completa espera 77 threads criadas/terminadas/coletadas,
3.105 operações determinísticas, ao menos 4.000 trocas e preempções de timer.
`CPU-bound iterations` é separado porque depende do tempo de execução.
O autoteste compara heap/PMM/VMM, exige retorno às duas threads básicas e
nenhum zombie, e rejeita chamadas de bloqueio/criação com IF=0.

Os testes host cobrem política pura, nesting, overflow, corrupção de filas,
falhas de backing, descritores antigos, frame inicial e comandos do shell.
A inspeção ELF verifica bytes da entrada, retorno RAX, INT 240 e bootstrap.

Na preparação das fixtures novas de `tests/test_thread_stack.c`, a contagem
host foi agregada de 137.325 para 1.096 checks. Invariantes de callbacks e de
páginas continuam sendo examinados, mas agora contam por cenário; os casos
e bytes verificados não foram removidos. Essa mudança afeta somente a nova
suíte de pilhas. As 14 suítes host da v0.3 foram preservadas, com acréscimos
em interrupções, input/parser e comandos do shell. Os logs preliminares em
`validation-artifacts/astra-v04-incremental/host-first.log` e
`host-aggregated.log` registram as duas granularidades; os totais não medem
quantidade de casos únicos nem cobertura.

`scripts/test-scheduler-qemu.py` reutiliza os harnesses de heap/memória.
Prevê três schedtest, regressões heap/PMM/VMM, input real PS/2 via QMP,
guard pages, permissões, snapshot GDB de idle/shell bloqueada, IDT240 e halt.
A execução é headless, limitada por timeout e coleta seu próprio processo.

`fault stack` é um comando fatal separado: escreve na guard page idle em
`0xffffc00040000000`. O harness de memória aceita `--fault stack` e exige
Page Fault de escrita supervisor, error code 2, CR2 nesse endereço e consulta
VMM indicando ausência. Reinicie a VM depois do probe. Consultar uma guard
page ou escrever nela deliberadamente não testa overflow recursivo de stack.

O gate completo de v0.4 ainda está pendente. Resultados de execuções futuras
devem identificar ELF/ISO, versão e artefatos; não há validação visual, UEFI
ou em hardware físico implícita neste guia.
