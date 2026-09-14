# Debugging

Esta entrega prepara os mecanismos de observação. Não houve compilação,
execução do QEMU, execução dos testes ou sessão GDB durante sua geração.
Todos os comandos abaixo destinam-se ao computador pessoal após preparar
[o ambiente de desenvolvimento](development-environment.md).

## Evidência antes do boot

Construa o ELF e inspecione o resultado antes de abrir a VM:

```sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
./toolchain/prefix/bin/x86_64-elf-objdump -d build/utamo-kernel.elf
```

Em `readelf`, confira ELF64 little-endian, arquitetura AMD x86-64, tipo `EXEC`,
entry point correspondente a `_start`, segmentos `LOAD` separados por
permissões e ausência de `INTERP`/`DYNAMIC`. A seção de requests do Limine deve
estar incluída em um segmento carregável gravável. Nenhum segmento precisa
ser simultaneamente gravável e executável. As seções DWARF não devem ocupar
memória carregada do kernel. `nm -u` deve produzir uma lista vazia.

Examine `build/utamo-kernel.map` para encontrar endereço e tamanho de cada
seção. O build falha diante de símbolos indefinidos e seções órfãs: uma falha é
informação útil, não um teste aprovado. Procure instruções SSE/AVX/x87 não
planejadas no disassembly, sobretudo em código de biblioteca. O código C usa
`-mgeneral-regs-only`; o contexto FPU ainda não é administrado pelo kernel.

## Serial e framebuffer

`make run` inicia QEMU em TCG, com 256 MiB, uma CPU, monitor desativado e COM1
ligada ao terminal (`-serial stdio`), sem interface de rede virtual (`-nic none`).
A serial do kernel usa polling, sem IRQs,
em 115200 baud, 8 bits, sem paridade, um stop bit. A janela gráfica representa
o framebuffer. As duas saídas permitem distinguir falha de console gráfico de
falha geral de execução. `-no-reboot -no-shutdown` conserva a VM aberta para
inspeção, inclusive após certas falhas; uma janela parada sozinha não prova que
o halt esperado foi atingido.

O kernel encerra com interrupções mascaráveis desabilitadas em um laço `hlt`.
Isso não desliga o computador virtual nem retorna ao Limine. Feche a janela do
QEMU, ou use Ctrl+C no terminal que o iniciou. Não há teclado, shell interativo
ou comando de desligamento neste marco.

Para capturar a serial em arquivo local, após gerar a ISO, execute em casa:

```sh
qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
    -cdrom build/utamo-os-0.0.1.iso -boot d \
    -serial file:build/serial.log -monitor none -nic none -no-reboot -no-shutdown
```

O arquivo só será evidência após uma execução real. Registre junto dele data,
versões de ferramentas, firmware BIOS ou OVMF, argumentos do QEMU, revisão dos
fontes e resultado observado. Capturar apenas a mensagem esperada escrita
manualmente na documentação não é um teste.

## GDB: parar antes de kernel_main

No primeiro terminal, a partir da raiz de `UtamoOS`:

```sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" debug
```

Esse alvo usa `-S` para parar a CPU antes de executar firmware e publica o stub
GDB somente em `127.0.0.1:1234`. Ele permanece aguardando comandos. O endereço
do kernel é fixo neste marco e `kaslr: no` está no `limine.conf`.

No segundo terminal, também na raiz:

```sh
gdb build/utamo-kernel.elf
```

Na sessão GDB:

```gdb
set pagination off
set architecture i386:x86-64
target remote 127.0.0.1:1234
hbreak kernel_main
continue
info registers
x/12i $rip
bt
```

Use `hbreak`, porque ao parar no reset o firmware ainda não estabeleceu o
mapeamento virtual do kernel. Um breakpoint de software nesse endereço pode
falhar antes de o Limine carregá-lo. Ao alcançar `kernel_main`, use `step`,
`next`, `info locals` e breakpoints normais. Pode haver variáveis otimizadas pelo
`-O2`; os frame pointers e DWARF ajudam, mas não eliminam os efeitos da
otimização. A diferença entre `-S` e a opção de conexão está descrita na
[documentação GDB do QEMU](https://www.qemu.org/docs/master/system/gdb.html).

Breakpoints úteis para o fluxo de falha ou término:

```gdb
break kernel_panic
break cpu_halt
continue
```

Inspecione argumentos e pilha antes de continuar. `PANIC` inclui arquivo e
linha; um panic explícito é diferente de uma exceção de CPU sem handler.
Não use `load` para substituir o kernel carregado pelo bootloader: reconstrua
a ISO e reinicie a VM quando alterar o binário. Se o kernel parar em `hlt`,
interrompa pelo GDB para inspecionar o RIP e RFLAGS. O bit IF deve estar zero
no halt final. `detach` desconecta o debugger; `quit` sai do GDB. Feche o QEMU
separadamente.

## Diagnóstico de falhas

| Sintoma observado | Próxima inspeção |
| --- | --- |
| Limine não encontra o kernel | Conteúdo da ISO, `limine.conf` e caminho `boot():/boot/utamo-kernel.elf` |
| Base revision não aceita | Header v8.7.0, tag do bootloader e seção carregável de requests |
| Serial aparece e tela permanece vazia | Resposta de framebuffer, bpp/máscaras RGB, pitch e limites |
| Tela e serial vazias | Breakpoint de hardware em `_start`; entry point, stack e ausência de COM1 |
| Reinício/freeze antes do halt | Disassembly e logs de exceções do QEMU; possível triple fault |
| `%` ou argumentos exibidos incorretamente | Contrato do formatador, tipo dos varargs e testes de host |
| Símbolo `memcpy`/`memset`/helper indefinido | Lista de objetos, flags freestanding e operação que gerou a chamada |
| Erro de seção órfã no link | Nome da seção, objeto de origem e classificação explícita no linker script |

Para registrar exceções/reset em uma execução pessoal:

```sh
qemu-system-x86_64 -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
    -cdrom build/utamo-os-0.0.1.iso -boot d -serial stdio -monitor none -nic none \
    -no-reboot -no-shutdown -d int,cpu_reset,guest_errors -D build/qemu.log
```

Ainda não existem IDT própria, handlers de page fault, proteção da stack,
backtrace automático em panic ou debugger interno. `cli` não bloqueia NMI,
exceções ou machine checks. Um acesso inválido pode impedir a própria mensagem
de panic e terminar em triple fault. A próxima etapa arquitetural é estabelecer
GDT/IDT e handlers de exceção antes de ativar interrupções.

## Primeira sessão de validação

1. Execute os testes de host e registre saída e exit code reais.
2. Compile sem ignorar warnings; examine o ELF e o mapa de link.
3. Gere a ISO com o checkout fixado do Limine.
4. Execute BIOS em TCG e confirme banner, framebuffer, memory map e halt por
   observação e pelo debugger.
5. Repita com `make run-uefi` e um par OVMF CODE/VARS compatível.
6. Compare a memória utilizável com o mapa do bootloader; ela será menor que os
   256 MiB configurados porque firmware, kernel e regiões reservadas consomem RAM.
7. Registre problemas e evidências em `docs/development-log.md`, sem converter
   resultados esperados em resultados observados.

O aceite do milestone em execução continua pendente até essa sessão ocorrer.
