# Libc de userspace — reservada

Não implementada. A biblioteca freestanding hoje necessária ao kernel fica
em `kernel/lib/`. Esta pasta será a libc de programas UTAMO, construída sobre
uma ABI própria de syscalls. Não contém libc do host nem implica compatibilidade
POSIX. Não faz parte do build de v0.0.1.
