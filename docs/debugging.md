# Debugging UTAMO OS 0.1.0

O desenvolvimento usa o repositório existente em `~/UtamoOS`, no Ubuntu WSL.
GDT, IDT, uma exceção UD2 real, PIC e avanço do contador PIT já tiveram
validação incremental em QEMU **headless**, por serial e GDB. Os comandos deste
documento permitem repetir essas observações; só uma execução concluída gera
evidência. O [relatório da implementação](v0.1-implementation-report.md) e o
[development log](development-log.md) registram resultados e limitações.

Toda execução automatizada mantém `-display none`, uma única VM por vez e
tempo limitado. Digitação PS/2, edição de linha, comandos pelo teclado e
aparência do framebuffer permanecem **PENDENTES DE VALIDAÇÃO MANUAL**, conforme
o escopo de validação solicitado. Nenhuma janela gráfica foi necessária para
os testes incrementais descritos aqui.

## Build e inspeção antes da VM

Execute na raiz do projeto, com a toolchain existente:

~~~sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
python3 scripts/inspect-elf.py
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
~~~

O GCC nativo compila apenas os executáveis de teste host. O kernel continua
C17 freestanding, com o cross compiler x86_64-elf. `make inspect` exibe
`readelf` e `nm -u`; `inspect-elf.py` acrescenta verificações estruturais
com resultado explícito. Consulte a descrição de cada script em
[scripts/README.md](../scripts/README.md).

Confira ELF64 x86-64 `EXEC`, entry point `_start`, segmentos `LOAD` com
permissões separadas, ausência de `INTERP`/`DYNAMIC`, símbolos indefinidos
e segmentos simultaneamente graváveis e executáveis. O mapa de link fica em
`build/utamo-kernel.map`. `-mgeneral-regs-only` mantém o código C fora do
contexto FPU/SIMD, ainda não administrado pelo kernel.

O NASM 3.01 usa `-Werror -Wno-error=reloc-rel-dword`: a exceção é específica
ao diagnóstico de relocation já conhecido. Não remova `-Werror` para contornar
uma falha. Os offsets dos interrupt stubs ficam na mesma seção do código;
a inspeção do ELF verifica o resultado efetivamente ligado.

## Boot e timer sem interface gráfica

Depois de gerar a ISO, execute uma VM por vez:

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name boot-v01 \
    --check-gdt --check-idt --check-timer
~~~

O script usa q35/TCG, CPU `qemu64`, 256 MiB, um core, sem rede, e serial
em arquivo. Escolhe a ISO UTAMO quando há exatamente uma em `build/`;
use `--iso build/utamo-os-0.1.0.iso` se houver mais de uma.
O timeout padrão é 90 segundos. O arquivo de lock serializa as instâncias do
harness; uma busca em `/proc` recusa iniciar se outro QEMU já estiver rodando.
Isso não autoriza encerrar VMs criadas por outra pessoa ou outro processo.

`--check-gdt` observa CS e GDTR; `--check-idt` também verifica IDTR com
limite `0xfff`, correspondente a 256 gates de 16 bytes.
`--check-timer` lê o contador real `ticks` via GDB, deixa a VM executar
por 0,6 segundo e faz outra leitura. A diferença positiva demonstra IRQs do
PIT atravessando a IDT/PIC e retornando à execução; não altera o contador.
`--timer-symbol` permite indicar outro nome se esse símbolo for refatorado.
As interrupções ficam prontas antes de IF ser habilitado.

A COM1 de saída usa polling, 115200 baud, 8N1. Ela não é uma entrada de shell.
Texto escrito no terminal host conectado à serial não testa IRQ1 nem o
teclado PS/2. O loop ocioso usa `sti; hlt`; esse repouso permite interrupções,
enquanto `cpu_halt` usa `cli; hlt` e não retoma a operação normal.

## Exceções controladas por GDB

Os probes não são executados no boot normal. Para testar a imagem final sem
entrada de teclado, redirecione a execução no primeiro repouso, depois de o
shell e a infraestrutura de interrupções estarem prontos:

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name fault-ud2-v01 \
    --check-gdt --check-idt --probe exception_fault_ud2 \
    --probe-at cpu_wait_interrupt --debug
python3 scripts/test-qemu.py --marker "utamo> " --name fault-div0-v01 \
    --check-gdt --check-idt --probe exception_fault_div0 \
    --probe-at cpu_wait_interrupt --debug
python3 scripts/test-qemu.py --marker "utamo> " --name fault-page-v01 \
    --check-gdt --check-idt --probe exception_fault_page \
    --probe-at cpu_wait_interrupt --debug
~~~

São três execuções sequenciais e independentes. Cada probe inicia QEMU com
`-S`, conecta GDB por socket Unix local ao projeto e estabelece um breakpoint
de hardware em `cpu_wait_interrupt`. Quando o kernel chega a esse ponto,
o harness remove o breakpoint, direciona RIP ao probe, limpa IF e retoma a
CPU. O kernel executa a instrução real que causa a exceção. GDT, IDT, stubs e
diagnósticos continuam sendo os mesmos da imagem normal.

O teste compara nome/vector, error code normalizado, registradores, selectors
CS/SS e endereços canônicos RIP/RSP. Para page fault, compara também CR2 com
`0x00007ffffffff000` e verifica acesso de escrita supervisor a página ausente.
No fim, observa RIP estável, IF=0 e `HLT=1`; uma linha de texto isolada não
basta para comprovar a parada da CPU.

O primeiro teste incremental de GDB tentou alterar RIP após a CPU já estar
em `HLT` e ficou aguardando `stepi`. No QEMU observado, essa alteração não
retirou a CPU do estado interno halted. A correção foi parar **antes** de HLT
com `-S` e breakpoint de hardware. O novo teste UD2 passou 37 checks, com
o processo QEMU encerrado corretamente. No marco anterior ao shell, esse
breakpoint era `cpu_halt`; na imagem final use `cpu_wait_interrupt`.
O timeout original não foi tratado como aprovação.

## Inspeção interativa pelo debugger

Para investigar o início do boot, primeiro conclua os builds e encerre qualquer
VM anterior. Uma sessão manual de debugger também pode permanecer headless e
limitada a 120 segundos:

~~~sh
timeout --signal=TERM --kill-after=2s 120s \
    qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
    -cdrom build/utamo-os-0.1.0.iso -boot d -display none \
    -serial file:build/debug-serial.log -monitor none -nic none \
    -no-reboot -no-shutdown -S -gdb tcp:127.0.0.1:1234
~~~

Em outro terminal do mesmo projeto:

~~~sh
gdb build/utamo-kernel.elf
~~~

~~~gdb
set pagination off
set architecture i386:x86-64
target remote 127.0.0.1:1234
hbreak kernel_main
continue
info registers
x/12i $rip
bt
monitor info registers
monitor info pic
~~~

Um breakpoint de hardware funciona antes de Limine mapear o endereço virtual
do kernel. Os frame pointers e DWARF ajudam no diagnóstico, mas `-O2` pode
eliminar variáveis. Use também breakpoints em `interrupt_dispatch`,
`kernel_panic`, `cpu_wait_interrupt` e `cpu_halt`.
Não use `load` para substituir a imagem carregada pelo bootloader.

O endpoint fica restrito a loopback; o harness usa um socket Unix no projeto.
A [documentação GDB do QEMU](https://www.qemu.org/docs/master/system/gdb.html)
descreve ambos os meios de conexão.
Ao sair, encerre a VM que abriu; `detach` por si só apenas desconecta o GDB.

## Evidências e diagnóstico

Cada `--name` cria um diretório novo em `build/validation/`, sem sobrescrever
uma execução anterior. `report.json` contém horário, revisão/status do Git,
hashes do ELF/ISO, versão e argumentos do QEMU, checks e confirmação de
encerramento do processo. A serial e as inspeções GDB/HMP ficam ao lado.
`--debug` acrescenta `qemu-debug.log` com interrupções, resets e guest errors.
O script sempre recolhe o processo que criou, incluindo falhas e timeouts.

`make clean` remove todo `build/`, inclusive essas evidências. Registre os
resultados no development log antes de limpar; mantenha as evidências da
validação final disponíveis para revisão. Uma nova compilação não torna
automaticamente válidos os resultados obtidos com outro hash de ELF/ISO.

| Sintoma | Inspeção útil |
| --- | --- |
| Limine não encontra o kernel | ISO, `boot/limine.conf`, caminho do ELF e assets Limine fixados |
| Serial vazia | `hbreak _start`, entry point, stack e inicialização de COM1 |
| Triple fault ou reset | `--debug`, base/limite IDT, selectors GDT, stack e gate do vector |
| GP na entrada/saída de IRQ | Gate, selector, frame, alinhamento antes de CALL e `iretq` |
| Page fault | CR2, error code, RIP e bits P/W/R/U/S/RSVD/I/D do relatório |
| Ticks não avançam | IF, divisor do PIT, máscaras/offsets PIC, IRQ0 e EOI |
| Teclado não produz comandos | Inicialização PS/2, máscara IRQ1, fila de scancodes e parser; interação ainda manual |
| Serial completa e framebuffer incompleto | Endereço, pitch, RGB e saída de emergência; revisão visual ainda manual |
| Relocation ou seção órfã | Objeto de origem, `objdump -dr`, linker script e mapa |
| GDB não retoma um probe depois de HLT | Reiniciar com `-S` e breakpoint antes de HLT; não injetar outra exceção para acordar a CPU |

`cli` não bloqueia NMI, exceções nem machine checks. O relatório fatal escreve
serial antes de framebuffer e tem contenção de recursão, mas não substitui
um debugger nem um gerenciador de memória virtual. Não há scheduler,
recuperação de page fault ou backtrace automático no kernel.

## Validação manual pendente

O harness mantém `--suite` e `--fault` preparados para uma sessão futura
explicitamente escolhida pelo usuário. Essas opções enviam teclas por QMP
ao controlador PS/2 emulado; não devem ser contabilizadas como executadas
nesta validação headless. `--capture-framebuffer` é opt-in e também não
foi usado para afirmar correção visual.

Permanecem para revisão manual: entrada de letras/números/sinais/Shift,
backspace/Enter, continuidade de IRQ1, comandos help/version/sysinfo/mem/
uptime/echo/clear/halt/fault pelo teclado, prompt/cursor e legibilidade do
framebuffer. UEFI e hardware físico precisam de evidência própria se não
constarem como executados no relatório da entrega. Aprovação de host tests,
build ou boot serial não preenche esses itens.
