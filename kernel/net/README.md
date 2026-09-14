# Rede

Ethernet, ARP, IPv4, ICMP echo, transações UDP, DHCP e DNS A estão implementados
em módulos próprios. O driver E1000 fica em kernel/drivers/. A thread de
bootstrap/shell possui a pilha de rede; polling e esperas têm limites, e
parsers não alocam memória. Não há worker de rede, sockets de userspace ou TCP.

Veja [contratos e validação de rede](../../docs/networking.md) e o
[estado do gate v0.8](../../docs/astra-campaign-state.md). Código implementado
não implica gate aprovado.
