/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_SYSCALL_ABI_H
#define UTAMO_SYSCALL_ABI_H
/* UTAMO native ABI: INT 0x80; RAX number/result; RDI, RSI, RDX arguments.
 * All other GPRs survive. Negative results are signed 64-bit errors.
 * No Linux ABI compatibility. These numeric defines also generate NASM input. */
#define UTAMO_SYS_WRITE 1
#define UTAMO_SYS_EXIT 2
#define UTAMO_SYS_GETPID 3
#define UTAMO_SYS_YIELD 4
#define UTAMO_SYS_SLEEP 5
#define UTAMO_SYS_EINVAL -1
#define UTAMO_SYS_EFAULT -2
#define UTAMO_SYS_ENOSYS -3
#define UTAMO_SYS_EBADF -4
#define UTAMO_SYS_WRITE_LIMIT 1024
#endif
