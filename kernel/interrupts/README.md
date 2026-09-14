# Interrupções — reservado para v0.1

Nenhum handler ou IDT própria está implementado. O BSP mantém IF=0.
Próximos contratos: frame de exceção em Assembly/C, GDT/TSS/IST, IDT, dispatch
por vetor e IRQ routing. Instalar e validar exceções antes de `sti`; `cli` não
mascara NMI nem exceções síncronas. Não há fonte C fictício neste diretório.
