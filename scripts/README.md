# Scripts do UTAMO OS

Os scripts usam as ferramentas existentes em Linux/WSL2 Ubuntu e trabalham
a partir da raiz do checkout. Não instalam pacotes, não atualizam a toolchain nem
dependências externas e não alteram configurações globais.

## make-iso.sh

`make iso` chama explicitamente Bash para gerar uma ISO híbrida BIOS/UEFI
em `build/`, a partir do ELF e do checkout local fixado de Limine. O script
prepara `build/iso_root`, copia assets e chama `limine bios-install` sobre
o arquivo temporário da ISO. Não escreve em um disco físico.

A criação da ISO já foi executada nos marcos incrementais; o resultado e o
hash da imagem final constam no
[relatório da implementação](../docs/v0.1-implementation-report.md).
Gerar uma ISO híbrida não comprova boot sob ambos os firmwares.

## inspect-elf.py

~~~sh
python3 scripts/inspect-elf.py
~~~

Usa apenas a biblioteca padrão Python para inspecionar
`build/utamo-kernel.elf`; um argumento posicional permite outro ELF.
Confere ELF64 x86-64 estático, limites das tabelas/seções, mapeamento
higher-half, permissões, requests Limine, símbolos e ausência de dependências
dinâmicas. Também verifica os bytes ligados dos 256 stubs/tabela relativa e
a convenção Assembly/C de entrada/retorno das interrupções.

A saída informa o número de checks e o processo falha no primeiro contrato
violado. Essa inspeção faz parte de `make inspect` e complementa os testes de execução;
não carrega GDT/IDT nem executa instruções privilegiadas.

## test-qemu.py

Todos os modos usam `-display none`, q35/TCG, um core, 256 MiB e nenhuma
rede. O script usa biblioteca padrão Python, QEMU e, para leituras/probes,
o GDB já instalado. Não compila nem gera a ISO automaticamente.

~~~sh
python3 scripts/test-qemu.py --marker "utamo> " --name final-boot \
    --check-gdt --check-idt --check-timer
python3 scripts/test-qemu.py --marker "utamo> " --name final-ud2 \
    --check-gdt --check-idt --probe exception_fault_ud2 \
    --probe-at cpu_wait_interrupt --debug
~~~

Execute sequencialmente. `--name` é obrigatório e deve ser novo para cada
execução; a pasta existente é recusada. Se houver exatamente uma ISO UTAMO
em `build/`, ela é selecionada; caso contrário, informe `--iso`.
O timeout padrão é 90 segundos, ajustável com `--timeout`.
A trava local serializa o harness e a inspeção de `/proc` recusa outro
QEMU existente. Somente o subprocesso criado pelo script é encerrado e
recolhido em `finally`, também após erro ou timeout.

| Opção | Observação |
| --- | --- |
| `--marker` | Espera um texto real da serial |
| `--check-gdt` / `--check-idt` | Inspeciona selectors, GDTR e IDTR por HMP/QMP |
| `--check-timer` | GDB lê `ticks` duas vezes com execução da VM entre leituras |
| `--probe` | Executa explicitamente UD2/div0/page fault por redirecionamento GDB antes de HLT |
| `--probe-at` | Símbolo do breakpoint; usar `cpu_wait_interrupt` na imagem final |
| `--debug` | Salva log QEMU de interrupções, resets e guest errors |
| `--gdb-port` / `--hold` | Janela limitada para inspeção externa; endpoint somente loopback |
| `--suite` / `--fault` | Entrada por QMP no PS/2 emulado; preparados; não incluídos na validação automatizada registrada |
| `--capture-framebuffer` | Captura headless opcional; não executada para afirmar validação visual |

No boot anterior à introdução do shell, o breakpoint de probe era
`cpu_halt`. Após HLT, apenas alterar RIP não acordou a CPU no QEMU usado;
por isso o modo de probe inicia com `-S` e para por hardware antes de HLT.
Não há código de auto-fault adicionado ao boot normal.

Os comandos de teclado e capturas permanecem disponíveis para uma sessão
futura escolhida pelo usuário. A validação automatizada registrada não os executou nem abriu uma janela
gráfica. O aceite manual em QEMU/VNC foi confirmado separadamente pelo usuário. Inicialização PS/2 bem-sucedida e testes host de parser
não substituem evidência de IRQ1/digitação real.

As evidências ficam em `build/validation/<name>/`. O JSON registra checks,
revisão/status Git, hashes do ELF/ISO, versão/argumentos do QEMU e confirmação
de processo recolhido; serial e arquivos GDB/HMP permitem revisar a execução.
`make clean` remove esse diretório. Registre os resultados antes de limpar
e mantenha os artefatos finais para revisão.

Veja [debugging](../docs/debugging.md) para as instruções de sessão e
[testes](../tests/README.md) para os limites de cada camada de validação.
