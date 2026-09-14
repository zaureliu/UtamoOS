# Scripts do UTAMO OS

Os scripts usam ferramentas existentes em Linux/WSL2 e operam dentro do checkout.
Não instalam pacotes nem alteram toolchain, dependências ou configuração global.
Kernel/ISO devem estar construídos antes de iniciar testes de VM.

## make-iso.sh

`make iso` chama Bash e gera `build/utamo-os-<versão>.iso`, derivando a versão
do header central. Copia o ELF e assets locais do Limine fixado, prepara a árvore
ISO e aplica `limine bios-install` somente à imagem temporária.
Não grava discos físicos. Assets BIOS/UEFI na ISO não comprovam execução UEFI.

## inspect-elf.py

~~~sh
python3 scripts/inspect-elf.py
~~~

Biblioteca padrão Python, sem dependências adicionais.
Inspeciona `build/utamo-kernel.elf`, ou outro ELF passado como argumento.
Confere ELF64 estático x86-64, segmentos higher half, seções/permissões,
requests Limine, símbolos e ausência de dependências dinâmicas.
Também examina os bytes ligados de stubs/tabela relativa e convenção Assembly/C.
Está integrado em `make inspect`; falha no contrato violado e informa checks.
Inspeção estática não executa CR3, LGDT/LIDT, INVLPG ou IRETQ.

## test-qemu.py

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name boot-v02 \
    --check-gdt --check-idt --check-timer
python3 scripts/test-qemu.py --marker "utamo> " --name ud2-v02 \
    --check-gdt --check-idt --probe exception_fault_ud2 \
    --probe-at cpu_wait_interrupt --debug
~~~

Todos os modos são headless (`-display none`), q35/TCG, um core e sem rede.
Usa biblioteca padrão Python, QEMU e GDB existente quando necessário.
O timeout padrão é 90 segundos. `--name` é obrigatório e novo;
uma pasta de evidência existente é recusada.
Se houver mais de uma ISO em build, indique `--iso`.

| Opção | Contrato |
| --- | --- |
| `--marker` | Aguarda texto observado na serial |
| `--check-gdt` / `--check-idt` | Lê selectors/GDTR/IDTR por HMP/QMP |
| `--check-timer` | Lê ticks por GDB em dois momentos de execução |
| `--probe` / `--probe-at` | Redireciona execução por GDB antes de HLT para probe explícito |
| `--suite` / `--fault` | Entrada QMP no teclado PS/2 emulado |
| `--debug` | Guarda log interno QEMU de IRQ, resets e erros |
| `--capture-framebuffer` | Captura opcional, separada da leitura serial |
| `--gdb-port` / `--hold` | Inspeção externa limitada com endpoint loopback |

A validação automatizada histórica v0.1 não usou teclado QMP/capturas;
o aceite gráfico daquela release veio do usuário.
Na sessão v0.2, teclado QMP foi autorizado para a suíte de memória.
Uma captura só pode ser chamada de validação visual se foi executada e analisada.

## test-memory-qemu.py

~~~sh
python3 scripts/test-memory-qemu.py --suite --name memory-v02 --timeout 180
python3 scripts/test-memory-qemu.py --fault vmm --name unmapped-v02
python3 scripts/test-memory-qemu.py --fault ro --name readonly-v02
python3 scripts/test-memory-qemu.py --fault nx --name nx-v02
python3 scripts/test-memory-qemu.py --fault pf --name legacy-pf-v02
~~~

Reutiliza o harness anterior para controlar a VM, enviar teclado QMP e colher
evidências. Não gera ISO, não altera o kernel e não abre janela gráfica.
Lê símbolos do ELF com o nm da toolchain local.
Os modos `--suite` e `--fault` são mutuamente exclusivos.

A suíte lê PMM/VMM, consulta mappings de seções e HHDM, observa CR0/CR3/CR4/EFER,
repete selftests e compara a contabilidade antes/depois.
Verifica custo inicial de tabelas fixadas e reutilização nas repetições,
proteção do texto, PIT ativo, comandos básicos, Backspace/Shift, clear e halt.
Os probes vmm/pf entram pelo shell; RO/NX entram pelo breakpoint GDB antes de HLT.
Verificam CR2, error code, registradores, flags e snapshot VMM sem alocação.

`--ram` (padrão 256M) e `--cpu` (qemu64) parametrizam a VM.
A suíte espera NX habilitado por padrão. Para testar o fallback em CPU sem NX,
combine `--cpu qemu64,-nx --expect-nx off`; o harness verifica suporte/NXE
desabilitados e ausência de flags NX, mantendo os demais checks.
Esse cenário não demonstra proteção de execução. `--version` fixa o semver esperado, ou lê version.h; o mesmo contrato vale
para o harness original test-qemu.py.
`--timeout` tem padrão 180 segundos e máximo 600.
O harness não solicita capturas de framebuffer.

## Evidência e ciclo de vida

Execute uma VM por vez. Lock local e verificação de processos evitam
sobreposição; somente o processo criado é encerrado/recolhido em finally,
também após falha ou timeout. Um processo externo não deve ser encerrado
apenas para liberar o teste.

Resultados ficam em `build/validation/<name>/`: report.json, serial,
registros HMP/GDB e logs adicionais. O relatório contém horários, Git,
hashes ELF/ISO, configuração QEMU, checks e confirmação de cleanup.
Resultados pertencem à imagem identificada por esses hashes.
`make clean` remove build e seus relatórios; preserve evidências necessárias
no projeto antes de limpar.

Os roteiros completos estão em [debugging](../docs/debugging.md), contratos
em [memory-management](../docs/memory-management.md) e cobertura em
[tests/README](../tests/README.md).
