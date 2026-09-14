# Development log

## 2026-09-14 — evolução v0.2: gerenciamento de memória

Desenvolvimento em v0.2-dev sobre o baseline v0.1.0. PMM/VMM, HHDM,
permissões, diagnóstico e testes evoluem o kernel existente; heap, scheduler,
userspace, APIC e SMP permanecem fora do escopo.
Os registros históricos abaixo são preservados.

### D012 — reservas e dois bitmaps dinâmicos

O maior fim de USABLE dimensiona elegibilidade/ocupação dos frames de 4 KiB.
First-fit escolhe storage alinhado em USABLE; página zero/metadados são
reservados. Next-fit oferece alocação contígua, free/reserve validam ranges
inteiros e pin converte tabelas em reservas permanentes.

HHDM/executable address usam requests revision 0, mantendo base revision 3.
Conversões puras aceitam somente os quatro tipos previstos pelo direct map.
O boot confere a árvore inteira herdada como BOOTLOADER_RECLAIMABLE antes
de escrever bitmaps. Bootloader e ACPI não são recuperados.

### D013 — CR3 preservado e publicação controlada

Slot PML4 384 começa vazio. Queries leem folhas de 4 KiB/2 MiB/1 GiB;
mutações públicas só usam 4 KiB na arena própria e frames PMM alocados.
Ramos são zerados fora da árvore; falha devolve frames temporários, sucesso
publica via release e fixa tabelas. Unmap não libera dados.
Tabelas vazias permanecem disponíveis para reúso.

### D014 — proteção e diagnóstico com limites explícitos

MAXPHYADDR via CPUID, quatro níveis com LA57 rejeitado, NX via CPUID/EFER
e WP habilitado. RX/RO-NX/RW-NX protegem o mapping principal quando
compatível. Aliases HHDM limitam essa política: não há W^X global.
Page faults mantêm o dump serial original completo antes do snapshot VMM
sem alocação; probes unmapped/RO/NX nunca rodam automaticamente no boot.

### D015 — selftests e matriz v0.2

Shell ganhou pmm/vmm/mapinfo/pmmtest/vmmtest/fault vmm.
Stress PMM usa 64 frames e intervalo contíguo; VMM usa 16 páginas através
de fronteira de 2 MiB, accounting e reutilização das tabelas.
Testes host separam lógica pura de hardware.

O usuário autorizou entrada QMP no PS/2 emulado nesta sessão headless.
Após o build limpo da versão 0.2.0, a matriz final observou:

| Camada | Checks | Falhas |
| --- | ---: | ---: |
| Host | 11224 | 0 |
| ELF/ABI | 1439 | 0 |
| QEMU headless | 1550 | 0 |
| Total | 14213 | 0 |

As 13 VMs foram sequenciais, todas aprovadas e recolhidas: boot (9),
suítes de memória em 64/256/512 MiB (301 cada), CPU sem NX em 64 MiB (301),
fault vmm/ro/nx/pf (43/48/48/43), UD2/div0 (37/34),
suíte original do shell (38) e PF do harness original (46).
Nenhuma VM permaneceu rodando.

O harness original ainda esperava o banner 0.1.0 fixo; foi corrigido para
--version opcional com default no header central, preservando seus checks.
A suíte original e o PF original foram executados novamente na imagem 0.2.0.

No boot final de 256 MiB, antes de selftests: 65027 frames gerenciados,
65023 livres, bitmaps em 0x53000 ocupando 16384 bytes, raiz CR3 em 0xff4c000,
NX habilitado e 10 visitas às tabelas herdadas. São valores daquela VM,
não constantes exigidas pelo kernel. Os quatro frames usados nessa contagem
inicial correspondem ao storage dos bitmaps.

[Relatório final v0.2](v0.2-implementation-report.md) e
[índice de evidências](validation-v0.2.json) identificam comandos, hashes e
artefatos. Não se atribui teclado físico ou validação gráfica à saída serial;
UEFI e hardware real continuam sem validação específica desta imagem.
Próximo milestone: v0.3, kernel heap.

## 2026-09-14 — aceite manual e preparação da release v0.1.0

O usuário confirmou que os testes manuais passaram no QEMU/VNC: teclado PS/2,
digitação de caracteres, Enter, Backspace, comandos do shell, clear e halt.
O relato é atribuído ao usuário e não contado como execução automatizada.

O release check anterior passou 5214 checks host, 1303 checks ELF/ABI, build
limpo e ISO. O ELF manteve o hash da validação QEMU anterior (126 checks).
As evidências anteriores à limpeza foram preservadas dentro do projeto.

O usuário autorizou criar main a partir de master, fazer merge --no-ff de
v0.1-dev, criar v0.1.0 anotado, configurar origin e publicar main/tags no
repositório zaureliu/UtamoOS. O baseline v0.0.1 permanece intacto; sem force push,
sem publicação de build, toolchain ou vendor. O registro anterior abaixo
descreve o estado histórico, anterior a esta confirmação.

[Notas da release](releases/v0.1.0.md).


## 2026-09-14 — evolução v0.1.0 sobre baseline validado

Registro histórico anterior ao aceite manual acima.

Implementação e validação automatizada concluídas no escopo headless;
aceite integral pendente de input PS/2 e framebuffer visual manual.
[Relatório completo](v0.1-implementation-report.md).

O usuário confirmou boot real v0.0.1. Inspeção encontrou master limpo e
tag v0.0.1 em 273e476. O checkout Git utilizado estava em `~/UtamoOS`, no filesystem Linux do WSL;
a cópia inicial fora dele não continha o histórico Git. Criada v0.1-dev; master/tag preservados, sem push.
Antes de alterar: 3044 checks host, zero falhas.

### D008 — tabelas e diagnóstico

GDT/TSS com stacks IST para DF/NMI/MC, IDT de 256 gates e frame de 176 bytes.
Manuais Intel/AMD confirmam SS:RSP incondicional no modo 64-bit.
Dump serial completo precede framebuffer. Probes UD2, DIV e PF são explícitos.
NASM 3.01 exigiu trocar tabela absoluta por offsets relativos; nenhuma flag
foi relaxada. A exceção reloc-rel-dword existente foi apenas deduplicada.

### D009 — IRQ e input

PIC em 0x20/0x28, fontes sem driver mascaradas, IRQ7/15 espúrias tratadas.
PIT modo 2, divisor 11932, alvo 100 Hz; contador uint64 saturante.
PS/2 set 1 explícito com ACK/RESEND, fila de 127 bytes úteis, decoder no
fluxo principal. IRQs não logam; compartilhamento usa seções curtas IF=0.
Não há contrato SMP.

### D010 — shell e espera

Nove comandos reais, parser limitado e edição numa linha preservam o
terminal do baseline. CLI antes de consultar a fila e STI/HLT contíguos
evitam perda de wakeup. Versão 0.1.0 centralizada no header; ISO deriva dela.
Reboot não foi implementado.

### D011 — evidências e restrição gráfica

| Etapa | Checks host | Evidência |
| --- | ---: | --- |
| Baseline | 3044 | Antes de modificar |
| A GDT | 3079 | Build/ISO/boot serial |
| B IDT | 3185 | GDT e IDTR observados |
| C diagnóstico | 3185 | UD2 por GDB; 37 checks QEMU |
| D PIC | 3266 | Build/ISO/boot com fontes mascaradas |
| E PIT | 3286 | Ticks reais crescentes |
| F input | 5059 | Init PS/2 e PIT; sem teclas injetadas |
| G shell | 5214 | Prompt serial e PIT |

Todos os QEMU da sessão usaram display none, uma VM por vez e encerramento
controlado. O usuário reiterou a proibição de GUI devido a WSLg/RemoteApp.
Teclado QMP e capturas visuais foram apenas preparados. Não foram executados
make run, make run-uefi, GTK ou SDL.

O primeiro probe GDB numa CPU já halted deu timeout. Corrigido por breakpoint
antes do HLT, com VM inicialmente pausada. Falha preservada em
validation-artifacts/incremental/validation/milestone-c/; C2 passou.

Após make clean: 5214 checks host, 1303 ELF, 126 QEMU: total 6643, zero falhas.
Build e ISO aprovados. Boot final mostrou ticks 2→63 em 0,6 segundo.
PF: vetor 14, erro 2, CR2=0x7ffffffff000. Processos QEMU recolhidos.
[Índice de evidências](validation-v0.1.json).
Nenhum pacote, toolchain, dependência externa ou configuração global alterado.


## 2026-09-13 — geração inicial 0.0.1

Registro histórico da geração dos fontes, anterior ao boot validado do
baseline e à implementação v0.1.0.

Estado global: fontes implementados, com duas revisões estáticas. Nenhum
compilador, teste, script de projeto, QEMU ou sistema operacional foi executado.
Nenhuma ferramenta foi instalada; nenhuma alteração de configuração do Windows,
PATH ou registro foi feita; não houve inicialização Git, push ou publicação.
Todos os arquivos criados pertencem a `UtamoOS/`.

### D001 — Limine selecionado e adaptador isolado

**Decision:** Limine v8.7.0, base revision 3, API revision 2; subset identificado
do header oficial, limitado a framebuffer, memory map e paging. Bootloader futuro
fixado em v8.7.0-binary, com SHA registrado na documentação de ambiente.

**Reason:** Contrato conhecido e conferido, com poucas dependências. A obtenção
direta do header via HTTPS na máquina falhou; suas declarações oficiais foram
consultadas por leitura web e transcritas seletivamente com avisos preservados.
O arquivo não é declarado como cópia byte a byte do upstream.

**Alternatives considered:** Bootloader próprio, UEFI application direta, header
completo e terminal legado. Um bootloader próprio desviaria o primeiro marco;
o subset torna explícito quais features foram integradas.

**Future impact:** Adicionar requests exige cotejar release/header/protocolo,
registrar revisão e ampliar testes. Somente o adaptador depende de `limine.h`.

### D002 — Estado inicial mínimo

**Decision:** ELF estático, high half fixo, stack própria de 64 KiB, um BSP,
quatro níveis de paginação e IF=0 durante todo o marco.

**Reason:** Simplificar o primeiro diagnóstico sem gerenciar simultaneamente
APs, relocação, heap e interrupções. CPU e I/O privilegiado ficam em NASM.

**Alternatives considered:** Entrada C direta usando pilha Limine, KASLR e SMP
desde o começo. A pequena entrada NASM define explicitamente alinhamento e vida
útil da pilha sem impor um subsistema de memória antecipado.

**Future impact:** Instalar GDT/IDT/TSS antes de IRQs; page tables e GDT do
bootloader ainda não podem ser liberadas. SMP exigirá arquitetura própria.

### D003 — Vídeo próprio e fonte incorporada

**Decision:** Framebuffer RGB 24/32 bpp, máscaras validadas, pixels por byte,
terminal ASCII 8x16 com fonte original 5x7 incorporada sob MIT.

**Reason:** Evitar dependência de terminal externo e pressupostos de pitch/BGRX.
Escritas por byte simplificam alinhamento e 24 bpp.

**Alternatives considered:** Terminal pronto, PSF carregado como módulo, VGA text
mode e fonte de terceiros. São desnecessários para o banner e exigiriam outro
contrato de boot, parser ou licença.

**Future impact:** Fonte, layout e pixels têm interfaces separadas. A tela é
limpa no transbordo; um futuro buffer de células permitirá scroll sem ler MMIO.
Geometria limitada a 8192 por eixo e extensão a 256 MiB no bootstrap.

### D004 — Logs transmitidos por callback

**Decision:** Até quatro sinks, formatter por caractere, serial e framebuffer
ativos simultaneamente quando disponíveis; sem heap nem buffer de log fixo.

**Reason:** Evitar truncamento invisível e acoplar o mínimo possível a saída.
COM1 tem polling limitado e se desabilita após timeout.

**Alternatives considered:** Apenas framebuffer, printf do host, ring buffer
concorrente. O último fará sentido quando houver IRQs ou SMP.

**Future impact:** Arquivos/debug console podem ser novos sinks; thread safety,
quais mensagens podem bloquear e modo de emergência terão que ser definidos.

### D005 — Mapa próprio, sem allocator prematuro

**Decision:** Até 512 regiões copiadas e validadas; totais de usable/reclaimable
separados. Unknown types viram reserved; nenhum frame é reutilizado agora.

**Reason:** Eliminar aliasing com metadados do loader e preparar contratos
testáveis sem liberar memória ainda em uso.

**Alternatives considered:** Guardar ponteiros Limine ou iniciar bitmap PMM já
na primeira tela. Ambos esconderiam decisões de vida útil ainda não resolvidas.

**Future impact:** PMM precisará reservar seus próprios dados e mappings; o limite
fixo pode ser substituído depois que houver allocator inicial seguro.

### D006 — Build explícito e auditável

**Decision:** GNU Make, cross compiler obrigatório, flags estritas, `-nostdlib`,
sem libgcc/CRT, ISO em caminho fixo e dependências provisionadas explicitamente.

**Reason:** Evitar dependência acidental do host e execução automática da VM.
GOT/PLT/relocações inesperadas são rejeitadas; DWARF é mantido fora de PT_LOAD.

**Alternatives considered:** Download automático em Make, compilador nativo como
fallback, tolerar warnings ou gerar ISO no target padrão. Essas escolhas
reduziriam a previsibilidade da primeira validação.

**Future impact:** Mudanças na toolchain devem registrar versões e começar com
build limpo. ISO bit a bit reproduzível ainda não é garantida.

### D007 — Limites da evidência

**Decision:** Separar revisão estática, testes preparados e execução observada.

**Reason:** Respeitar o computador corporativo e manter o relato tecnicamente
honesto. Ausência de erro detectado na leitura não é prova de boot.

**Alternatives considered:** Nenhuma execução local é admissível nesta geração.

**Future impact:** Primeira tarefa em casa é registrar toolchain e executar a
matriz de validação, corrigindo falhas antes de expandir funcionalidades.

## Modelo para entradas futuras

```text
Date / version / source revision:
Decision:
Reason:
Alternatives considered:
Future impact:
Validation commands actually executed:
Observed result / exit code / artifact paths:
Pending validation:
```
