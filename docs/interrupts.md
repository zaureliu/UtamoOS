# Interrupções e exceções do UTAMO OS

O frame e os probes do baseline v0.2 são preservados. Este guia inclui as
extensões de scheduler e CPL3; resultados históricos continuam nos
[registros da campanha](astra-campaign-state.md), sem validar uma nova imagem.

## Escopo

O kernel executa no BSP em ring 0, com processos isolados em CPL3 quando NX
está disponível. A paginação usa quatro níveis e raízes privadas de usuário.
GDT/TSS próprias antecedem a instalação da IDT; nenhuma interrupção mascarável
deve ser habilitada antes de PIC e drivers estarem prontos. Exceções de kernel
e NMI/Double Fault/Machine Check são fatais. Demais falhas de CPL3 encerram
somente o processo e retornam a outra tarefa; não há page-in nem debugger interno.

## GDT e IST

Seletores utilizados: código kernel `0x08`, dados kernel `0x10` e TSS
`0x28`. Slots `0x18` e `0x20` contêm segmentos DPL3; seus seletores com
RPL3 são CS `0x1b` e SS `0x23`. RSP0 recebe a stack protegida da tarefa
antes de entrar em CPL3. O descritor TSS ocupa dois slots e o TSS contém
pilhas independentes de 16 KiB para IST1/#DF, IST2/NMI e IST3/#MC. Essas pilhas
estáticas não têm guard pages neste marco e não constituem recuperação de
stack overflow.

## IDT

A IDT contém 256 gates de 16 bytes, alinhada em 16 bytes. IDTR usa limite
`4095` e base de 64 bits; o operando de `lidt` tem 10 bytes. Todos os
gates são presentes, tipo interrupt gate de 64 bits, com seletor de código
kernel. DPL0 (`0x8e`) é o padrão; o vetor 128 usa DPL3 (`0xee`) quando
a infraestrutura de processos está pronta. É a única entrada de syscall admitida.

| Bytes no gate | Campo |
| --- | --- |
| 0–1 | Offset bits 0–15 |
| 2–3 | Seletor GDT |
| 4 | IST nos bits 0–2; demais bits zero |
| 5 | Atributos `0x8e`; `0xee` para INT128 habilitado |
| 6–7 | Offset bits 16–31 |
| 8–11 | Offset bits 32–63 |
| 12–15 | Reservado, zero |

Cada vetor possui seu próprio stub, inclusive os reservados. Uma tabela de
offsets relativos com sinal de 32 bits reside junto aos stubs na seção RX
`.text`. O C reconstrói cada endereço por extensão de sinal e adição sem
sinal à base da tabela. A representação dispensa relocations absolutas
entre seções e não exige exceção adicional aos warnings NASM 3.01. `idt_set_gate`
rejeita índice fora de 0–255; o encoder puro rejeita endereços zero ou
não canônicos, seletor nulo/LDT/RPL diferente de zero e IST maior que 7.
Somente após construir todos os gates, `idt_init` executa `lidt`.

| Vetores | Uso | IST |
| --- | --- | --- |
| 0–31 | Exceções de CPU; nomes de vetores reservados mantidos | 0, exceto abaixo |
| 2 | NMI, fatal nesta versão | 2 |
| 8 | Double Fault | 1 |
| 18 | Machine Check | 3 |
| 32–47 | IRQ0–IRQ15 do PIC remapeado | 0 |
| 128 | Syscall nativa INT128, DPL3 quando disponível | 0 |
| 240 | Yield interno do scheduler, DPL0 | 0 |
| Demais vetores | Entrada inesperada: fatal em kernel, encerra processo em CPL3 | 0 |

O uso de interrupt gates limpa IF ao entrar. IRQ handlers retornam por
`iretq`, que restaura o IF anterior; o handler não deve executar `sti`.
IRQ drivers não usam logger/terminal, não alocam frames e não modificam page tables.

## Convenção do frame Assembly/C

`struct interrupt_frame` corresponde exatamente aos 176 bytes iniciados
em RDI no `call interrupt_dispatch`. Todos os campos são `uint64_t`.

| Offset | Conteúdo |
| --- | --- |
| 0–56 | r15, r14, r13, r12, r11, r10, r9, r8 |
| 64–112 | rbp, rdi, rsi, rdx, rcx, rbx, rax |
| 120 | vector |
| 128 | error_code real ou zero sintético |
| 136 | rip |
| 144 | cs |
| 152 | rflags |
| 160 | rsp interrompido |
| 168 | ss interrompido |

No **modo de 64 bits**, a CPU salva SS:RSP incondicionalmente, mesmo sem
mudança de privilégio. A convenção não se aplica a 32 bits ou compatibility
mode. `iretq` restaura o frame completo. A realocação/alinhamento da pilha
pelo hardware é possível porque o RSP anterior foi salvo.
Referência: [Intel SDM, volume 3A, seções 6.14.2–6.14.4](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf).

Os vetores com error code da CPU são 8, 10, 11, 12, 13, 14, 17, 21, 29 e 30.
Os demais stubs inserem zero antes do vetor. Os vetores 21/#CP, 29/#VC e 30/#SX
são preparados para arquiteturas que implementam essas extensões, mesmo sem
ativá-las no QEMU atual. **Não teste esses vetores com `int n`**:
uma interrupção por software não fornece o error code que o stub espera.

O stub salva os quinze GPRs, executa `cld` e passa o frame em RDI. A pilha é
alinhada em 16 bytes imediatamente antes do CALL. O dispatcher retorna em RAX
o frame escolhido; `mov rsp, rax` permite retornar à tarefa atual ou trocar
de contexto. O scheduler valida ownership/estado do frame, prepara TSS/RSP0
e troca CR3 quando necessário. A rotina restaura os quinze GPRs, descarta
vector/error_code e executa IRETQ, incluindo retorno a CPL3.
O RFLAGS salvo preserva DF do contexto interrompido. Não há swapgs ou estado
SIMD/FPU por tarefa. O kernel mantém `-mno-red-zone -mgeneral-regs-only`;
FS/GS são zerados e a ABI rejeita uso de FPU/SIMD. Veja
[scheduler](scheduler.md), [processos](processes.md) e [syscalls](syscalls.md).

## Diagnóstico de exceções

O caminho fatal executa `cli`, registra uma guarda de recursão, lê CR2
para #PF e emite **todo** o relatório por polling COM1 antes de tocar o
framebuffer. A serial tem limite de polling e pode estar ausente. O diagnóstico
não usa o logger global, não espera IRQ e não adquire locks. Para page fault,
depois de terminar esse relatório original, consulta o VMM sem alocar e
acrescenta presença, tradução, flags efetivas e tamanho da folha, ou indica
query indisponível. Só depois reinicializa o terminal registrado e tenta
o relatório gráfico, incluindo o mesmo snapshot.

Uma exceção durante a consulta VMM ou saída gráfica encontra a guarda e tenta apenas uma
mensagem serial curta, terminando em `cpu_halt`. Uma segunda falha nesse
caminho curto para sem tentar outra emissão. Isto limita recursão; não promete
recuperação diante de corrupção de memória, machine check ou hardware defeituoso.

O relatório contém nome, vetor, error code, RIP, CS, SS, RFLAGS, RSP e os
quinze GPRs. Valores hexadecimais têm dezesseis dígitos. Para #PF, inclui
CR2 e decodifica P, W/R, U/S, RSVD e I/D. Bits adicionais permanecem disponíveis
no error code bruto. O caminho fatal mantém o diagnóstico do VMM v0.2:
não resolve page faults, não faz page-in e não aloca no caminho de exceção.
Uma falha CPL3 não crítica emite o diagnóstico com buffers de kernel,
marca o processo encerrado e seleciona outra tarefa; o reaper libera sua
VM/stack mais tarde. NMI, Double Fault e Machine Check conservam o caminho fatal.
A consulta distingue endereço ausente de walk indisponível/inseguro.
O dump original precede a consulta para continuar útil se as tabelas estiverem
corrompidas. [Gerenciamento de memória](memory-management.md) detalha o contrato.

## Probes controlados

- `exception_fault_ud2()`: executa UD2 e provoca #UD/vetor 6.
- `exception_fault_div0()`: divide em Assembly por zero e provoca #DE/vetor 0;
  não depende de comportamento indefinido em C.
- `exception_fault_page()`: escreve em `0x00007ffffffff000`, endereço
  canônico deixado sem mapping pelo bootstrap Limine atual. Espera #PF/vetor
  14, CR2 igual ao endereço e W/R=1; precisa ser revisado se o VMM futuro mapear
  essa página.
- `memory_fault_unmapped()`: aloca/mapeia na arena VMM, remove/libera e escreve
  em 0xffffc00000000000; espera page fault ausente, error code 2.
- `memory_fault_readonly()`: cria página de teste, remove RW e tenta escrever;
  com CR0.WP espera page fault de proteção, error code 3.
- `memory_fault_nx()`: tenta executar a página de teste NX; exige EFER.NXE e
  espera page fault de instruction fetch, error code 17.

O shell expõe `fault ud2`, `fault div0`, `fault pf`, `fault vmm` e
`fault stack` (escrita na guard page da thread idle).
RO/NX são probes internos selecionados pelo harness/GDB, não comandos
`fault ro` ou `fault nx`. Nenhum probe é chamado no boot normal. Cada teste fatal exige novo
boot; nenhuma exceção fatal é tratada como comando retornável.

## Evidência e limites de testes

`tests/test_interrupts.c` testa bytes de gate contra representação
arquitetural independente, rejeição sem mutação, limites canônicos, nomes,
decodificação dos flags de #PF, registradores e formatação do snapshot VMM. Não executa
`lidt`, `iretq` ou qualquer instrução privilegiada no host.

Build e testes host não validam a ABI real de uma interrupção. A validação
de hardware exige QEMU, observando o dump de UD2/DIV0/#PF e IRQs repetidas
retornando ao loop principal sem corromper registradores. As evidências
efetivamente obtidas ficam em `docs/development-log.md`; este documento
define o contrato, não declara resultados de execução por antecipação.
