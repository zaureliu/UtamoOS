# Interrupções e exceções do UTAMO OS 0.1.0

## Escopo

O kernel executa somente no BSP, em ring 0 e modo de 64 bits. Mantém paginação
de quatro níveis recebida do Limine. GDT/TSS próprias antecedem a instalação
da IDT; nenhuma interrupção mascarável deve ser habilitada antes de PIC e
drivers estarem prontos. As exceções são fatais neste marco, inclusive
breakpoint, debug e NMI; não há recuperação de processos nem debugger interno.

## GDT e IST

Seletores utilizados: código kernel `0x08`, dados kernel `0x10` e TSS
`0x28`. Slots `0x18` e `0x20` ficam nulos, reservados para futura definição
dos segmentos de usuário. O descritor TSS ocupa dois slots e o TSS contém
pilhas independentes de 16 KiB para IST1/#DF, IST2/NMI e IST3/#MC. Essas pilhas
estáticas não têm guard pages neste marco e não constituem recuperação de
stack overflow.

## IDT

A IDT contém 256 gates de 16 bytes, alinhada em 16 bytes. IDTR usa limite
`4095` e base de 64 bits; o operando de `lidt` tem 10 bytes. Todos os
gates são presentes, DPL0, tipo interrupt gate de 64 bits (`0x8e`), com
seletor de código kernel. Não existem gates de syscall ou de ring 3.

| Bytes no gate | Campo |
| --- | --- |
| 0–1 | Offset bits 0–15 |
| 2–3 | Seletor GDT |
| 4 | IST nos bits 0–2; demais bits zero |
| 5 | Atributos `0x8e` |
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
| 48–255 | Interrupção inesperada, diagnóstico fatal | 0 |

O uso de interrupt gates limpa IF ao entrar. IRQ handlers retornam por
`iretq`, que restaura o IF anterior; o handler não deve executar `sti`.
IRQ drivers não usam logger nem terminal.

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

O stub salva os quinze GPRs, executa `cld`, passa o frame em RDI e guarda
a base do frame em RBX. RBX é callee-saved pela ABI SysV AMD64. A pilha é
alinhada em 16 bytes imediatamente antes do CALL; depois da chamada, a base
salva recupera qualquer espaço de alinhamento. A rotina restaura os quinze
GPRs, descarta apenas vector/error_code e executa `iretq`. O RFLAGS salvo
preserva DF do contexto interrompido. Não há `swapgs`, troca de CR3,
salvamento de SIMD/FPU ou mudança de privilégio. O kernel mantém
`-mno-red-zone -mgeneral-regs-only`.

## Diagnóstico de exceções

O caminho fatal executa `cli`, registra uma guarda de recursão, lê CR2
para #PF e emite **todo** o relatório por polling COM1 antes de tocar o
framebuffer. A serial tem limite de polling e pode estar ausente. O diagnóstico
não usa o logger global, não espera IRQ e não adquire locks. Em seguida,
reinicializa o terminal registrado e imprime o relatório gráfico, quando houver.

Uma exceção durante a saída gráfica encontra a guarda e tenta apenas uma
mensagem serial curta, terminando em `cpu_halt`. Uma segunda falha nesse
caminho curto para sem tentar outra emissão. Isto limita recursão; não promete
recuperação diante de corrupção de memória, machine check ou hardware defeituoso.

O relatório contém nome, vetor, error code, RIP, CS, SS, RFLAGS, RSP e os
quinze GPRs. Valores hexadecimais têm dezesseis dígitos. Para #PF, inclui
CR2 e decodifica P, W/R, U/S, RSVD e I/D. Bits adicionais permanecem disponíveis
no error code bruto. Nenhum VMM é implementado.

## Probes controlados

- `exception_fault_ud2()`: executa UD2 e provoca #UD/vetor 6.
- `exception_fault_div0()`: divide em Assembly por zero e provoca #DE/vetor 0;
  não depende de comportamento indefinido em C.
- `exception_fault_page()`: escreve em `0x00007ffffffff000`, endereço
  canônico deixado sem mapping pelo bootstrap Limine atual. Espera #PF/vetor
  14, CR2 igual ao endereço e W/R=1; precisa ser revisado se o VMM futuro mapear
  essa página.

Os probes são expostos pelo shell como `fault ud2`, `fault div0` e
`fault pf`. Nunca são chamados no boot normal. Cada teste fatal exige novo
boot; nenhuma exceção fatal é tratada como comando retornável.

## Evidência e limites de testes

`tests/test_interrupts.c` testa bytes de gate contra representação
arquitetural independente, rejeição sem mutação, limites canônicos, nomes,
decodificação dos flags de #PF e relatório dos registradores. Não executa
`lidt`, `iretq` ou qualquer instrução privilegiada no host.

Build e testes host não validam a ABI real de uma interrupção. A validação
de hardware exige QEMU, observando o dump de UD2/DIV0/#PF e IRQs repetidas
retornando ao loop principal sem corromper registradores. As evidências
efetivamente obtidas ficam em `docs/development-log.md`; este documento
define o contrato, não declara resultados de execução por antecipação.
