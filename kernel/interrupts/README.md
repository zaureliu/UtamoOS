# Interrupções

A base v0.1.0 implementa IDT completa, stubs x86_64, contexto normalizado,
diagnósticos fatais e dispatch de IRQ PIC.
Veja [interrupts.md](../../docs/interrupts.md).

No v0.2, page faults acrescentam um snapshot VMM sem alocação, depois de
terminar o dump original inteiro na serial. O diagnóstico diferencia mapping
ausente de query indisponível e não tenta recuperar a página.
O framebuffer é tentado somente depois da saída serial.
A guarda de recursão continua ativa caso o walk ou framebuffer falhe.

IRQs não logam, não alocam frames e não alteram tabelas de páginas.
PMM/VMM usam exclusão por IF no BSP; não há contrato SMP.
Veja [memory-management.md](../../docs/memory-management.md).
