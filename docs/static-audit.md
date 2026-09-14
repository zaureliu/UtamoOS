# Auditoria estática de geração — 0.0.1

Data: 2026-09-13. Escopo: todos os fontes C/headers/NASM, linker, Makefile,
configuração Limine, script de ISO, testes preparados, licenças e documentação.

**Método:** leitura direta, busca textual, cotejo de interfaces e revisão
independente entre componentes. Foram feitas uma primeira revisão de integração
e uma segunda revisão completa, seguida de releitura das correções.
Não foram executados compiladores, assembler, linker, Make, testes, scripts,
QEMU, GDB ou ferramentas de análise que compilem/executem o projeto. Não houve
dry run de Make nem `bash -n`. Portanto, este relatório não contém aprovação
de build, de testes ou de boot.

## Primeira revisão

| Área examinada | Verificação por leitura | Resultado/correção |
| --- | --- | --- |
| Includes e interfaces | Headers próprios, declarações/definições, dependências | Contratos separados; include Limine limitado ao adaptador |
| ABI Limine | IDs, markers, base3, API2, layouts e revisões de features | Subset identificado, avisos preservados, assertions de ABI preparadas |
| Entrada e halt | `_start`, alinhamento de RSP, direção, pilha, retorno | Pilha própria; `cli`/`cld`; halt permanente com retorno defensivo |
| ELF e linker | Entrada, higher half, PHDRs, KEEP e seções | Requests RW retidos; texto RX; DWARF fora de PT_LOAD |
| Seções sintéticas | GOT/PLT/REL/RELA e strict orphan handling | Seções reconhecidas explicitamente e exigidas vazias por ASSERT |
| Lib freestanding | Símbolos de memória e risco de chamadas recursivas geradas | Primitivas próprias; flags anti-builtin e anti-loop-pattern também no host |
| Formatter | `va_copy`, mínimos assinados, buffers, grammar | Sem negação assinada de LLONG_MIN; NUL/truncamento e unknown formats definidos |
| Vídeo | Pitch, endian, masks, shifts, pixels, cursor | Escritas por byte; masks disjuntas; quebra de tab após wrap corrigida |
| Memory map | Overflow, ordenação, sobreposição, alinhamento e totais | Cópia própria, rejeição atômica de inserções e descarte de importação parcial |
| Serial e panic | Polling, assinatura NASM/C, erro e reentrada | Loops limitados; fallback serial; limitações pré-IDT documentadas |
| Build/testes | Listas de fontes, objetos, flags e targets | memory_map incluído nos testes; dois executáveis com mains separados |
| ISO | Paths, licença, release e destino de bios-install | Checkout fixado por tag+commit; licenças copiadas; saída só em build |

## Segunda revisão completa

A leitura cruzada voltou aos fontes finais, incluindo fonte bitmap, ambos os
programas de host, dependências entre targets, comentários, paths e documentação.
Nenhum novo bloqueador funcional foi identificado por leitura após as correções
abaixo. Essa conclusão tem somente o alcance do método estático usado.

Correções e esclarecimentos aplicados:

- Comparação do enum de tipo de memória evita teste redundante contra zero
  quando sua representação é sem sinal; valores negativos e altos continuam rejeitados.
- Framebuffer ganhou limites explícitos de 8192 pixels por eixo e 256 MiB
  incluindo pitch/padding, com casos de teste exatos e excedidos preparados.
- Foram retiradas comparações redundantes com `SIZE_MAX` em campos já de 64 bits;
  divisão antes do produto continua protegendo a extensão.
- Argumentos variádicos de versão/nome são explicitamente `const char *`, iguais
  ao tipo consumido por `%s`. O contrato foi documentado no formatter e nos testes.
- Extensões GNU de DWARF preservam seus próprios nomes de seção.
- Comandos do README esclarecem o prefixo absoluto do cross compiler local,
  sem depender de alteração de PATH.
- Os nomes dos targets e dos caminhos de artefatos foram harmonizados entre docs.
- A documentação distingue checagem aritmética de comprovação de mapeamento.
- O caso `memcpy(p, p, n)` foi explicitado como suportado, incluindo teste preparado,
  conforme a necessidade do GCC freestanding.

## Cobertura das revisões

- Todos os headers próprios têm guards e dependem de tipos disponíveis.
- As funções públicas dos módulos têm declaração correspondente; rotinas NASM
  usam os símbolos/argumentos esperados pelo C e pelo entry point do linker.
- Não foi identificada dependência circular de headers próprios.
- Includes de libc hosted aparecem somente nos programas de testes; o kernel
  tem apenas os headers freestanding e os próprios.
- Não há stubs de subsistemas futuros que finjam sucesso. Algumas APIs de
  biblioteca são usadas pelos testes ou estão preparadas para chamadas emitidas
  pelo compilador; `--gc-sections` poderá descartá-las do ELF se não referenciadas.
- As regras C/NASM geram dependências por arquivo e recompilam quando o Makefile
  muda. Alterações externas de toolchain/flags exigem build limpo, como documentado.
- A formatação usa tipos variádicos explícitos, checks de capacidade e aritmética
  sem sinal para magnitudes. Ponteiros e strings continuam sujeitos ao contrato
  de validade do chamador.
- Os arquivos de projeto foram mantidos dentro do diretório confirmado
  `C:/Users/anderson.justino/Desktop/UtamoOS`.

## Riscos que permanecem para validação real

1. Compatibilidade efetiva de GCC/binutils/NASM com todas as flags, warnings,
   seções sintéticas e DWARF do linker estrito.
2. Resultado ELF, símbolos indefinidos, alinhamento da pilha gerado e ausência
   de instruções SIMD/FPU exigem inspeção do binário futuro.
3. Execução das duas suítes host; a expectativa manual de cada teste pode
   conter erros que só aparecerão ao compilar/executar.
4. Geração de ISO e reconhecimento do arquivo/configuração por Limine em BIOS
   e UEFI, usando exatamente a release selecionada.
5. Legibilidade visual da fonte e formatos/mappings de framebuffer recebidos.
6. Comportamento de COM1, disponibilidade de saída em panic e halt observado.
7. Sem IDT/IST próprios, uma exceção pode impedir o panic e causar triple fault.
   `cli` não oferece proteção contra NMI, machine checks ou acessos inválidos.
8. Logger e terminal são restritos a bootstrap de um CPU; não podem ser usados
   por IRQs/SMP antes de novos contratos de sincronização.
9. O limite de 512 entradas de memória e os limites de vídeo podem rejeitar
   firmware incomum. São erros explícitos, não truncamentos silenciosos.
10. ISO bit a bit reproduzível, Secure Boot e hardware físico estão fora da
    validação realizada e do primeiro roteiro.

Próxima evidência necessária: seguir [boot-validation.md](../tests/boot-validation.md)
no computador pessoal e registrar resultados realmente observados.
