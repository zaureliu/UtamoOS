# Development log

## 2026-09-13 — geração inicial 0.0.1

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
