# Limine: proveniência e versão

O arquivo `limine.h` é uma **adaptação reduzida** das declarações oficiais em
[Limine v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/limine.h),
consultadas durante a geração. Não é cópia integral nem afirma identidade de hash
com o header upstream. O comentário BSD Zero Clause original foi preservado.

Seleção: C, ponteiros nativos de 64 bits, x86_64, API revision 2. O kernel solicita
base revision 3. Foram mantidos os marcadores, base tag, framebuffer, memory map
e paging request necessários; omitidas outras arquiteturas, aliases legados,
compatibilidade C++ e features não usadas. `LIMINE_PTR(TYPE)` foi expandido para
o tipo nativo. Nomes, IDs, campos, ordem e larguras das features usadas seguem o
header oficial. `boot.c` contém assertions do layout esperado.

Requests de framebuffer/memória têm revision 0 e paging revision 1. Campos novos
do framebuffer (`mode_count`/`modes`) aparecem no layout oficial, mas não são
acessados por esta versão; portanto não exigimos resposta revision 1.

O bootloader binário **não acompanha esta geração**. No ambiente pessoal,
o checkout `vendor/` deverá conter `v8.7.0-binary`, commit
`aad3edd370955449717a334f0289dee10e2c5f01`. O script de ISO confere versão e
árvore rastreada antes de copiar os assets. Procedimento em
[development-environment.md](../../docs/development-environment.md).

Fontes oficiais para revisão/atualização:

- [Header C v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/limine.h).
- [Protocolo v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/PROTOCOL.md).
- [Configuração v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/CONFIG.md).
- [Construção de imagem v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/USAGE.md).
- [Release binária selecionada](https://github.com/limine-bootloader/limine/releases/tag/v8.7.0-binary).

Não substitua o header ou o bootloader isoladamente. A licença MIT do UTAMO não
substitui a licença dos componentes Limine. Preserve também `vendor/LICENSE`
ao distribuir a imagem; o script já prepara essa cópia.

## UTAMO v0.2 memory requests

The reduced header also includes HHDM and Executable Address request/response
declarations, checked against the same official v8.7.0 header and protocol
(API revision 2). Both requests use revision 0. No bootloader binary, vendor
checkout or toolchain was changed. Their ABI layouts are asserted in boot.c.

Base revision 3 maps only USABLE, BOOTLOADER_RECLAIMABLE, EXECUTABLE_AND_MODULES
and FRAMEBUFFER regions through HHDM. Bootloader tables and response structures
remain reserved; UTAMO performs no reclaim in v0.2.

## UTAMO v0.6 module request

The reduced header now additionally exposes the v8.7.0 module request,
module response, internal-module and file/UUID layouts. These declarations
were checked against the same upstream v8.7.0 source; the BSD notice is retained.
The request uses revision 0, no internal modules, and selects the configured
module by its exact cmdline label. boot.c asserts request/file size and offsets.
Neither the vendor checkout nor the bootloader binary was modified.
