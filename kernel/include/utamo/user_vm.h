/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_USER_VM_H
#define UTAMO_USER_VM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <utamo/vmm_core.h>

#define USER_VM_PAGE_LIMIT 128u
#define USER_VM_TABLE_LIMIT 64u /* Includes the private PML4. */
#define USER_VM_MIN UINT64_C(0x10000)
#define USER_VM_END UINT64_C(0x0000800000000000) /* Exclusive. */
#define USER_VM_COPY_LIMIT ((size_t)65536)
#define USER_VM_WRITE UINT32_C(1)
#define USER_VM_EXEC UINT32_C(2)

/* READ is implicit. access=0 is R+NX; WRITE|EXEC is rejected.
 * The process is the sole owner. Fields are internal, never caller-mutable.
 * No user frames/tables are shared or permanently pinned. HHDM remains a
 * supervisor alias. Empty private tables are retained until destroy.
 */
struct user_vm_page {
    uint64_t virt, phys;
    bool allocated;
};
struct user_vm_table {
    uint64_t phys;
    bool allocated, published;
};
struct user_vm {
    struct vmm_space space;
    struct user_vm_page pages[USER_VM_PAGE_LIMIT];
    struct user_vm_table tables[USER_VM_TABLE_LIMIT];
    size_t page_count, table_count;
    bool initialized;
};

/* vm starts zeroed and stays at the same address until destruction.
 * NX is required; PCID is unsupported. Failed creation leaves vm zeroed.
 * All operations preserve IF and serialize on the BSP; never call from IRQ.
 */
bool user_vm_create(struct user_vm *vm);
bool user_vm_alloc_page(struct user_vm *vm, uint64_t virt, uint32_t access);
/* Unmap also frees the exclusively owned data frame. */
bool user_vm_unmap_page(struct user_vm *vm, uint64_t virt);
bool user_vm_protect_page(struct user_vm *vm, uint64_t virt, uint32_t access);
/* Byte-offset query of owned lower-half mappings; failure preserves out. */
bool user_vm_query(const struct user_vm *vm, uint64_t virt,
                    struct vmm_mapping *out);
/* Kernel buffer is trusted, valid for bytes, and does not overlap user aliases.
 * bytes <= COPY_LIMIT. All pages/aliases are preflighted under IF=0 before any
 * copy: validation failure leaves the destination untouched. Zero length is
 * accepted for an initialized vm without accessing either buffer.
 * Loader: allocate WRITE (RW+NX), copy_to, protect EXEC (RX), then publish.
 */
bool user_vm_copy_from(const struct user_vm *vm, void *kernel_dst,
                       uint64_t user_src, size_t bytes);
bool user_vm_copy_to(struct user_vm *vm, uint64_t user_dst,
                     const void *kernel_src, size_t bytes);
/* Caller first detaches the process from the scheduler and every external
 * reference. Active CR3 is rejected. Successful destroy clears the object.
 */
bool user_vm_destroy(struct user_vm *vm);

#endif
