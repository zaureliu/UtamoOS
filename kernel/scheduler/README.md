# Scheduler

A implementação fica em kernel/core/sched_core.c e scheduler.c. Threads de
kernel e processos CPL3 compartilham round-robin com quantum de dois ticks,
filas de prontos/sleepers e reap de zombies em outra stack. Pilhas dinâmicas
têm guard pages. O PIT fornece preempção; a thread idle espera interrupções.

Este diretório conserva o ponto de documentação da organização original.
Não há SMP nem estado FPU/TLS por tarefa. Veja
[contratos do scheduler](../../docs/scheduler.md),
[processos](../../docs/processes.md) e [gates](../../docs/astra-campaign-state.md).
