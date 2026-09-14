/* SPDX-License-Identifier: MIT */
/* Real native syscall handler and return-state policy, with host mocks only.
 * Runtime interrupt_dispatch first verifies protected frame ownership and
 * process_user_return_valid. These unit tests apply that policy explicitly;
 * they do not claim to execute INT80, IRETQ, CR3 or the runtime dispatcher.
 */
#include <utamo/process.h>
#include <utamo/scheduler.h>
#include <utamo/syscall_abi.h>
#include <utamo/gdt.h>
#include <utamo/memory.h>
#include <utamo/log.h>
#include <utamo/panic.h>
#include <utamo/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#define MODEL_USER_ADDRESS UINT64_C(0x400123)

static unsigned int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

struct model {
    struct process process;
    bool missing_process, fail_copy;
    unsigned int recorded, copied, logged, resumed, yielded, exited, marked;
    uint64_t source, sleep_ms;
    size_t copied_bytes, logged_bytes;
    struct interrupt_frame *routed_frame;
    unsigned char payload[UTAMO_SYS_WRITE_LIMIT];
    unsigned char output[UTAMO_SYS_WRITE_LIMIT];
};
static struct model fixture;
static struct interrupt_frame resume_result, yield_result, exit_result;
static jmp_buf panic_target;
static bool expect_panic;
static unsigned int panic_count;

struct process *scheduler_current_process(void)
{
    return fixture.missing_process ? NULL : &fixture.process;
}

void process_record_syscall(struct process *process)
{
    if (process != &fixture.process || process->state != UTAMO_PROCESS_READY) {
        PANIC("Mock observed syscall without a live process");
    }
    ++fixture.recorded;
    ++process->syscalls;
}

void process_mark_exit(struct process *process, int64_t code, bool faulted,
                       uint64_t vector, uint64_t error, uint64_t address)
{
    CHECK(process == &fixture.process);
    CHECK(process->state == UTAMO_PROCESS_READY);
    ++fixture.marked;
    process->state = UTAMO_PROCESS_EXITED;
    process->exit_code = code;
    process->faulted = faulted;
    process->fault_vector = vector;
    process->fault_error = error;
    process->fault_address = address;
}

bool user_vm_copy_from(const struct user_vm *vm, void *destination,
                       uint64_t source, size_t bytes)
{
    CHECK(vm == &fixture.process.vm);
    CHECK(bytes <= UTAMO_SYS_WRITE_LIMIT);
    ++fixture.copied;
    fixture.source = source;
    fixture.copied_bytes = bytes;
    if (bytes == 0u) {
        return true;
    }
    CHECK(destination != NULL);
    if (fixture.fail_copy || source != MODEL_USER_ADDRESS) {
        /* Even a failed copy that touches the private temporary buffer must
         * never cause those bytes to be sent to the log. */
        memset(destination, 0xee, bytes);
        return false;
    }
    memcpy(destination, fixture.payload, bytes);
    return true;
}

void log_write(const char *bytes, size_t length)
{
    CHECK(length <= UTAMO_SYS_WRITE_LIMIT);
    ++fixture.logged;
    fixture.logged_bytes = length;
    memcpy(fixture.output, bytes, length);
}

struct interrupt_frame *scheduler_resume_user(struct interrupt_frame *frame)
{
    ++fixture.resumed;
    fixture.routed_frame = frame;
    return &resume_result;
}

struct interrupt_frame *scheduler_yield_user(struct interrupt_frame *frame,
                                            uint64_t milliseconds)
{
    ++fixture.yielded;
    fixture.routed_frame = frame;
    fixture.sleep_ms = milliseconds;
    return &yield_result;
}

struct interrupt_frame *scheduler_exit_user(struct interrupt_frame *frame)
{
    ++fixture.exited;
    fixture.routed_frame = frame;
    return &exit_result;
}

_Noreturn void kernel_panic(const char *message, const char *file,
                            unsigned int line)
{
    ++panic_count;
    if (expect_panic) {
        longjmp(panic_target, 1);
    }
    (void)printf("UNEXPECTED PANIC: %s (%s:%u)\n", message, file, line);
    exit(2);
}

static void reset(void)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.process.pid = UINT64_C(0xfedcba9876543210);
    fixture.process.state = UTAMO_PROCESS_READY;
    fixture.process.vm.initialized = true;
    for (size_t i = 0u; i < sizeof(fixture.payload); ++i) {
        fixture.payload[i] = (unsigned char)((i * 37u + 19u) & 255u);
    }
    expect_panic = false;
}

static struct interrupt_frame make_frame(uint64_t syscall)
{
    return (struct interrupt_frame){
        .r15 = UINT64_C(0x1515151515151515),
        .r14 = UINT64_C(0x1414141414141414),
        .r13 = UINT64_C(0x1313131313131313),
        .r12 = UINT64_C(0x1212121212121212),
        .r11 = UINT64_C(0x1111111111111111),
        .r10 = UINT64_C(0x1010101010101010),
        .r9 = UINT64_C(0x0909090909090909),
        .r8 = UINT64_C(0x0808080808080808),
        .rbp = UINT64_C(0xbabababababababa),
        .rdi = UINT64_C(0xd1d1d1d1d1d1d1d1),
        .rsi = UINT64_C(0x5151515151515151),
        .rdx = UINT64_C(0xd2d2d2d2d2d2d2d2),
        .rcx = UINT64_C(0xc1c1c1c1c1c1c1c1),
        .rbx = UINT64_C(0xb1b1b1b1b1b1b1b1),
        .rax = syscall, .vector = 128u, .error_code = 0u,
        .rip = UINT64_C(0x400080),
        .cs = UTAMO_GDT_USER_CODE_SELECTOR,
        .rflags = UINT64_C(0x202) | (UINT64_C(1) << 8u) |
                  (UINT64_C(1) << 10u),
        .rsp = UINT64_C(0x7fffffffeff8),
        .ss = UTAMO_GDT_USER_DATA_SELECTOR
    };
}

static void check_frame(const struct interrupt_frame *before,
                         const struct interrupt_frame *after, uint64_t rax)
{
    struct interrupt_frame expected = *before;
    expected.rax = rax;
    /* Every argument, non-result GPR, vector and hardware return slot survives. */
    CHECK(memcmp(&expected, after, sizeof(expected)) == 0);
    CHECK(fixture.routed_frame == after);
    CHECK(fixture.recorded == 1u && fixture.process.syscalls == 1u);
}

static struct interrupt_frame *dispatch_valid(struct interrupt_frame *frame)
{
    CHECK(process_user_return_valid(frame));
    return process_on_syscall(frame);
}

static void test_write_success(void)
{
    const size_t lengths[] = {0u, 1u, 17u, UTAMO_SYS_WRITE_LIMIT};
    for (uint64_t descriptor = 1u; descriptor <= 2u; ++descriptor) {
        for (size_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            reset();
            const unsigned char special[] = {'A', 0u, '%', 'n', '%', 's', '%',
                                              'p', '\n', '%', '%', 0xffu};
            memcpy(fixture.payload, special, sizeof(special));
            struct interrupt_frame frame = make_frame(UTAMO_SYS_WRITE);
            frame.rdi = descriptor;
            frame.rsi = lengths[i] == 0u ? UINT64_MAX : MODEL_USER_ADDRESS;
            frame.rdx = (uint64_t)lengths[i];
            const struct interrupt_frame before = frame;
            CHECK(dispatch_valid(&frame) == &resume_result);
            check_frame(&before, &frame, (uint64_t)lengths[i]);
            CHECK(fixture.copied == 1u && fixture.copied_bytes == lengths[i]);
            CHECK(fixture.source == before.rsi);
            CHECK(fixture.logged == 1u && fixture.logged_bytes == lengths[i]);
            CHECK(memcmp(fixture.output, fixture.payload, lengths[i]) == 0);
            CHECK(fixture.resumed == 1u && fixture.yielded == 0u &&
                  fixture.exited == 0u && fixture.marked == 0u);
        }
    }
}

static void test_write_errors(void)
{
    const uint64_t descriptors[] = {0u, 3u, UINT64_MAX};
    for (size_t i = 0u; i < sizeof(descriptors) / sizeof(descriptors[0]); ++i) {
        reset();
        struct interrupt_frame frame = make_frame(UTAMO_SYS_WRITE);
        frame.rdi = descriptors[i];
        frame.rsi = MODEL_USER_ADDRESS;
        frame.rdx = 8u;
        const struct interrupt_frame before = frame;
        CHECK(dispatch_valid(&frame) == &resume_result);
        check_frame(&before, &frame, (uint64_t)(int64_t)UTAMO_SYS_EBADF);
        CHECK(fixture.copied == 0u && fixture.logged == 0u);
        CHECK(fixture.resumed == 1u && fixture.yielded == 0u && fixture.exited == 0u);
    }
    const uint64_t lengths[] = {UTAMO_SYS_WRITE_LIMIT + 1u,
                                UINT64_C(0x100000000), UINT64_MAX};
    for (size_t i = 0u; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        reset();
        struct interrupt_frame frame = make_frame(UTAMO_SYS_WRITE);
        frame.rdi = 1u;
        frame.rsi = MODEL_USER_ADDRESS;
        frame.rdx = lengths[i];
        const struct interrupt_frame before = frame;
        CHECK(dispatch_valid(&frame) == &resume_result);
        check_frame(&before, &frame, (uint64_t)(int64_t)UTAMO_SYS_EINVAL);
        CHECK(fixture.copied == 0u && fixture.logged == 0u);
    }
    const uint64_t sources[] = {0u, USER_VM_END, UINT64_MAX, MODEL_USER_ADDRESS};
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        reset();
        fixture.fail_copy = true;
        struct interrupt_frame frame = make_frame(UTAMO_SYS_WRITE);
        frame.rdi = 2u;
        frame.rsi = sources[i];
        frame.rdx = 16u;
        const struct interrupt_frame before = frame;
        CHECK(dispatch_valid(&frame) == &resume_result);
        check_frame(&before, &frame, (uint64_t)(int64_t)UTAMO_SYS_EFAULT);
        CHECK(fixture.copied == 1u && fixture.copied_bytes == 16u);
        CHECK(fixture.logged == 0u);
        CHECK(fixture.output[0] == 0u && fixture.output[15] == 0u);
    }
}

static void test_exit(void)
{
    const int64_t codes[] = {0, 1, -1, INT64_MIN, INT64_MAX,
                             INT64_C(0x123456789abcdef)};
    for (size_t i = 0u; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        reset();
        struct interrupt_frame frame = make_frame(UTAMO_SYS_EXIT);
        frame.rdi = (uint64_t)codes[i];
        const struct interrupt_frame before = frame;
        CHECK(dispatch_valid(&frame) == &exit_result);
        check_frame(&before, &frame, UTAMO_SYS_EXIT);
        CHECK(fixture.marked == 1u && fixture.process.state == UTAMO_PROCESS_EXITED);
        CHECK(fixture.process.exit_code == codes[i]);
        CHECK(!fixture.process.faulted && fixture.process.fault_vector == 0u &&
              fixture.process.fault_error == 0u && fixture.process.fault_address == 0u);
        CHECK(fixture.exited == 1u && fixture.resumed == 0u && fixture.yielded == 0u);
        CHECK(fixture.copied == 0u && fixture.logged == 0u);
    }
}

static void test_pid_yield_sleep_unknown(void)
{
    reset();
    struct interrupt_frame frame = make_frame(UTAMO_SYS_GETPID);
    struct interrupt_frame before = frame;
    CHECK(dispatch_valid(&frame) == &resume_result);
    check_frame(&before, &frame, fixture.process.pid);
    CHECK(fixture.copied == 0u && fixture.logged == 0u && fixture.resumed == 1u);

    reset();
    frame = make_frame(UTAMO_SYS_YIELD);
    frame.rdi = UINT64_MAX; /* Yield does not interpret argument registers. */
    before = frame;
    CHECK(dispatch_valid(&frame) == &yield_result);
    check_frame(&before, &frame, 0u);
    CHECK(fixture.sleep_ms == 0u && fixture.yielded == 1u);
    CHECK(fixture.resumed == 0u && fixture.exited == 0u);
    CHECK(fixture.copied == 0u && fixture.logged == 0u);

    const uint64_t delays[] = {0u, 1u, 20u, UINT64_C(0x100000000), UINT64_MAX};
    for (size_t i = 0u; i < sizeof(delays) / sizeof(delays[0]); ++i) {
        reset();
        frame = make_frame(UTAMO_SYS_SLEEP);
        frame.rdi = delays[i];
        before = frame;
        CHECK(dispatch_valid(&frame) == &yield_result);
        check_frame(&before, &frame, 0u);
        CHECK(fixture.sleep_ms == delays[i] && fixture.yielded == 1u);
        CHECK(fixture.resumed == 0u && fixture.exited == 0u);
        CHECK(fixture.copied == 0u && fixture.logged == 0u);
    }
    const uint64_t unknown[] = {0u, 14u, UINT64_C(0x100000001), UINT64_MAX};
    for (size_t i = 0u; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
        reset();
        frame = make_frame(unknown[i]);
        before = frame;
        CHECK(dispatch_valid(&frame) == &resume_result);
        check_frame(&before, &frame, (uint64_t)(int64_t)UTAMO_SYS_ENOSYS);
        CHECK(fixture.resumed == 1u && fixture.yielded == 0u && fixture.exited == 0u);
        CHECK(fixture.copied == 0u && fixture.logged == 0u);
    }
}

static void check_policy_only(const struct interrupt_frame *frame, bool expected)
{
    const unsigned int copied = fixture.copied;
    const unsigned int logged = fixture.logged;
    const unsigned int recorded = fixture.recorded;
    struct interrupt_frame original = {0};
    if (frame != NULL) {
        original = *frame;
    }
    CHECK(process_user_return_valid(frame) == expected);
    CHECK(fixture.copied == copied && fixture.logged == logged &&
          fixture.recorded == recorded);
    if (frame != NULL) {
        CHECK(memcmp(&original, frame, sizeof(original)) == 0);
    }
}

static void test_return_policy(void)
{
    reset();
    struct interrupt_frame frame = make_frame(UTAMO_SYS_GETPID);
    check_policy_only(NULL, false);
    check_policy_only(&frame, true);
    const uint64_t valid[] = {USER_VM_MIN, USER_VM_MIN + 1u,
                              USER_VM_END - MEMORY_PAGE_SIZE, USER_VM_END - 1u};
    for (size_t i = 0u; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        frame = make_frame(UTAMO_SYS_GETPID);
        frame.rip = valid[i]; /* These host addresses are never dereferenced. */
        frame.rsp = valid[sizeof(valid) / sizeof(valid[0]) - 1u - i];
        check_policy_only(&frame, true);
    }
    const uint64_t invalid[] = {0u, USER_VM_MIN - 1u, USER_VM_END,
                                UINT64_C(0xffff7fffffffffff),
                                UINT64_C(0xffff800000000000), UINT64_MAX};
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        frame = make_frame(UTAMO_SYS_WRITE);
        frame.rip = invalid[i];
        check_policy_only(&frame, false);
        frame = make_frame(UTAMO_SYS_WRITE);
        frame.rsp = invalid[i];
        check_policy_only(&frame, false);
    }
    const uint64_t bad_cs[] = {0u, UTAMO_GDT_CODE_SELECTOR,
                               UTAMO_GDT_USER_CODE_SELECTOR & ~UINT64_C(3),
                               UTAMO_GDT_USER_DATA_SELECTOR,
                               UTAMO_GDT_USER_CODE_SELECTOR | UINT64_C(0x10000)};
    for (size_t i = 0u; i < sizeof(bad_cs) / sizeof(bad_cs[0]); ++i) {
        frame = make_frame(UTAMO_SYS_WRITE);
        frame.cs = bad_cs[i];
        check_policy_only(&frame, false);
    }
    const uint64_t bad_ss[] = {0u, UTAMO_GDT_DATA_SELECTOR,
                               UTAMO_GDT_USER_DATA_SELECTOR & ~UINT64_C(3),
                               UTAMO_GDT_USER_CODE_SELECTOR,
                               UTAMO_GDT_USER_DATA_SELECTOR | UINT64_C(0x10000)};
    for (size_t i = 0u; i < sizeof(bad_ss) / sizeof(bad_ss[0]); ++i) {
        frame = make_frame(UTAMO_SYS_WRITE);
        frame.ss = bad_ss[i];
        check_policy_only(&frame, false);
    }
    const unsigned int forbidden[] = {12u, 13u, 14u, 17u, 19u, 20u};
    for (size_t i = 0u; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        frame = make_frame(UTAMO_SYS_GETPID);
        frame.rflags |= UINT64_C(1) << forbidden[i];
        check_policy_only(&frame, false);
    }
    frame = make_frame(UTAMO_SYS_GETPID);
    frame.rflags &= ~UINT64_C(0x200);
    check_policy_only(&frame, false);
    frame = make_frame(UTAMO_SYS_GETPID);
    frame.rflags &= ~UINT64_C(2);
    check_policy_only(&frame, false);
    const unsigned int allowed[] = {8u, 10u, 18u}; /* TF, DF and AC. */
    for (size_t i = 0u; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        frame = make_frame(UTAMO_SYS_GETPID);
        frame.rflags = UINT64_C(0x202) | (UINT64_C(1) << allowed[i]);
        check_policy_only(&frame, true);
    }
    CHECK(fixture.copied == 0u && fixture.logged == 0u && fixture.recorded == 0u);
}

static void check_handler_precondition(unsigned int kind)
{
    reset();
    struct interrupt_frame frame = make_frame(UTAMO_SYS_GETPID);
    if (kind == 0u) {
        fixture.missing_process = true;
    } else if (kind == 1u) {
        frame.cs = UTAMO_GDT_CODE_SELECTOR;
    } else {
        fixture.process.state = UTAMO_PROCESS_EXITED;
    }
    const unsigned int before = panic_count;
    expect_panic = true;
    if (setjmp(panic_target) == 0) {
        (void)process_on_syscall(&frame);
        CHECK(false);
    }
    expect_panic = false;
    CHECK(panic_count == before + 1u);
    CHECK(fixture.recorded == 0u && fixture.copied == 0u && fixture.logged == 0u);
    CHECK(fixture.resumed == 0u && fixture.yielded == 0u && fixture.exited == 0u);
}

static void test_handler_preconditions(void)
{
    /* These fatal checks supplement the dispatcher contract; do not pass an
     * unowned/NULL frame into a function whose argument is trusted by design.
     * Each setjmp scope is isolated from the driver's iteration variable. */
    for (unsigned int kind = 0u; kind < 3u; ++kind) {
        check_handler_precondition(kind);
    }
}

int main(void)
{
    test_write_success();
    test_write_errors();
    test_exit();
    test_pid_yield_sleep_unknown();
    test_return_policy();
    test_handler_preconditions();
    (void)printf("UTAMO process syscall host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}

/* Extension dispatch is exercised against real VFS in test_process_files.c. */
int64_t process_file_syscall(struct process *process, uint64_t number,
                             uint64_t a, uint64_t b, uint64_t c)
{
    (void)process; (void)number; (void)a; (void)b; (void)c;
    return UTAMO_SYS_ENOSYS;
}
