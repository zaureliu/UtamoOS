# ABI nativa de syscalls

**Estado: GREEN local — ABI executada em CPL3 no gate v0.5.0.**
O [registro de validação](validation-astra-v0.5.json) distingue a matriz candidata
da confirmação da versão final. A ABI continua própria, sem compatibilidade Linux/POSIX.

A ABI é específica do UTAMO OS. A fonte dos números é
[syscall_abi.h](../kernel/include/utamo/syscall_abi.h).
O [gerador](../scripts/gen-nasm-constants.py) produz o include NASM em
build/generated/syscall_abi.inc; payloads não mantêm uma segunda lista
manual de números. Leia [processes.md](processes.md) para ownership,
address spaces e política de CPU.

## Entrada e registradores

A chamada usa INT 0x80, vector 128. RAX contém o número na entrada e o
resultado na saída; RDI, RSI e RDX contêm argumentos. Os demais GPRs,
inclusive os registradores dos argumentos, são preservados pelo frame.
Erros usam valores negativos de 64 bits em RAX.

O gate IDT128 tem atributo 0xee: presente, interrupt gate de 64 bits, DPL3,
selector de código de kernel 0x08 e IST=0. Sua habilitação acontece somente
após a preparação arquitetural de usuário. As outras gates continuam DPL0;
INT240 é uma interface interna do scheduler, não uma syscall de usuário.

A escolha de INT/IRET reaproveita os stubs, a TSS e o frame completo já
usados pelo scheduler. Não introduz um segundo formato de contexto com
SYSCALL/SYSRET. EFER.SCE é limpo, e IA32_SYSENTER_CS é zerado somente quando
CPUID.SEP confirma esses MSRs. Assim, rotas rápidas herdadas do bootloader
não viram entradas alternativas sem tratamento. A configuração preserva
os demais bits e verifica readback antes de disponibilizar processos.

## Chamadas implementadas

| Número | Nome | Argumentos | Resultado |
| ---: | --- | --- | --- |
| 1 | WRITE | RDI: destino 1 ou 2; RSI: endereço; RDX: bytes | Quantidade solicitada, ou erro |
| 2 | EXIT | RDI: status, interpretado como inteiro de 64 bits com sinal | Não retorna ao processo |
| 3 | GETPID | Nenhum | PID do processo atual |
| 4 | YIELD | Nenhum | 0 quando a tarefa retomar |
| 5 | SLEEP | RDI: milissegundos sem sinal | 0 quando a tarefa retomar, ou EINVAL |

Destinos 1 e 2 de WRITE seguem os mesmos sinks de logging do kernel. Não há
tabela de descritores de arquivos ou streams POSIX. A saída é contada por
bytes, não interpretada como format string e não exige terminador NUL.
O resultado indica bytes entregues ao logger; não promete confirmação de
transmissão por hardware ou persistência.

WRITE valida primeiro o destino, depois o limite de **1024 bytes**, depois
o buffer. Copia todo o conteúdo para um array limitado na pilha do kernel
antes de emitir saída. Uma falha de validação não imprime um prefixo parcial.
Length zero retorna 0 para destino válido e ignora o endereço fornecido.

EXIT preserva os 64 bits do status, registra o resultado e marca a thread
para coleta posterior. O usuário não escolhe a hora de liberar sua pilha
nem recebe acesso ao PCB.

GETPID não aloca recursos. IDs de processo e de thread são campos distintos.
YIELD pode selecionar a mesma thread se nenhuma outra estiver pronta.
SLEEP zero equivale a YIELD; valores positivos são convertidos para
ceil(ms/10) ticks do PIT nominal de 100 Hz. Overflow da conversão ou da soma
do deadline resulta em EINVAL. Não há garantia de latência de tempo real.

## Erros

| Valor | Nome | Situação atual |
| ---: | --- | --- |
| -1 | EINVAL | WRITE acima de 1024 bytes; duração/deadline de SLEEP inválido |
| -2 | EFAULT | Extensão de buffer inválida, ausente ou sem acesso permitido |
| -3 | ENOSYS | Número de syscall não implementado |
| -4 | EBADF | Destino WRITE diferente de 1 e 2 |

Esses nomes e valores pertencem à ABI UTAMO; não devem ser inferidos a partir
de headers do host. Não há variável errno, runtime de libc nem wrappers de
syscall Linux.

Um exemplo Assembly usando o include gerado:

~~~nasm
%include "build/generated/syscall_abi.inc"

mov eax, UTAMO_SYS_GETPID
int 0x80
; RAX recebe o PID; os outros GPRs continuam preservados.
~~~

## Buffers e limite de confiança

RSI é um endereço no espaço virtual do processo, não um ponteiro C confiável.
A extensão precisa caber inteiramente no lower half permitido, sem overflow.
O helper percorre todas as páginas, exige presença e USER efetivo e confirma
que os frames pertencem ao processo. O acesso físico usa aliases supervisor
HHDM validados. Uma cópia para usuário também exige RW.

O preflight de todas as páginas ocorre sob IF=0 antes de qualquer memcpy.
Não se usa tratamento de page fault como substituto da validação de ponteiros.
O syscall não dereferencia diretamente a pilha ou o buffer de usuário e não
aloca memória para WRITE. Isso também evita depender de uma recuperação
genérica de page fault em CPL0.

A extensão genérica do helper de cópia pode chegar a 64 KiB, mas o dispatch
WRITE mantém seu limite próprio de 1024 bytes. A interface não expõe map,
unmap, PMM, page tables, endereços físicos ou operações arbitrárias do kernel.

## IF, scheduler e retorno seguro

A interrupt gate limpa IF. O dispatcher mantém syscalls com IF=0 do começo
ao epílogo, inclusive na saída limitada de WRITE. Isso restringe a quantidade
de trabalho por chamada, mas ainda pode aumentar a latência do PIT/teclado;
1024 bytes não constituem garantia de tempo máximo em hardware.

YIELD, SLEEP e EXIT operam diretamente sobre o frame CPL3 e as filas.
Retornam o frame que o Assembly deve restaurar, sem chamar as APIs de
bloqueio de kernel que exigem IF=1 e sem executar INT240 aninhado.
Nenhum caminho normal habilita IRQ no meio da syscall.

TSS.RSP0 fornece a pilha de kernel e IRETQ restaura RIP, CS, RFLAGS, RSP e SS.
A localização do frame é validada contra a pilha supervisor proprietária.
Os valores de retorno exigem seletores de usuário, IF=1, IOPL=0 e endereços
RIP/RSP dentro do domínio canônico permitido. Um RSP hostil capturado na
entrada encerra o processo antes de IRET; não é usado para executar C.

Long mode pode deixar SS=0 no kernel após a mudança de privilégio. A política
sem IRQ/INT aninhado evita exigir que esse contexto intermediário tenha o
SS=0x10 de uma thread de kernel. A seleção restaura o frame inteiro da tarefa
escolhida. O mecanismo de stack switching está descrito no
[Intel SDM, volume 3A, seção 7.14.4](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).

## Falhas e recursos ainda ausentes

Exceções comuns CPL3 encerram o processo e preservam o kernel. NMI, double
fault e machine check continuam fatais com IST; falhas CPL0 continuam
recebendo tratamento de erro de kernel. Não há signals, handler de exceção
de usuário ou retomada automática de uma instrução que falhou.

FPU/SIMD e TLS não fazem parte da ABI: não existe salvamento de estado
FP/vector, FS/GS é normalizado e CR0.TS nega o uso sem contexto.
Port I/O é negado por IOPL/TSS. SYSCALL/SYSENTER permanecem desabilitados;
somente INT128 é uma entrada de usuário suportada.

Não há fork, exec, waitpid, arquivos, filesystem, sockets, memória compartilhada
ou compatibilidade POSIX. O programa incorporado testa esta ABI; ele não é
evidência de um loader geral ou de um userspace completo.

## Evidência

Fixtures host exercitam os contratos sem executar instruções privilegiadas.
Os probes incorporados incluem números desconhecidos, destinos inválidos,
tamanhos excessivos, buffers de kernel/não canônicos/ausentes, travessia de
página, overflow e length zero.

O gate headless observou a entrada real CPL3 por GDB, WRITE/GETPID/YIELD/SLEEP/EXIT,
327.900 syscalls por usertest, erros de ponteiro e contenção das instruções hostis.
Os 12 usertests registrados completaram 3.934.800 syscalls e 156 faults contidos;
a shell, IRQs e regressões de kernel continuaram operacionais após a coleta.

O total v0.5 é 40.620 checks sem falhas: 25.541 host e 1.707 ELF finais,
10.257 QEMU da matriz candidata 0.4.0 e 3.115 QEMU da confirmação 0.5.0.
Todas as 22 VMs foram recolhidas. Os bytes executáveis, dados e requests são
idênticos entre fases, com um byte de versão diferente em rodata; a matriz
completa não foi repetida após o stamp. [O registro](validation-astra-v0.5.json)
mantém hashes, limites e UBSan suplementar fora dessa soma. Contagens não são
cobertura, publicação ou aceite manual.
