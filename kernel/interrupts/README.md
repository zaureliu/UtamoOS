# Interrupções

A versão 0.1.0 implementa IDT completa, stubs x86_64, contexto normalizado,
diagnósticos fatais e dispatch de IRQ PIC.
Veja [interrupts.md](../../docs/interrupts.md).
IRQs não logam. Exceções imprimem primeiro na serial e depois tentam o terminal.
