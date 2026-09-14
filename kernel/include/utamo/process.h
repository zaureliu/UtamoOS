/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_PROCESS_H
#define UTAMO_PROCESS_H
#include <utamo/arch_user.h>
#include <utamo/user_vm.h>
#include <utamo/interrupts.h>
#define UTAMO_PROCESS_LIMIT 16u
#define UTAMO_PROCESS_NAME_SIZE 24u
#define UTAMO_PROCESS_HISTORY 64u
enum process_state {UTAMO_PROCESS_READY, UTAMO_PROCESS_EXITED};
/* Single thread and exclusive address-space owner. Kernel-internal only. */
struct process {
    struct user_vm vm;
    uint64_t pid, tid, syscalls;
    enum process_state state;
    char name[UTAMO_PROCESS_NAME_SIZE];
    int64_t exit_code;
    uint64_t fault_vector, fault_error, fault_address;
    bool faulted;
};
struct process_stats {
    uint64_t created, exited, reaped, user_faults, syscalls;
    size_t active;
    enum arch_user_status status;
};
struct process_result {
    uint64_t pid;
    int64_t exit_code;
    uint64_t fault_vector, fault_error, fault_address, syscalls;
    bool faulted;
};
bool process_init(void);
bool process_available(void);
bool process_spawn_probe(unsigned int probe, uint64_t *out_pid);
bool process_get_stats(struct process_stats *out);
bool process_get_result(uint64_t pid, struct process_result *out);
bool process_selftest(void);
/* Read-only diagnostic snapshot of a live private user mapping. */
bool process_query(uint64_t pid, uint64_t virt, struct vmm_mapping *out);
/* Scheduler/exception/syscall internals. IF=0, trusted PCB/frame only. */
void process_record_syscall(struct process *process);
void process_mark_exit(struct process *process, int64_t code, bool faulted,
                       uint64_t vector, uint64_t error, uint64_t address);
void process_reap(struct process *process);
struct interrupt_frame *process_on_fault(struct interrupt_frame *frame,
                                        uint64_t vector, uint64_t error,
                                        uint64_t address);
struct interrupt_frame *process_on_syscall(struct interrupt_frame *frame);
/* Pure return-state policy. Does not dereference RIP or RSP. */
bool process_user_return_valid(const struct interrupt_frame *frame);
#endif
