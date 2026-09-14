# Validação de boot — pendente

**Este documento é um roteiro para execução no computador pessoal. Nenhuma linha
abaixo representa teste executado nesta geração.** Os fontes do milestone estão
implementados; aceite em runtime depende das evidências descritas aqui.

## Preparação e evidências

1. Copie `UtamoOS` para Linux/WSL2 pessoal, em caminho sem espaços.
2. Leia `docs/development-environment.md`, prepare a toolchain e Limine fixado.
3. Registre versões reais de GCC/binutils/NASM/Make/xorriso/QEMU/GDB/OVMF.
4. Execute os dois testes de host com `make test-host`; registre saída e exit code.
5. Execute build do kernel sem remover `-Werror`. Inspecione ELF e mapa antes da VM.

Para cada cenário, registre data, revisão dos fontes, comandos exatos, exit code
quando aplicável, firmware, memória e resultado observado. Guarde logs/capturas
em `build/validation/` durante a sessão; antes de `make clean`, preserve dentro
de `docs/validation/` apenas evidências selecionadas que queira versionar.
Essas pastas serão criadas pelo desenvolvedor em casa quando houver resultados.

## Inspeção do ELF

Comandos estão em `docs/debugging.md`. Critérios a conferir no artefato real:

- ELF64 little-endian, `ET_EXEC`, máquina AMD x86-64, entry `_start` no higher half.
- Requests/delimitadores em segmento RW carregável, entre markers e alinhados.
- Texto RX, dados RW, constantes R; nenhum segmento ELF RWX.
- `.bss`/pilha em região carregável de memória, sem bytes de arquivo para NOBITS.
- DWARF fora de PT_LOAD; ausência de INTERP/DYNAMIC e relocações de runtime.
- `nm -u` sem símbolos indefinidos; biblioteca do host ausente do mapa de link.
- Assembly de entrada alinha a pilha antes do `call`; ausência de SIMD/FPU e red zone.

O linker já tenta rejeitar construções inesperadas. Somente `readelf`/`objdump`
do binário real confirmarão o resultado da toolchain escolhida.

## Matriz de cenários

| ID | Cenário futuro | Resultado esperado | Estado |
| --- | --- | --- | --- |
| B01 | `make run`, BIOS/TCG, 256 MiB, um CPU | Banner, framebuffer, logs, mapa e halt | Pendente |
| B02 | `make run-uefi`, OVMF sem Secure Boot | Mesmo fluxo e saída legível | Pendente |
| B03 | QEMU com 128 MiB e depois 512 MiB | Totais coerentes com as regiões USABLE de cada boot | Pendente |
| B04 | COM1 não conectada (`-serial none`) | Terminal funciona; aviso de COM1 indisponível se UART falhar | Pendente |
| B05 | Resolução 800x600x32 no limine.conf | Texto legível, sem pressupor 1024x768 | Pendente |
| B06 | GDB invalida framebuffer response após carga | Panic serial, sem desenho através de ponteiro nulo | Pendente |
| B07 | GDB invalida memory map response | Panic após console inicializado, sem mensagem final normal | Pendente |
| B08 | GDB altera base revision para valor não aceito | Panic antes de consumir framebuffer/mapa | Pendente |
| B09 | Breakpoint/inspeção em `cpu_halt` | IF=0 e laço `hlt` no caminho final | Pendente |

B04: `-serial none` elimina o backend, mas o modelo da máquina pode continuar
expondo uma UART funcional. Uma inicialização bem-sucedida por loopback nesse
caso não é defeito: o kernel detecta a UART, não um leitor conectado à saída.
Documente o comportamento observado em vez de exigir um aviso que o modelo não
justifica. Um timeout controlado do dispositivo precisa de emulação/injeção própria.

Para B03, use o comando QEMU manual de `debugging.md` e altere apenas `-m`.
O total utilizável não precisa ser igual à memória configurada: exclui reservas.
Não se exige crescimento exato de MiB por MiB entre cenários, pois o firmware
pode alterar a distribuição das reservas.

Para B05, preserve o arquivo original e restaure-o após o teste. Limine pode
selecionar fallback se o modo não existir. Registre dimensões realmente recebidas.
24 bpp e máscaras alternativas têm testes em buffers de RAM no host; não se deve
alterar apenas `bpp` de um framebuffer real no debugger, pois seu layout físico
não mudaria e o teste deixaria de ser válido.

## Injeção controlada por GDB

Use `make debug`, carregue símbolos e pare com `hbreak kernel_main` como descrito
em `docs/debugging.md`. Cada cenário começa em VM nova. Antes de continuar,
inspecione a variável escolhida; depois aplique **uma** alteração:

```gdb
# B06, somente após parar em kernel_main:
set variable framebuffer_request.response = 0

# OU B07 em outra inicialização:
set variable memmap_request.response = 0

# OU B08 em outra inicialização:
set variable limine_base_revision[2] = 3
```

Os símbolos são locais a `boot.c`, mas constam do ELF de debug. Se houver
ambiguidade de nome, use a qualificação GDB por arquivo, por exemplo
`'boot.c'::memmap_request`. Não use `set` antes de o loader mapear/carregar o ELF;
não desreferencie ponteiros fabricados para simular falhas.

Adicione `break kernel_panic` e `break cpu_halt`, continue e observe argumentos,
arquivo/linha, serial e framebuffer disponível. Os caminhos de erro não devem
imprimir `Welcome`/`System halted safely.`. Retire a injeção reiniciando a VM.

## Observações de tela e parada

Compare letras maiúsculas/minúsculas, dígitos, pontuação, bordas e contraste.
O terminal tem cursor lógico sem cursor piscante. Ao exceder a última linha,
limpa toda a área e retorna ao topo; histórico persistente não é implementado.
Em 1024x768, o banner atual deve caber sem limpeza por transbordo.

Tela parada não é prova de halt correto. Para B09, confirme pelo debugger o
caminho de chamada final, RIP no laço de `cpu_halt` e IF=0 em RFLAGS. Registre
também ausência de panic e de reset inesperado nos logs. QEMU permanecer aberto
é comportamento previsto; feche-o manualmente após coletar evidências.

## Fora do aceite 0.0.1

Não há testes de filesystem, rede, keyboard, PMM, scheduler, syscalls ou GUI,
porque esses componentes ainda não existem. Não provoque uma exceção de CPU
esperando um handler amigável: IDT/TSS/IST são o próximo marco.
SMP, bare metal e Secure Boot não fazem parte da primeira matriz de validação.

## Registro de resultado

```text
ID do cenário:
Data / versão dos fontes:
Ferramentas e firmware:
Comandos realmente executados:
Exit code / resultado observado:
Evidências (caminhos locais):
Diferenças em relação ao esperado:
Correções necessárias / nova validação:
```
