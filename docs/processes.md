# Processos e isolamento de memória

**Estado: GREEN local — v0.5.0 compilada e validada no gate headless.**
O [registro v0.5](validation-astra-v0.5.json) identifica código, hashes e fases
observadas. Publicação, aceite visual, UEFI e hardware físico permanecem separados.

A v0.5 acrescenta processos com uma thread CPL3 por processo ao
[scheduler existente](scheduler.md). Threads de kernel continuam usando o
address space do kernel. Cada processo possui um PML4 privado, páginas de
usuário exclusivas e uma pilha de kernel para entrada por interrupção.
A interface de chamadas está em [syscalls.md](syscalls.md).

## Responsabilidades

| Arquivo | Responsabilidade |
| --- | --- |
| [process.c](../kernel/core/process.c) | Registro, construção, publicação, resultado e reap |
| [process_policy.c](../kernel/core/process_policy.c) | Validação pura do estado de retorno CPL3 |
| [process_syscall.c](../kernel/core/process_syscall.c) | Dispatch da ABI nativa |
| [user_vm.c](../kernel/memory/user_vm.c) | PML4 privado, ownership, cópias e destruição |
| [scheduler.c](../kernel/core/scheduler.c) | Thread proprietária, seleção do frame, CR3 e pilha de privilégio |
| [arch_user.c](../kernel/arch/x86_64/arch_user.c) | Precondições da CPU, rotas de entrada e política FS/GS/FPU |
| [user_probe.asm](../kernel/arch/x86_64/user_probe.asm) | Programas de teste incorporados, sem loader de arquivos |

O PCB tem PID, TID, estado, contadores e um contexto de VM em endereço estável.
Os callbacks de paginação apontam para esse contexto; copiá-lo ou movê-lo
depois da inicialização violaria o contrato de ownership.

## Address space privado

O domínio permitido para novos mappings de usuário é
[0x0000000000010000, 0x0000800000000000), com páginas de 4 KiB e
endereços canônicos de quatro níveis. A página zero e os primeiros 64 KiB
não fazem parte desse domínio.

user_vm_create() aloca um PML4 pelo PMM e zera seus 512 slots.
Copia os 256 slots superiores do kernel e limpa USER em cada entrada copiada.
Os 256 slots inferiores começam ausentes. A árvore superior permanece
compartilhada e supervisor; não é clonada ou liberada pelo processo.

O subtree permanente da arena dinâmica precisa existir antes dessa cópia.
Assim, novos mappings de heap e pilhas alteram tabelas compartilhadas já
alcançáveis pelos PML4s privados. Código, dados, GDT/TSS, IDT, stacks IST,
framebuffer e HHDM permanecem acessíveis em CPL0 durante a troca de CR3.
A presença desses aliases não concede acesso CPL3: USER precisa estar
habilitado em todos os níveis do walk.

A estrutura usa limites explícitos:

| Recurso | Limite atual |
| --- | --- |
| Processos simultâneos, incluindo os que aguardam reap | 16 |
| Páginas de usuário por processo | 128, ou até 512 KiB de mappings de dados |
| Tabelas privadas por processo | 64, incluindo PML4 |
| Histórico circular de resultados coletados | 64 |
| Threads totais no scheduler | 64, compartilhadas com shell, idle e workers |

Os limites de páginas e tabelas são independentes. Um espaço virtual esparso
pode atingir o limite de tabelas antes do limite de páginas. O histórico
sobrescreve resultados antigos após completar uma volta; não é armazenamento
persistente.

## Páginas, permissões e cópias

Cada frame de dados e cada tabela privada pertence exclusivamente a um
processo. Novas páginas são zeradas via HHDM antes de serem expostas.
Tabelas intermediárias vêm do PMM e são zeradas pelo core VMM antes da
publicação. O ledger diferencia tabelas alocadas de tabelas publicadas;
rollback devolve as não publicadas.

READ é implícito. A API aceita R+NX, RW+NX ou RX e rejeita WRITE|EXEC.
Folhas de usuário são de 4 KiB, USER e não GLOBAL. A política W^X aplica-se
aos mappings de usuário; o alias supervisor HHDM mantém a política do
[memory subsystem](memory-management.md), sem promessa de W^X global.

Map não substitui silenciosamente uma tradução existente. Query devolve
endereço físico com offset e permissões efetivas. Unmap remove o mapping e
libera seu frame de dados exclusivo. Tabelas privadas vazias ficam no ledger
até a destruição do address space. Diferentemente das tabelas permanentes do
kernel, essas tabelas não usam pin permanente no PMM.

Cópias validam a extensão completa antes de mover bytes: limites, overflow,
presença, USER efetivo, ownership dos frames e aliases HHDM. Escritas ainda
exigem RW. Todas as páginas são verificadas sob IF=0; falha de validação
preserva o destino. Não há dereference direto de ponteiro de usuário em C.
O limite genérico de cópia é 64 KiB; a syscall WRITE usa um limite menor,
descrito em [syscalls.md](syscalls.md).

INVLPG é emitido para mutações do address space atualmente ativo. Uma árvore
inativa não exige essa invalidação; sua ativação ocorre por CR3 sem PCID.
Não há TLB shootdown, SMP ou compartilhamento de páginas de usuário.

## Imagem e pilhas dos probes

Os programas atuais são um blob Assembly incorporado de até uma página.
Não existe loader ELF, executável de filesystem ou seleção arbitrária de
binário. O loader interno aloca código RW+NX, copia o blob e protege RX
antes de publicar a thread.

| Região virtual dos probes | Permissão/função |
| --- | --- |
| 0x00400000 até 0x00401000 | Código RX, USER |
| 0x00600000 até 0x00601000 | Dados privados RW+NX, USER |
| 0x6ffef000 até 0x6fff0000 | Guard page inferior ausente |
| 0x6fff0000 até 0x70000000 | Pilha de usuário de 64 KiB, RW+NX |

Os limites superiores da tabela são exclusivos. A pilha de usuário não
cresce automaticamente; o resto do lower half continua ausente. O RSP inicial
é 0x6ffffff8, com argumento de probe em RDI. O entry é Assembly e encerra
explicitamente pela syscall EXIT.

Cada thread CPL3 também recebe uma pilha supervisor de 64 KiB com guard
page inferior, usando o allocator de [pilhas do scheduler](scheduler.md).
Essa pilha pertence ao TCB e não ao conjunto de páginas USER. O frame inicial
de 176 bytes fica nessa pilha protegida, com CS=0x1b, SS=0x23 e RFLAGS=0x202;
o RSP salvo nele aponta para a pilha de usuário.

## GDT, TSS e retorno

| Seletor | Uso |
| --- | --- |
| 0x08 | Código de kernel |
| 0x10 | Dados de kernel |
| 0x1b | Código de usuário: slot 3, RPL3 |
| 0x23 | Dados/pilha de usuário: slot 4, RPL3 |
| 0x28 | TSS de 64 bits, ocupando slots 5 e 6 |

Antes da restauração, o scheduler atualiza TSS.RSP0 para o topo da pilha de
kernel selecionada. O setter exige IF=0, endereço higher-half canônico e
alinhamento de 16 bytes; ownership e mappings da pilha são responsabilidade
do chamador. Não há LTR ou reload da GDT a cada troca.

A seleção usa o CR3 original para threads de kernel e a raiz privada para
processos. Só escreve CR3 quando o valor muda. O frame da próxima tarefa
permanece acessível pelos mappings supervisor compartilhados durante essa
transição. O epílogo existente restaura GPRs e usa IRETQ.

Em long mode, uma entrada CPL3 para CPL0 obtém RSP0 da TSS e força SS de
kernel nulo; SS:RSP antigos ficam no frame. Isso permite que o handler
trabalhe com a pilha protegida mesmo quando o usuário fornece RSP inválido.
Uma interrupção aninhada em CPL0 poderia salvar SS=0. A política inicial
mantém IF=0 em syscalls, não usa INT240 aninhado e retorna diretamente um
frame ao epílogo. Kernel threads retomam seu próprio SS=0x10; retorno CPL3
restaura SS=0x23. Essa distinção segue o
[Intel SDM, volume 3A, seção 7.14.4](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).

## Falhas, filas e fim de vida

A localização do frame e seus valores têm validações separadas. Um frame
fora da pilha de kernel proprietária é corrupção interna fatal. RIP/RSP
hostis ou flags/seletores incompatíveis de usuário terminam o processo;
não são tratados como motivo para devolver um IRET inválido ao kernel.

O estado inicial exige IF e bit 1 de RFLAGS ligados. IOPL, NT, VM, VIF e VIP
são proibidos. TF, DF e AC permanecem estado arquitetural de usuário; a
entrada Assembly limpa DF antes de chamar C. A política não dereferencia
RIP/RSP nem exige que estejam mapeados: acessos inválidos continuam sujeitos
às exceções de CPU e à contenção de processo.

IRQ0 mantém ticks e IRQ1 mantém o wake da shell, inclusive quando a tarefa
interrompida apresenta estado de retorno inválido. O driver/PIC conclui EOI
antes de uma troca de contexto. Exceções comuns originadas em CPL3 produzem
diagnóstico serial e terminam somente o processo. Page fault acrescenta
uma consulta sem alocação; para higher-half, o snapshot considera o USER
negado pelo PML4 privado.

NMI, double fault e machine check continuam fatais, usando suas ISTs.
Exceções CPL0 continuam sendo falhas de kernel, inclusive durante syscalls.
Não existe recuperação genérica de acesso inválido em CPL0.

EXIT ou falha registra o resultado, marca o processo EXITED e a thread ZOMBIE,
e seleciona outro frame. Nenhum desses caminhos libera a própria pilha.
O reaper executa em outra thread, retira a tarefa do scheduler, devolve a
pilha supervisor, destrói a VM inativa e libera PCB/TCB. A destruição rejeita
CR3 ativo, valida ownership e devolve somente os frames exclusivos de dados
e tabelas, deixando o PML4 para o final. Tabelas superiores compartilhadas
não entram no ledger privado e não são liberadas.

Operações do registro/VM usam IF no BSP. Construção mantém preempção
desabilitada enquanto prepara recursos, permite IRQ entre transações e
publica somente o processo completo. Não há estado parcialmente construído
visível a outra thread.

## Política de CPU e limitações

Processos exigem NX anunciado por CPUID e EFER.NXE já habilitado, além de
long mode, paginação de quatro níveis e CR0.WP. Ausência de NX deixa o
subsistema de processos indisponível; kernel threads continuam disponíveis.
PCID, LA57, CET e PKE ativos são rejeitados nesta implementação.

CR0.TS impede uso de FPU/SIMD sem um contexto próprio; o probe x87 produz #NM.
Não há FXSAVE/XSAVE, TLS ou estado FPU compartilhável entre processos.
FS/GS selectors e bases são zerados antes do retorno CPL3, FSGSBASE é
desabilitado e não há SWAPGS. IOPL=0 e o offset do bitmap de I/O além do
limite da TSS negam port I/O em CPL3.

O escopo ainda não inclui ELF loader, arquivos, filesystem, VFS, POSIX,
fork, exec, waitpid, signals, múltiplas threads por processo, memória
compartilhada, copy-on-write, demand paging ou swapping.

## Validação registrada

Nenhum processo de teste roda no boot normal. O comando `usertest` executa
cinco pares GOOD isolados, um teste POINTERS, 13 probes fatais e um teste de
capacidade com 16 processos POINTERS simultâneos. A 17ª criação é rejeitada
sem alterar o PID de saída ou o accounting PMM/heap. Cada execução aprovada
cria e coleta 40 processos, contém 13 faults e completa 327.900 syscalls;
o kernel confere isolamento físico dos dados, páginas zeradas e retorno de recursos.

O [harness de processos](../scripts/test-process-qemu.py) envia comandos por
PS/2/QMP e observa o primeiro entry com breakpoint de hardware temporário.
O snapshot real mostra CS=0x1b, SS=0x23, RIP=0x400000, RSP=0x6ffffff8,
RFLAGS=0x202 e CR3 privado. Também verifica TSS.RSP0, GDT, IDT128, as quatro
camadas das páginas de código/dados/stack e a guard page ausente. Os dumps
seriais completos são correlacionados por PID aos resultados dos 13 probes.

A matriz candidata com banner 0.4.0 passou 10.257 checks QEMU em 19 VMs.
Após o stamp, a imagem 0.5.0 passou mais 3.115 checks em três VMs, incluindo
a suíte de processos, recusa sem NX e page fault fatal do kernel. Os 12
usertests dessas fases criaram/coletaram 480 processos, contiveram 156 faults
e completaram 3.934.800 syscalls. Todas as 22 VMs foram recolhidas.

Host e ELF finais passaram 25.541 e 1.707 checks. O total registrado,
contando host/ELF uma vez e ambas as fases QEMU, é 40.620 sem falhas.
`.text`, `.data` e `.limine_requests` são idênticos entre candidato e
stamp final; `.rodata` mudou um byte de versão. A matriz completa não foi
repetida após o stamp. As contagens não medem testes únicos ou cobertura.

[Índice e hashes](validation-astra-v0.5.json);
evidência serial final: `build/validation/astra-v05-final-process/serial.log`.
Fixtures host e UBSan complementam a execução observada, sem se passarem por
execução CPL3. Aceite visual, teclado físico, UEFI e hardware real não estão
incluídos nesse gate.
