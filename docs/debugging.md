# Debugging — UTAMO OS v0.2

Execute os comandos na raiz do checkout Linux/WSL2, usando a toolchain existente.
Este guia descreve como repetir verificações; a existência de um teste não
equivale a sua execução. Resultados e limitações são registrados no
[development log](development-log.md). Os
[resultados v0.1](v0.1-implementation-report.md) permanecem históricos.

A automação usa `-display none`, uma VM por vez e timeout.
Na evolução v0.2, entrada QMP no PS/2 emulado foi autorizada para testes
headless. Isso permite exercitar IRQ1 e comandos sem GTK, SDL ou WSLg;
não equivale a digitação física ou revisão visual do framebuffer.

## Build e inspeção

~~~sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
~~~

O compilador nativo só compila testes host. Kernel é C17 freestanding com
cross compiler x86_64-elf, sem libc/CRT/libgcc. Warnings continuam erros,
incluindo `-Wall -Wextra -Wpedantic -Werror`.
O NASM mantém a exceção específica `-Wno-error=reloc-rel-dword`; não remova
`-Werror` para contornar falhas.

`make inspect` executa readelf, nm e a inspeção Python do ELF realmente
ligado: ELF64 EXEC x86-64 higher half, segmentos, símbolos, requests,
ausência de runtime dinâmico e convenção de interrupções.
O linker exporta limites de text/rodata/data/BSS para o VMM.
`-mgeneral-regs-only` evita uso de contexto SIMD/FPU não administrado.

## Boot, descritores e PIT

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name boot-v02 \
    --check-gdt --check-idt --check-timer
~~~

O harness usa q35/TCG, CPU qemu64, 256 MiB, um core e sem rede.
Seleciona a única ISO UTAMO em `build/`; se houver mais de uma, informe
`--iso build/utamo-os-0.2.0.iso`. O timeout padrão desse harness é 90 s.
Cada `--name` deve ser novo. Lock e inspeção de /proc evitam VMs concorrentes;
somente o subprocesso criado pelo harness é encerrado.

Checks GDT/IDT leem estado real por HMP/QMP.
O teste de timer lê `ticks` por GDB em dois momentos de execução;
incremento comprova IRQs do PIT atravessando PIC/IDT e retornando.
A COM1 continua saída por polling a 115200 8N1; escrever na serial do host
não fornece entrada para o shell PS/2.

## Memória e comandos via PS/2 headless

Depois de recompilar kernel/ISO:

~~~sh
python3 scripts/test-memory-qemu.py --suite --name memory-v02 --timeout 180
~~~

O harness de memória reaproveita o controle de VM do script anterior.
Envia comandos por QMP ao teclado emulado e observa a serial.
Espera PMM/VMM prontos e o prompt; verifica CR0.WP, CR3, CR4.LA57,
EFER.NXE, MAXPHYADDR, descritores e permissões por seção.
Consulta os endereços reais dos símbolos no ELF e os mappings pelo comando
`mapinfo`, incluindo o alias HHDM dos bitmaps e a arena inicialmente ausente.

Executa `pmmtest` repetidamente, verifica restituição exata de contadores,
depois executa `vmmtest` repetidamente.
A primeira execução pode reter tabelas intermediárias fixadas; o custo deve
corresponder ao incremento de `PMM-owned page tables`.
Repetições devem reutilizá-las, liberar frames de dados e manter accounting.
Ao final, confere texto RX, timer ainda avançando, echo, Shift, Backspace,
Enter, clear e halt. Clear é verificado pela sequência serial; não se infere
aparência gráfica sem uma captura analisada.

`--ram` e `--cpu` permitem cenários adicionais, registrados no relatório.
Por padrão a suíte espera NX habilitado; o caso sem NX usa uma expectativa
explícita, mantendo os demais checks e exigindo que bits NX não sejam aplicados:

~~~sh
python3 scripts/test-memory-qemu.py --suite --name memory-no-nx-v02 \
    --ram 64M --cpu qemu64,-nx --expect-nx off --timeout 180
~~~

Esse caso testa o fallback sem proteção de execução; não executa o probe NX. `--version` pode fixar o banner esperado; por padrão lê version.h,
portanto uma ISO desatualizada deve falhar em vez de produzir falso sucesso.

## Page faults de memória

Cada comando abaixo inicia e recolhe uma VM separada:

~~~sh
python3 scripts/test-memory-qemu.py --fault vmm --name memory-unmapped-v02
python3 scripts/test-memory-qemu.py --fault ro --name memory-readonly-v02
python3 scripts/test-memory-qemu.py --fault nx --name memory-nx-v02
python3 scripts/test-memory-qemu.py --fault pf --name memory-legacy-pf-v02
~~~

| Modo | Disparo | CR2 esperado | Error code |
| --- | --- | --- | --- |
| vmm | Shell `fault vmm`: map, unmap, free e escrita ausente | 0xffffc00000000000 | 2 |
| ro | GDB → `memory_fault_readonly`: escrita em mapping RO | 0xffffc00000000000 | 3 |
| nx | GDB → `memory_fault_nx`: execução em mapping NX | 0xffffc00000000000 | 17 |
| pf | Shell `fault pf`: probe herdado | 0x00007ffffffff000 | 2 |

Os números são expectativas que o harness compara com o hardware,
não resultados atribuídos automaticamente a qualquer execução.
Os modos RO/NX param por hardware em `cpu_wait_interrupt` antes de HLT
e redirecionam RIP ao probe. Não são comandos `fault ro` ou `fault nx`.

O teste exige vetor 14, registradores e selectors completos, CR2, bits
P/W/R/U/S/RSVD/I/D, parada real com IF=0 e snapshot VMM coerente.
O dump serial original deve terminar antes de `Virtual memory context`.
Página ausente não pode ganhar uma tradução física fictícia.
A consulta não aloca nem tenta recuperar a falha.

Uma proteção RO/NX eficaz no endereço testado não prova W^X global:
aliases HHDM herdados podem continuar graváveis e executáveis.
Veja [gerenciamento de memória](memory-management.md).

## Exceções preservadas

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name fault-ud2-v02 \
    --check-gdt --check-idt --probe exception_fault_ud2 \
    --probe-at cpu_wait_interrupt --debug
python3 scripts/test-qemu.py --marker "utamo> " --name fault-div0-v02 \
    --check-gdt --check-idt --probe exception_fault_div0 \
    --probe-at cpu_wait_interrupt --debug
~~~

Probes nunca rodam no boot normal. No desenvolvimento v0.1, alterar RIP
depois de HLT não retirou a CPU do estado halted observado no QEMU.
O harness usa `-S` e breakpoint antes de HLT por esse motivo.
Timeout não constitui aprovação; não injete outra exceção para mascarar o problema.

## GDB manual, ainda headless

Uma sessão de diagnóstico pode usar a mesma ISO e timeout:

~~~sh
timeout --signal=TERM --kill-after=2s 120s \
    qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
    -cdrom build/utamo-os-0.2.0.iso -boot d -display none \
    -serial file:build/debug-serial.log -monitor none -nic none \
    -no-reboot -no-shutdown -S -gdb tcp:127.0.0.1:1234
~~~

Em outro terminal do projeto:

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

Breakpoints úteis: `memory_init`, `pmm_core_init`, `vmm_space_map`,
`interrupt_dispatch`, `kernel_panic` e `cpu_wait_interrupt`.
Com `-O2`, variáveis podem estar otimizadas; consulte também o mapa de link,
nm e objdump. Não use GDB `load` para substituir a imagem de Limine.
O endpoint é loopback; o harness normal usa socket Unix no projeto.
`detach` desconecta GDB, mas não encerra a VM.

## Diagnóstico e evidências

| Sintoma | Inspeção |
| --- | --- |
| Falha de inicialização PMM/VMM | Responses HHDM/executable, tipos do mapa, MAXPHYADDR, slot 384 e validação das tabelas herdadas |
| Bitmap ou kernel sobrescrito | Reservas, classificações USABLE, ownership e ordem de validação antes do bitmap |
| map/protect retorna false | Arena, alinhamento de 4 KiB, frame alocado, flags, mapping existente/huge e restrições dos ancestrais |
| Contador usado cresce | Comparar frames de dados com page tables fixadas; repetições devem estabilizar |
| Page fault | CR2, RIP, error code e snapshot VMM posterior ao dump original |
| NX/RO não gera fault | CPUID/EFER.NXE, CR0.WP, flags efetivas e endereço exato/alias acessado |
| Triple fault/reset | Log `--debug`, IDT/IST/CS, stack, frame e mappings de código/dados |
| Ticks ou teclado param | IF, PIC, IRQ0/IRQ1, EOI e duração das seções críticas |
| Dump serial completo, gráfico incompleto | Mapping/pitch/RGB e revisão visual separada |
| GDB não retoma depois de HLT | Reiniciar com breakpoint antes de HLT |

Cada execução cria `build/validation/<name>/` com serial, JSON, registros
e logs pertinentes. O JSON identifica revisão/status Git, hashes ELF/ISO,
configuração e checks, falha/timeout e processo recolhido.
`make clean` também remove essas evidências; preserve os resultados dentro
do projeto antes de limpar. Uma recompilação não transfere evidência entre hashes.

O aceite manual QEMU/VNC do v0.1 permanece nas
[notas históricas](releases/v0.1.0.md).
A sessão v0.2 diferencia QMP/PS2 emulado, observação serial e inspeção de
registradores de teclado físico, legibilidade gráfica, UEFI e hardware real.
Não há recuperação de page fault, scheduler ou backtrace automático no kernel.
