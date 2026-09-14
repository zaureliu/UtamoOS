# Heap do kernel

Este documento descreve o heap da v0.3.0, marco GREEN local da campanha Astra.
O código integra o PMM e o VMM existentes. O gate de build, testes e QEMU foi
aprovado; isso não cria uma release pública nem altera os tags preservados.

## Organização e inicialização

| Arquivo | Responsabilidade |
| --- | --- |
| `kernel/memory/heap_core.c` | Blocos, lista livre, alocação, validação e estatísticas; sem hardware. |
| `kernel/memory/heap_pages.c` | Crescimento transacional por callbacks testáveis no host. |
| `kernel/memory/heap.c` | API pública, exclusão por IF e integração PMM/HHDM/VMM. |
| `kernel/memory/heap_selftest.c` | Autoteste explícito, determinístico e limitado. |
| `kernel/include/utamo/heap*.h` | Contratos das três camadas e estatísticas. |

`heap_init()` é chamado depois de PMM/VMM e antes de PIC, PIT e habilitação
normal das interrupções. O wrapper verifica que toda a faixa reservada está
ausente no VMM, mapeia o prefixo inicial e inicializa o core nesse prefixo.
A mensagem de sucesso é `Kernel heap initialized`.

| Propriedade | Valor |
| --- | --- |
| Base virtual | `0xffffc00001000000` |
| Limite exclusivo | `0xffffc00005000000` |
| Prefixo inicial | 65.536 bytes, 64 KiB |
| Unidade de crescimento | 65.536 bytes, 64 KiB |
| Capacidade máxima | 67.108.864 bytes, 64 MiB |
| Página de backing | 4.096 bytes |
| Alinhamento de payload | 16 bytes |

A faixa está no slot PML4 384 do VMM, separada dos primeiros 4 MiB usados pelos
testes de memória. A contiguidade é virtual: os frames do PMM não precisam ser
fisicamente consecutivos. Reservar a faixa não aloca antecipadamente 64 MiB.

A inicialização do wrapper é uma operação única de boot. Falha resulta em
panic no chamador; não há contrato de reinicialização ou tentativa posterior.
O core isolado recebe armazenamento zerado e uma arena inicial já acessível:
sua inicialização não chama o callback de crescimento e, se rejeitada, preserva
estado e arena. Uma inicialização bem-sucedida não pode ser repetida.

## Blocos e busca

Cada bloco começa por um header de 48 bytes. Seus campos são tamanho/flags,
tamanho do bloco anterior, bytes solicitados, offsets anterior/próximo da lista
livre e cookie. O payload começa imediatamente depois desse header.
O tamanho total mínimo é 64 bytes; o total é arredondado para múltiplo de 16.
O campo de tamanho anterior permite encontrar o vizinho sem buscar desde a base.

A lista duplamente encadeada de blocos livres é ordenada por endereço crescente.
Os links são offsets na arena. A busca usa first-fit: escolhe o primeiro bloco
adequado nessa ordem. A inserção custa O(F), para F blocos livres; a remoção,
com os links já validados, custa O(1). Divisão só ocorre se a sobra comportar
outro bloco de pelo menos 64 bytes. Caso contrário, a sobra fica no bloco usado.

A liberação combina vizinhos livres anteriores e posteriores. O encolhimento
por realloc também pode dividir o bloco e combinar a sobra com o vizinho livre.
A estrutura mantém a propriedade de não haver dois blocos livres adjacentes.
Não existem classes de tamanho, slabs, árvore de busca ou garantia de tempo real.

A validação percorre a cadeia de blocos contíguos da arena e compara a lista
livre ordenada com essa cadeia em O(N), para N blocos. Verifica limites antes de
ler headers, alinhamento, flags, cookie, tamanhos anteriores, links, ausência
de vizinhos livres e contabilidade das alocações. Não segue um endereço de
header fornecido por quem chama free. Alocação, liberação, realloc e leitura
de estatísticas validam o estado antes de operar; esse custo é deliberado.

## API e memória de payload

| API | Contrato |
| --- | --- |
| `kmalloc(n)` | Payload alinhado a 16 bytes; zero ou falha retornam NULL. |
| `kcalloc(count, n)` | Detecta overflow do produto e zera os bytes solicitados. |
| `krealloc(p, n)` | Preserva os primeiros min(tamanho antigo, n) bytes em sucesso. |
| `kfree(p)` | Retorna bool; NULL é sucesso sem efeito. |
| `heap_get_stats(out)` | Obtém contabilidade validada; retorna false se indisponível/inválida. |
| `heap_validate()` | Valida o core e compara seus bytes mapeados com o backing. |

Um fator zero em calloc retorna NULL sem contabilizar falha. Realloc com NULL
delega a alocação; com tamanho zero libera e retorna NULL. Realloc pode manter
o endereço, consumir o vizinho livre, crescer a cauda ou alocar/copiar/liberar.
Se a alocação necessária falhar, o bloco original e seu conteúdo permanecem.

Free rejeita ponteiros externos, interiores e blocos já livres, retornando
false. Um ponteiro antigo cujo endereço já tenha sido reutilizado não pode ser
distinguido de um ponteiro válido. O cookie detecta corrupção simples; não
fornece isolamento de segurança nem validação de toda escrita do consumidor.

O wrapper ativa `debug_poison=true`: payload novo recebe `0xCD`, e regiões
livres ou descartadas recebem `0xDD`. Calloc zera seus bytes solicitados depois
da alocação. Esses padrões auxiliam inspeção; não bloqueiam use-after-free
ou acessos além do tamanho solicitado e não são guard pages.

## Crescimento, permissões e rollback

Quando não há bloco suficiente, o core considera o espaço livre na cauda e
arredonda o crescimento necessário por unidades de 64 KiB, limitado ao máximo.
Só altera metadados após o callback confirmar o novo prefixo acessível.

O backing consulta previamente toda a extensão para rejeitar mapeamentos
preexistentes. Para cada página, aloca um frame exclusivo, zera os 4 KiB via
HHDM e instala um mapeamento supervisor, PRESENT e WRITABLE, com cache WB.
NX é aplicado quando o VMM informa suporte habilitado; sem NX, o heap não
oferece proteção contra execução. O alias HHDM herdado mantém as limitações
de permissões descritas em [memory-management.md](memory-management.md).

Em falha, o frame ainda não mapeado é liberado, e as páginas novas já mapeadas
são retiradas em ordem inversa antes de liberar seus frames. O prefixo anterior
é preservado. O tamanho mapeado só é publicado após concluir toda a extensão.
Não é necessário alocar um journal no próprio heap.

Falhas impossíveis de consulta/unmap/free durante a limpeza marcam o backing
como corrompido; o wrapper entra em panic. Um frame possivelmente ainda mapeado
não é liberado. Os callbacks não podem reentrar no heap e uma falha simples de
map/unmap/free deve preservar o mapeamento ou propriedade que lhe corresponde.

Tabelas intermediárias vazias criadas pelo VMM podem permanecer fixadas mesmo
após rollback. O rollback devolve páginas de dados novas, não promete restaurar
o total de frames livres se houve criação de tabelas. Não há devolução de
páginas ao PMM em free/realloc, decommit, destruição ou redução da arena:
a capacidade mapeada cresce e fica disponível para reutilização.

## Estatísticas e concorrência

`used_bytes` soma bytes solicitados pelas alocações vivas; `free_bytes` soma
a capacidade de payload dos blocos livres. `overhead_bytes` inclui headers,
alinhamento e sobra interna dos blocos usados. Portanto:

```text
mapped_bytes = used_bytes + free_bytes + overhead_bytes
delta(PMM used frames) = delta(heap mapped bytes) / 4096 + delta(VMM table pages)
```

A segunda identidade aplica-se ao teste controlado sem outros consumidores
alterando PMM/VMM. `largest_free_bytes` mede o maior payload livre.
`peak_usage` mede o pico de bytes solicitados, não a capacidade mapeada.
Contadores de operações são saturantes em UINT64_MAX. Alocações/liberações
bem-sucedidas contam; realloc no lugar não incrementa esses dois contadores,
enquanto realloc que move conta uma alocação e uma liberação.
Falhas de alocação e frees inválidos têm contadores próprios; realloc inválido
com tamanho não zero conta falha de alocação, não free inválido.

Os wrappers salvam IF, desabilitam interrupções durante toda a operação e
restauram o valor anterior. PMM/VMM aninhados preservam IF=0. O core e o backing
não implementam sincronização: o chamador serializa também stats/validate.
Uso permitido: boot e contexto principal do BSP, em single-core. Alocação em
IRQ/NMI é proibida por contrato, sem detecção automática do contexto.
Não há suporte a SMP; IF local não substitui sincronização entre CPUs.
Busca, validação, preenchimento e crescimento dentro dessa região crítica
podem aumentar a latência de interrupções.

## Autotestes e gate

`heap` mostra as estatísticas reais e `Heap integrity: OK|FAILED`.
`heaptest` executa o teste explícito, sem argumentos; não roda no boot normal.
Usa xorshift32 com seed `0x41535452`, 128 slots e 8.192 operações aleatórias.
Primeiro aloca 4.096–6.143 bytes por slot, forçando crescimento além de 512 KiB.
Cria buracos alternados, testa dupla liberação e ponteiro interior, overflow,
calloc, realloc e preservação de conteúdo. Verifica alinhamento, ausência de
sobreposição e padrões em cada payload; depois libera os slots e compara
bytes solicitados, alocações vivas e consumo de frames/tabelas.
A saída inclui seed, número realizado de operações e `Heap self-test: PASS|FAIL`.

`tests/test_heap.c` exercita o core com arena e crescimento modelados no host:
fragmentação, coalescência, esgotamento, corrupção, overflow, realloc, padrões,
contadores saturantes e stress determinístico. `tests/test_heap_pages.c`
modela páginas e injeta falhas nas etapas de crescimento e rollback, incluindo
mapeamento preexistente e falha de limpeza, sem instruções privilegiadas.

`scripts/test-heap-qemu.py` reutiliza o harness de memória para testar boot,
três repetições de heaptest, estabilização da capacidade, contabilidade
PMM/VMM, permissões, regressão pmmtest/vmmtest, PIT, entrada PS/2, clear e halt.
Executa sem janela, com timeout e coleta/encerramento do processo.
Gate v0.3 GREEN em 2026-09-14: 18.994 host, 1.482 ELF/ABI e 3.203 checks
QEMU; total de 23.679, zero falhas. As 14 VMs foram encerradas e coletadas.
As quatro suítes de heap (64/256/512 MiB e 64 MiB sem NX) completaram
98.304 operações aleatórias no kernel. Registro: [validação v0.3](validation-astra-v0.3.json).
Esses resultados não substituem validação visual, UEFI ou em hardware físico.
