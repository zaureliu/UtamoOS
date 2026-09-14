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
#define UTAMO_SYS_OPEN 6
#define UTAMO_SYS_READ 7
#define UTAMO_SYS_CLOSE 8
#define UTAMO_SYS_SEEK 9
#define UTAMO_SYS_FSTAT 10
#define UTAMO_SYS_SPAWN 11
#define UTAMO_SYS_WAIT 12
#define UTAMO_SYS_INFO 13
#define UTAMO_SYS_ENOENT -5
#define UTAMO_SYS_EMFILE -6
#define UTAMO_SYS_EIO -7
#define UTAMO_SYS_EAGAIN -8
#define UTAMO_SYS_EEXEC -9
#ifndef __ASSEMBLER__
#include <stdint.h>
struct utamo_system_info {
    uint64_t pid, ticks, free_pages, page_size;
};
#endif
#define UTAMO_SYS_EINVAL -1
#define UTAMO_SYS_EFAULT -2
#define UTAMO_SYS_ENOSYS -3
#define UTAMO_SYS_EBADF -4
#define UTAMO_SYS_WRITE_LIMIT 1024
#endif
