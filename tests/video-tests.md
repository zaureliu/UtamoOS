# Testes de framebuffer, fonte e terminal

A suíte host de vídeo foi executada no baseline e na validação v0.1.0.
Ela integra os 5214 checks host registrados na release; esse total cobre
as nove suítes, não somente vídeo. Resultados e limites estão no
[README de testes](README.md) e no
[relatório v0.1](../docs/v0.1-implementation-report.md).

O arquivo test_video.c é um programa de teste para host, separado do kernel e
da suíte de biblioteca. O target `make test-host` compila e executa todas as suítes host,
incluindo build/tests/utamo-video-tests. Nenhum driver de hardware, bootloader,
comando privilegiado ou VM é necessário para os testes de vídeo.

O framebuffer recebe pequenos arrays de RAM como superfícies. As comparações
usam bytes esperados e pontos geométricos fixos, sem chamar o empacotador de
cores ou as funções de desenho como oráculo.

Cobertura exercitada nos testes host:

- RGB de 24 e 32 bpp, ordem de canais invertida e bits não utilizados zerados.
- Escalonamento para canais de um bit e para RGB565 armazenado em 24 bpp.
- Linhas com padding e bytes de guarda antes e depois da superfície.
- Coordenadas fora dos limites, incluindo SIZE_MAX.
- Configurações nulas, dimensões vazias, pitch insuficiente, máscaras inválidas,
  sobreposição de canais e overflow de extensão/endereço.
- Aceitação exatamente nos limites de 8192 pixels por eixo e 256 MiB por
  superfície, além da rejeição de valores imediatamente superiores.
- Limites dos 95 glifos ASCII, espaço vazio e fallback para caracteres ausentes.
- Inicialização do terminal, desenho do ápice de A e duplicação vertical.
- Cursor, quebra adiada, newline, limpeza ao atingir o fim, tabulação,
  carriage return, backspace e controles ignorados.
- Superfícies menores que uma célula e terminais inativos.

Os testes de overflow fabricam somente um endereço próximo a UINTPTR_MAX para
verificar rejeição aritmética, sem desreferenciá-lo. Assim como o kernel, esse
caso pressupõe o modelo de endereços plano dos hosts x86. Os demais testes
fornecem armazenamento real suficiente para cada configuração aceita.

Os casos aceitos nos limites reservam um array estático de 256 MiB em BSS.
A suíte usa somente seus metadados, sem limpar ou desenhar nessa superfície.
Isso exige espaço de endereçamento virtual do host, mas evita tocar centenas de
megabytes apenas para verificar as guardas de inicialização.

Limites: buffers de RAM não validam mapeamento do framebuffer por Limine,
atributos de cache, formatos efetivamente oferecidos pelo firmware, qualidade
visual em QEMU ou hardware, nem legibilidade de todos os glifos. Essas
verificações exigem observação no guest. O usuário confirmou manualmente
input e comandos do shell, incluindo `clear` e `halt`, em QEMU/VNC; isso não
representa inspeção individual de todos os glifos, formatos ou modos de vídeo.
