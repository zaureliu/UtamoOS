# Processo de boot — UTAMO OS

A validação de memória introduzida em v0.2 permanece descrita abaixo, com
as etapas atuais de heap, scheduler, CPL3, VFS, armazenamento e rede.
A aprovação de cada imagem está no [estado da campanha](astra-campaign-state.md).

## Contrato mantido

UTAMO usa Limine v8.7.0, base revision 3 e API revision 2 do header selecionado.
Framebuffer e memory map usam revision 0; paging usa revision 1 com mínimo
e máximo em quatro níveis. HHDM e executable address, acrescentados para
memória, usam revision 0. O adaptador copia os campos necessários para tipos
`utamo/`; nenhuma outra camada depende de structs Limine.

As declarações permanecem no subset local de
[limine.h](../third_party/limine/limine.h), com proveniência documentada.
Não se usa terminal Limine, identity mapping improvisado ou APs. Ring 3 é
inicializado pelo próprio kernel; o módulo initramfs é solicitado a Limine.

## Ordem implementada

1. Firmware e Limine carregam o ELF definido em `limine.conf`.
2. O linker preserva os requests em segmento gravável. Limine preenche as
   respostas e zera a BSS conforme o carregamento ELF.
3. `_start` executa CLI/CLD, seleciona stack própria de 64 KiB, alinha RSP
   a 16 bytes antes de CALL e zera RBP.
4. `kernel_main` inicializa COM1; valida protocolo e paginação antes de usar
   as demais respostas; inicializa framebuffer, terminal e logging.
5. Copia e valida o mapa físico em armazenamento próprio.
6. Carrega GDT/TSS/IST e IDT, conectando a saída fatal de exceções.
7. `memory_init` valida HHDM/bases do executável, MAXPHYADDR, LA57 e NX.
8. Lê CR3 e percorre as tabelas herdadas antes de escrever bitmaps.
   Exige seus frames em BOOTLOADER_RECLAIMABLE; verifica os mappings do
   kernel, HHDM e framebuffer e o slot dinâmico vazio.
9. Planeja os dois bitmaps PMM em USABLE, inicializa a elegibilidade e reserva
   página zero, metadados, imagem e framebuffer. Memória reclaimable continua
   reservada. Nenhum frame de tabela herdada entra no allocator.
10. Habilita/verifica CR0.WP e aplica permissões às seções quando as folhas
    herdadas são compatíveis. Confirma CR3 preservado e publica VMM pronto.
11. Inicializa heap, remapeia PIC com fontes mascaradas e configura PIT.
12. Inicializa scheduler/idle, infraestrutura CPL3 e mount obrigatório do
    initramfs. Uma CPU sem NX recusa userspace e conserva a shell de kernel.
13. Configura PS/2, libera somente IRQ0/IRQ1 e habilita IF.
14. Enumera PCI; tenta AHCI e FAT32 readonly. Ausência de disco ou rejeição do
    dispositivo/volume mantém a VFS do initramfs disponível.
15. Tenta inicializar E1000 e obter configuração DHCP com esperas limitadas.
    Ausência de NIC ou falha de aquisição permite continuar sem rede configurada.
16. Quando CPL3 está disponível, executa /bin/init como PID 1 e aguarda seu
    encerramento após os programas demonstrativos. Falha do init é fatal.
17. Emite `UTAMO OS ready.`, inicializa shell e mostra `utamo> `.
18. Processa input e reap fora da ISR. A shell bloqueia atomicamente após
    consultar a fila com IF=0; IRQ1 a acorda. A idle usa STI/HLT contíguos.
    Rede é processada na thread de bootstrap/shell durante comandos de rede,
    sem trabalho de protocolo nos handlers de PIT/PS2.

`pmmtest`, `vmmtest` e probes fatais só rodam mediante comando explícito
ou seleção pelo debugger. Não fazem parte do boot normal.

## Vida útil e falhas

O mapa, framebuffer e terminal pertencem ao boot durante toda a execução.
A pilha própria e os descritores próprios removem algumas dependências do
loader, mas suas page tables e respostas permanecem preservadas.
Não há reclaim de BOOTLOADER_RECLAIMABLE/ACPI. O CR3 de kernel é preservado
como raiz compartilhada; o scheduler seleciona raízes privadas para processos.
[Gerenciamento de memória](memory-management.md), [processos](processes.md),
[armazenamento](storage.md) e [rede](networking.md) descrevem ownership e checks.

| Falha | Comportamento |
| --- | --- |
| COM1 indisponível | Continua com saída gráfica quando possível |
| Protocolo, paging, framebuffer ou mapa inválidos | Panic pelos sinks disponíveis |
| IDT ou PS/2 não inicializados | Panic; interrupções ainda não liberadas |
| HHDM, CPU, reservas ou tabelas incompatíveis | `Cannot initialize physical/virtual memory safely`, sem continuar o shell |
| Proteção de seções incompatível com folhas herdadas | Aviso explícito de proteção adiada; não divide huge pages |
| Exceção de kernel após IDT ou IST crítica | Dump completo serial, diagnóstico opcional e tentativa de framebuffer; halt |
| Falha não crítica de CPL3 | Diagnóstico, encerramento do processo e reap posterior |
| Initramfs inválido ou init nativo malsucedido | Panic |
| Disco/NIC ausente ou inicialização rejeitada | Continua com os subsistemas disponíveis |
| Page fault com VMM pronto | Acrescenta snapshot de mapping sem alocar, depois do dump original |
| `halt` | Mensagem e parada permanente com IF=0 |

Antes de IDT, falhas de CPU ainda dependem do handoff e podem exigir GDB.
IST não recupera falhas e CLI não bloqueia NMI/machine checks.
Os aliases HHDM limitam a política de permissões; não há W^X global,
page-in ou shutdown ACPI. O scheduler e o isolamento de processos estão
implementados; contenção de falhas CPL3 não recupera corrupção de kernel.

## Artefatos e validação

A versão central em `utamo/version.h` determina a identificação e o nome
`build/utamo-os-<versão>.iso`. ELF e símbolos ficam em
`build/utamo-kernel.elf`; os assets da ISO contemplam BIOS e x86_64 UEFI.
Criar a ISO não comprova boot UEFI.

O [guia de debugging](debugging.md) separa host, inspeção ELF, QEMU headless
e validação manual. As notas e evidências históricas
[v0.1.0](releases/v0.1.0.md) permanecem preservadas e não validam
automaticamente uma nova imagem da campanha.
