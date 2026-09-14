# Teclado e shell do UTAMO OS v0.2

## Controlador e IRQ1

O driver i8042 em `kernel/drivers/input/keyboard.c` usa as portas `0x60`
(dados) e `0x64` (status/comando). A inicialização ocorre com IF=0 e IRQ1
ainda mascarada no PIC. Desabilita as duas portas, drena bytes anteriores,
lê a configuração, desliga a tradução do controlador e testa a primeira
interface. A segunda porta permanece desabilitada; não há driver de mouse.

O teclado recebe `F5` (parar scanning), `F0 01` (selecionar set 1),
`F0 00` (consultar e verificar o set) e `F4` (habilitar scanning).
Cada byte exige ACK `FA`; RESEND `FE` permite no máximo três tentativas.
Esperas de leitura/escrita têm limite de 100.000 consultas de status; a drenagem
é limitada a 256 bytes. São limites de trabalho, não timeouts calibrados em
milissegundos. Falha retorna `false`; o boot não publica sucesso ou libera
IRQ1 nesse caso. O driver deixa o desmascaramento do PIC a cargo do chamador.

A ISR lê status e, quando disponível, um byte. Dados AUX são descartados;
erro de paridade/timeout do controlador descarta a sequência pendente.
A ISR não imprime, não interpreta comandos, não altera framebuffer e não espera
por respostas de protocolo. O dispatcher envia EOI depois do driver.

## Buffer e sincronização

`kernel/input/input.c` contém lógica portável independente de portas:
fila circular de 128 posições, com 127 bytes úteis, e decoder de scancode set 1.
Um slot distingue cheia de vazia. Overflow descarta a sequência pendente e o byte
que excedeu a capacidade, marcando a necessidade de reset do decoder.
Isso evita preservar Shift pressionado depois de perder seu release.

No kernel single-core, toda operação na fila ocorre com IF=0: o produtor já
está numa interrupt gate; o consumidor salva RFLAGS, executa CLI, retira um byte
e restaura apenas o estado de IF. A interpretação do byte acontece depois da
restauração. A fila não é lock-free nem uma solução para SMP. `volatile`
sozinho não forneceria esta exclusão; funções externas NASM também delimitam os
acessos de memória para o compilador neste build sem LTO.

O decoder reconhece letras, números, espaço, Enter, Backspace, Tab convertido em
espaço pelo shell, ambos Shift, Caps Lock e pontuação ASCII do layout US.
Caps Lock não alterna repetidamente durante typematic. Prefixos E0 filtram
teclas estendidas não suportadas e falso Shift do Print Screen; Enter e barra
estendidos são aceitos. Pause E1 consome seus cinco bytes seguintes. Não há
layout ABNT2, Unicode, LEDs, hotplug, histórico, set 2/3 em runtime ou atalhos
Ctrl/Alt.

## Shell

`kernel/core/shell.c` retém referências ao mapa e terminal pertencentes ao boot,
reutiliza `kprintf` e roda somente no loop principal. O parser e editor de linha
em `kernel/lib/shell_line.c` não têm acesso a hardware.

A linha comporta no máximo 127 caracteres, limitada também a
`terminal.columns - 7` para manter `utamo> ` e edição em uma única linha
física. Em 1024x768 com células 8x16, o limite efetivo é 121 caracteres.
Excesso é ignorado sem modificar a linha; Enter ainda submete e Backspace ainda
remove. Não há wrapping da linha de entrada, aspas ou escapes no parser.
Espaços/tabs nas bordas são removidos; `echo` preserva espaços internos.
Backspace não apaga o prompt. No framebuffer usa o contrato preexistente
`terminal_putc('\b')`; na serial emite `BS SPACE BS`.

| Comando | Efeito real |
| --- | --- |
| `help` | Lista os comandos implementados |
| `clear` | Limpa o framebuffer e envia ANSI clear/home apenas à serial |
| `version` | Exibe a fonte de versão em `utamo/version.h` |
| `sysinfo` | Versão, arquitetura, bootloader, mapa, PIT/ticks e framebuffer |
| `mem` | Mapa de boot e estatísticas reais PMM/VMM |
| `pmm` | Frames totais/livres/usados e metadados do allocator |
| `vmm` | CR3, HHDM, NX, MAXPHYADDR e tabelas próprias |
| `mapinfo hex` | Presença, tradução, flags efetivas e tamanho da folha |
| `pmmtest` | Stress limitado de frames físicos e contabilidade |
| `vmmtest` | Stress limitado de mappings, proteção, unmap/remap e contabilidade |
| `uptime` | Horas/minutos/segundos desde o PIT, sem relógio civil |
| `echo texto` | Exibe argumentos como dados, inclusive caracteres `%` |
| `halt` | CLI, mensagem e parada permanente da CPU |
| `fault ud2/div0/pf/vmm` | Dispara exceção fatal; vmm acessa página removida pelo VMM |

Comandos sem argumentos rejeitam texto extra. `fault` exige exatamente um tipo
reconhecido. Não há comando fictício `reboot`. `fault` nunca é executado no
boot normal. Os contratos dos comandos de memória e dos probes internos RO/NX
estão em [memory-management.md](memory-management.md). Não há comando de heap.

## Evidência de teste e fronteiras

Os testes host exercitam FIFO, wrap, overflow/reset, decoder, edição limitada,
parser/tokenizer e dispatch de todos os comandos. O teste PS/2 substitui I/O por
um modelo determinístico de ACK/RESEND, consulta de set, timeouts e bytes de IRQ.
O teste de comandos substitui teclado, timer, framebuffer, serial e instruções
privilegiadas por efeitos observáveis no processo host.

Testes host não provam entrega real de IRQ1 ou aparência da tela.
Na evolução v0.2, o usuário autorizou teclado QMP no PS/2 emulado para
a suíte headless de memória. Quando executada, ela percorre IRQ1, fila,
decoder e shell, observando o resultado serial. Isso não equivale a digitação
física ou validação visual do framebuffer.

O aceite manual em QEMU/VNC informado em 2026-09-14 pertence à release
v0.1.0: teclado PS/2, digitação, Enter, Backspace, comandos, clear e halt.
É evidência histórica atribuída ao usuário, separada dos testes automatizados
e da matriz v0.2. Combinações de teclas não citadas não recebem cobertura presumida.
