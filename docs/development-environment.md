# Ambiente de desenvolvimento pessoal

Estado desta entrega: arquivos-fonte e infraestrutura implementados; compilação,
scripts, testes e boot pendentes. Nenhum comando deste documento foi executado na
máquina corporativa. Os exemplos abaixo são instruções para o proprietário
executar posteriormente em seu computador pessoal.

## Plataforma e ferramentas

Use Linux nativo x86_64 ou uma instalação pessoal de WSL2 com Ubuntu e terminal
Bash. A instalação do WSL2 fica a cargo do usuário; este projeto não modifica
Windows, PATH ou configurações do sistema. No WSL2, copie `UtamoOS` para um
diretório do filesystem Linux, sem espaços no caminho. O Makefile utiliza GNU
Make, Bash no script de ISO, `find` e utilitários POSIX; não foi projetado para
PowerShell ou `cmd.exe`.

| Ferramenta | Uso |
| --- | --- |
| GCC/G++ nativos e binutils | Construir o cross compiler e o utilitário host do Limine; testes de biblioteca |
| `x86_64-elf-gcc` | Compilar C17 freestanding para o kernel |
| `x86_64-elf-as`, `ld`, `readelf`, `nm`, `objdump` | Ferramentas GNU binutils para ELF64 bare metal |
| NASM | Montar os arquivos `.asm` em ELF64 |
| GNU Make | Orquestrar compilação e dependências |
| `xorriso` | Criar a ISO híbrida BIOS/UEFI |
| Git | Obter e conferir a revisão exata do Limine |
| QEMU system x86 | Executar futuramente a máquina virtual x86_64 |
| GDB com suporte x86_64 | Depurar o kernel pelo stub do QEMU |
| OVMF, opcional | Firmware UEFI para a validação complementar |

Os seguintes comandos são **somente exemplos para Ubuntu na máquina pessoal**.
Eles instalam ferramentas no ambiente pessoal; não são parte de `make`, nem
devem ser executados no computador corporativo:

```sh
sudo apt update
sudo apt install build-essential binutils bison flex libgmp-dev libmpfr-dev \
    libmpc-dev texinfo nasm make xorriso qemu-system-x86 gdb git ovmf xz-utils
```

QEMU usa TCG por padrão neste projeto, sem exigir KVM ou permissões de acesso a
dispositivos. A janela gráfica precisa de uma sessão Linux com display, ou WSLg
funcional no ambiente pessoal. Esses requisitos não foram verificados durante a
geração. O pacote `gcc` do Ubuntu, sozinho, não fornece `x86_64-elf-gcc`.

## Por que usar um cross compiler

O target do kernel é `x86_64-elf`, sem ABI de Linux e sem biblioteca do host.
O Makefile não usa a variável implícita `CC=cc` para compilar o kernel: constrói
explicitamente o nome `$(CROSS_COMPILE)gcc`. Um compilador nativo Linux pode
introduzir pressupostos de distribuição, includes e padrões de link inadequados.
Mesmo em Linux x86_64, use o cross compiler especificado.

Os headers `stdint.h`, `stddef.h`, `stdbool.h`, `stdarg.h` e `limits.h` vêm do
ambiente freestanding do compilador. O kernel fornece as rotinas de memória e
strings de que precisa. O link usa `-nostdlib`, sem libc, CRT, loader dinâmico ou
`libgcc`. Se uma alteração gerar helpers como `__udivti3`, o símbolo indefinido
deve ser investigado; não adicione bibliotecas do host para mascará-lo.

## Construção opcional do cross compiler dentro do projeto

Se já houver uma toolchain confiável `x86_64-elf`, pule esta etapa e informe seu
prefixo absoluto ao Makefile. Não é necessário alterar PATH.

Uma combinação de referência para construir a ferramenta é GCC 14.2.0 com GNU
binutils 2.43.1. É uma seleção explícita de versões, **ainda não validada com este
projeto**, não uma alegação de ser a versão mais recente. Obtenha os arquivos-fonte
e respectivas assinaturas nos diretórios oficiais
[GCC 14.2.0](https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/) e
[GNU binutils](https://ftp.gnu.org/gnu/binutils/), verifique sua autenticidade pelo
procedimento GNU e coloque os tarballs em `UtamoOS/toolchain/src/`. Preserve os
avisos de licença dessas ferramentas. Nenhum tarball ou binário acompanha esta
entrega.

Execute os comandos abaixo na raiz de `UtamoOS`, somente no computador pessoal.
As pastas de fontes, build e instalação são distintas. A configuração segue as
opções oficiais de [instalação do GCC](https://gcc.gnu.org/install/configure.html).
O prefixo local evita instalação global ou uso de `sudo make install`:

```sh
utamo_project=$(pwd -P)
utamo_prefix="$utamo_project/toolchain/prefix"
mkdir -p toolchain/src toolchain/build-binutils toolchain/build-gcc toolchain/prefix
tar -xf toolchain/src/binutils-2.43.1.tar.xz -C toolchain/src
tar -xf toolchain/src/gcc-14.2.0.tar.xz -C toolchain/src

cd "$utamo_project/toolchain/build-binutils"
../src/binutils-2.43.1/configure --target=x86_64-elf \
    --prefix="$utamo_prefix" --with-sysroot --disable-nls --disable-werror
make -j2
make install

cd "$utamo_project/toolchain/build-gcc"
../src/gcc-14.2.0/configure --target=x86_64-elf \
    --prefix="$utamo_prefix" --disable-nls --enable-languages=c \
    --without-headers --disable-multilib --disable-shared --disable-threads \
    --with-as="$utamo_prefix/bin/x86_64-elf-as" \
    --with-ld="$utamo_prefix/bin/x86_64-elf-ld"
make -j2 all-gcc
make install-gcc
cd "$utamo_project"
```

`all-gcc` é suficiente para o marco atual: não construímos uma libc nem uma
`libgcc` para o target. GMP/MPFR/MPC são dependências da construção do compilador
host; não entram no kernel. O exemplo limita paralelismo a dois jobs para conter
consumo de RAM. Todos os artefatos dessa toolchain ficam em `toolchain/`, ignorada
pelo Git. Os caminhos de assembler/linker configurados são absolutos; se mover a
pasta depois, reconstrua a toolchain ou utilize outra instalada no novo local.

Confira o target e registre versões antes do primeiro build pessoal:

```sh
./toolchain/prefix/bin/x86_64-elf-gcc -dumpmachine
./toolchain/prefix/bin/x86_64-elf-gcc --version
./toolchain/prefix/bin/x86_64-elf-ld --version
nasm -v
make --version
xorriso -version
qemu-system-x86_64 --version
gdb --version
```

O target esperado do compilador é `x86_64-elf`. Registre as saídas reais no seu
diário de desenvolvimento; elas não foram produzidas nesta entrega.

## Limine fixado em v8.7.0

O header em `third_party/limine/limine.h` é uma **adaptação reduzida e explicitamente
identificada** das declarações oficiais da versão **v8.7.0**, com API revision 2
e licença upstream preservada. Não é uma cópia integral do header oficial.
O kernel solicita **base revision 3**, com requests de revisão 0 para framebuffer
e mapa de memória e revisão 1 para selecionar paginação de quatro níveis.
O bootloader deve ser obtido da referência oficial
**v8.7.0-binary**. Essa referência existe no upstream, incluindo o
[Makefile do utilitário host](https://raw.githubusercontent.com/limine-bootloader/limine/v8.7.0-binary/Makefile).
O commit fixado é
[`aad3edd370955449717a334f0289dee10e2c5f01`](https://github.com/limine-bootloader/limine/commit/aad3edd370955449717a334f0289dee10e2c5f01),
identificado pela [release oficial](https://github.com/limine-bootloader/limine/releases/tag/v8.7.0-binary).
Não substitua pela branch de desenvolvimento nem por uma versão nova sem revisar
o header, o protocolo, a configuração e os testes de boot em conjunto.

Somente em casa, a partir da raiz do projeto:

```sh
git clone --branch v8.7.0-binary --depth 1 \
    https://github.com/limine-bootloader/limine.git third_party/limine/vendor
git -C third_party/limine/vendor describe --tags --exact-match
git -C third_party/limine/vendor rev-parse HEAD
make -C third_party/limine/vendor
```

A referência de distribuição contém os artefatos de boot; o último comando compila
somente seu utilitário host `limine` com o compilador nativo. Não execute
`make install` nesse diretório. O script da ISO exige HEAD igual ao commit da tag
local `v8.7.0-binary` e ao SHA registrado acima, sem alterações em arquivos
rastreados. O executável host
recém-compilado não é um arquivo rastreado e pode permanecer no checkout.

Assets necessários em `third_party/limine/vendor/`:

```text
limine                 utilitário host compilado para seu Linux
limine-bios.sys         stage de boot BIOS
limine-bios-cd.bin      imagem de boot óptico BIOS
limine-uefi-cd.bin      imagem de boot óptico UEFI
BOOTX64.EFI            aplicação EFI x86_64
LICENSE                avisos de licença do bootloader
```

O checkout completo conserva as licenças upstream; a ISO inclui a licença do
bootloader em `/boot/limine/LICENSE` e a do projeto em `/UTAMO-LICENSE`.
A rotina de ISO não acessa a
rede, não baixa dependências e não instala Limine em disco físico. Ela aplica
`bios-install` exclusivamente ao arquivo temporário da ISO dentro de `build/`.
A composição da imagem e a configuração acompanham as instruções oficiais
[USAGE v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/USAGE.md) e
[CONFIG v8.7.0](https://github.com/limine-bootloader/limine/blob/v8.7.0/CONFIG.md).

## Build e validação futura

Na raiz do projeto, com a toolchain local descrita acima:

```sh
make test-host
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" kernel
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" inspect
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" iso
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" run
```

Se `x86_64-elf-*` já estiver disponível no ambiente, `make`, `make kernel`,
`make iso`, `make run` e `make debug` funcionam sem informar `CROSS_COMPILE`.
Informar o prefixo absoluto não modifica PATH. `make` sozinho constrói somente
`build/utamo-kernel.elf`, com símbolos DWARF e mapa `build/utamo-kernel.map`.
`make iso` produz `build/utamo-os-0.0.1.iso`. `make clean` apaga somente `build/`;
não apaga fontes, checkout do Limine ou toolchain.

O build usa warnings como erros, sem red zone, sem instruções SIMD/FPU geradas
por C, sem PIE e com seções ELF inesperadas tratadas como erro de link. Essas
opções tornam divergências visíveis. Não desative os diagnósticos em bloco: leia
e corrija a causa. Se mudar `CROSS_COMPILE`, versões de ferramentas ou flags sem
editar o Makefile, faça `make clean` antes de reconstruir, pois timestamps não
registram essas mudanças externas.

Execute apenas uma instância independente de Make por diretório de build.
`make -j2 kernel` é permitido; não misture `clean` com alvos de build na mesma
invocação. Não use symlinks em `build/`. Este marco não promete ISO bit a bit
reproduzível: timestamps e versões das ferramentas ainda precisam de controle.

O teste complementar UEFI usa uma cópia local do NVRAM, mantendo a imagem CODE
somente para leitura:

```sh
make CROSS_COMPILE="$PWD/toolchain/prefix/bin/x86_64-elf-" run-uefi \
    OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd \
    OVMF_VARS=/usr/share/OVMF/OVMF_VARS_4M.fd
```

Informe um par CODE/VARS correspondente ao pacote OVMF instalado; os nomes
podem variar. O target recria `build/OVMF_VARS.fd` a cada execução para começar
com estado conhecido. A validação inicial considera OVMF sem Secure Boot
ativado; assinatura de executáveis EFI não faz parte do marco. Detalhes de
breakpoints, serial, inspeção ELF e critérios de evidência estão em
[debugging.md](debugging.md) e na documentação de testes.
