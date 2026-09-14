# Syscalls nativas

A ABI INT128 é implementada em kernel/core/process_syscall.c e
process_files.c. Inclui WRITE, EXIT, GETPID, YIELD, SLEEP, descritores VFS,
SPAWN, WAIT e INFO. Entrada CPL3 usa TSS/RSP0 e o frame comum de interrupção;
user copies validam todo o intervalo antes de copiar ou alterar offsets.

Este diretório conserva o ponto de documentação da organização original.
Não há ABI Linux/POSIX, fork, input de userspace ou sockets. Veja
[ABI e erros](../../docs/syscalls.md), [runtime](../../docs/userspace.md) e
[isolamento de processos](../../docs/processes.md).
