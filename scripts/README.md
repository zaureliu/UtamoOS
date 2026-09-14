# Scripts

`make-iso.sh` prepara uma ISO híbrida em `build/` a partir do ELF e do checkout
local fixado de Limine. Use `make iso` no ambiente pessoal; o Makefile chama
explicitamente Bash, sem depender do bit executável preservado pelo Windows.

Não há downloads automáticos, instalação global ou escrita em dispositivos.
O script remove apenas seu staging em `build/iso_root`, copia assets e chama
`limine bios-install` no arquivo temporário da ISO. Não foi executado durante
a geração, inclusive para verificação de sintaxe.
