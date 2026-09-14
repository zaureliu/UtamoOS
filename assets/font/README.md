# UTAMO Basic

A fonte inicial do UTAMO OS é uma tabela original de bitmaps 5 × 7, desenhada
manualmente durante a criação deste projeto. A tabela foi escrita diretamente
em kernel/drivers/video/font.c; não foi extraída de BIOS, ROM, sistema
operacional, arquivo de fonte ou biblioteca externa. Ela está sob a licença
MIT do projeto, disponível em LICENSE.

Cada um dos 95 caracteres ASCII imprimíveis, de espaço (0x20) a til
(0x7e), possui sete linhas de cinco bits. O bit 4 representa o pixel da
esquerda. Há glifos próprios para letras maiúsculas e minúsculas. Os caracteres
fora desse intervalo são representados por ?, salvo controles tratados pelo
terminal. Não há Unicode, composição de acentos, kerning ou carregamento de
fontes nesta versão.

O terminal desenha uma célula de 8 × 16 pixels, com o glifo começando na coluna
1 e linha 1 da célula. Cada linha do glifo é repetida verticalmente duas vezes.
Todos os pixels da célula recebem cor de frente ou fundo a cada desenho; os
espaçamentos não dependem do conteúdo anterior do framebuffer.

O cursor é uma posição lógica. A quebra automática é adiada até o próximo
caractere, evitando apagar uma tela cheia assim que a última célula é escrita.
Uma quebra de linha na última linha limpa a tela inteira e recomeça no topo.
Essa política deliberadamente simples dispensa leitura de memória de vídeo e
buffer auxiliar. Carriage return retorna à primeira coluna, tab emite espaços
até a próxima parada de quatro colunas e backspace apaga a célula anterior na
mesma linha. Outros controles ASCII são ignorados.

Para evolução, uma fonte PSF2 com licença explícita pode ser carregada de um
módulo do bootloader ou do VFS. A abstração de framebuffer não depende do
formato da fonte. Rolagem, cursor visual e Unicode pertencem à evolução do
terminal e não exigem alterar o driver de pixels.

Estado: implementado; inspeção visual dos glifos e testes de renderização
pendentes no ambiente de desenvolvimento pessoal.
