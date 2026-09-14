/* SPDX-License-Identifier: MIT */
/* Real exceptions.c, linked normally to host-only mocks. Fatal policy retains
 * static state forever, so each fatal fixture runs in a fresh forked child.
 * POSIX is confined to this Linux/WSL host test; no kernel dependency is added.
 * cpu_halt uses setjmp/longjmp inside each child, never privileged instructions.
 */
#include <utamo/interrupts.h>
#include <utamo/cpu.h>
#include <utamo/scheduler.h>
#include <utamo/process.h>
#include <utamo/irq.h>
#include <utamo/pic.h>
#include <utamo/idt.h>
#include <utamo/serial.h>
#include <utamo/terminal.h>
#include <utamo/vmm.h>
#include <utamo/gdt.h>
#include <utamo/string.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

static unsigned int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    (void)printf("FAIL line %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)

struct fixture {
    char events[128];
    size_t event_count;
    bool interrupts, safe, terminal_ok, query_ok, recursive_query, recursive_terminal;
    unsigned int irq_calls, safe_calls, schedule_calls, syscall_calls, fault_calls;
    unsigned int cr2_calls, serial_formats, terminal_formats, memory_formats;
    unsigned int query_calls, terminal_calls, halts, recursive_messages;
    uint8_t irq;
    uint64_t fault_vector, fault_error, fault_address;
    const struct interrupt_frame *frame;
    struct vmm_mapping mapping;
};
static struct fixture fixture;
static struct interrupt_frame scheduled, syscall_result, fault_result;
static struct framebuffer framebuffer;
static struct terminal terminal;
static jmp_buf halt_target;
static bool allow_halt;
static const uint64_t fault_address = UINT64_C(0xffffc000deadb000);
uint64_t exception_read_cr2(void);

static void event(char value)
{
    CHECK(!fixture.interrupts);
    if (fixture.event_count + 1u >= sizeof(fixture.events)) {
        CHECK(false);
        return;
    }
    fixture.events[fixture.event_count++] = value;
    fixture.events[fixture.event_count] = '\0';
}

void cpu_disable_interrupts(void)
{
    fixture.interrupts = false;
    event('D');
}

_Noreturn void cpu_halt(void)
{
    event('H');
    ++fixture.halts;
    if (allow_halt) {
        longjmp(halt_target, 1);
    }
    (void)printf("UNEXPECTED HALT\n");
    exit(2);
}

void irq_dispatch(uint8_t irq)
{
    event('I');
    ++fixture.irq_calls;
    fixture.irq = irq;
}

struct interrupt_frame *scheduler_on_interrupt(struct interrupt_frame *frame)
{
    event('S');
    CHECK(frame == fixture.frame);
    ++fixture.schedule_calls;
    return &scheduled;
}

bool scheduler_user_frame_safe(const struct interrupt_frame *frame)
{
    event('V');
    CHECK(frame == fixture.frame);
    ++fixture.safe_calls;
    return fixture.safe;
}

struct interrupt_frame *process_on_syscall(struct interrupt_frame *frame)
{
    event('C');
    CHECK(frame == fixture.frame);
    ++fixture.syscall_calls;
    return &syscall_result;
}

struct interrupt_frame *process_on_fault(struct interrupt_frame *frame,
                                        uint64_t vector, uint64_t error,
                                        uint64_t address)
{
    event('F');
    CHECK(frame == fixture.frame);
    ++fixture.fault_calls;
    fixture.fault_vector = vector;
    fixture.fault_error = error;
    fixture.fault_address = address;
    return &fault_result;
}

uint64_t exception_read_cr2(void)
{
    event('R');
    ++fixture.cr2_calls;
    return fault_address;
}

void serial_sink(char value, void *context)
{
    CHECK(value == 'x' && context == NULL);
}

void terminal_sink(char value, void *context)
{
    CHECK(value == 'x' && context == &terminal);
}

bool serial_write_string(const char *text)
{
    event('W');
    CHECK(strcmp(text, "\nUTAMO: recursive CPU exception; halted\n") == 0);
    ++fixture.recursive_messages;
    return true;
}

void exception_format(format_emit_fn emit, void *context,
                       const struct interrupt_frame *frame, uint64_t cr2)
{
    CHECK(frame == fixture.frame);
    CHECK(cr2 == (frame->vector == 14u ? fault_address : 0u));
    if (emit == serial_sink) {
        event('E');
        CHECK(context == NULL);
        ++fixture.serial_formats;
    } else {
        event('B');
        CHECK(emit == terminal_sink && context == &terminal);
        CHECK(fixture.serial_formats == 1u); /* Complete serial dump precedes FB. */
        ++fixture.terminal_formats;
    }
    emit('x', context);
}

void exception_format_memory(format_emit_fn emit, void *context,
                              bool available, const struct vmm_mapping *mapping)
{
    CHECK(fixture.query_calls == 1u && mapping != NULL);
    CHECK(available == fixture.query_ok);
    if (available) {
        CHECK(mapping->mapped && mapping->physical == fixture.mapping.physical &&
              mapping->flags == fixture.mapping.flags &&
              mapping->page_size == fixture.mapping.page_size);
    } else {
        CHECK(!mapping->mapped && mapping->physical == 0u && mapping->flags == 0u);
    }
    if (emit == serial_sink) {
        event('M');
        CHECK(context == NULL && fixture.serial_formats == 1u);
    } else {
        event('N');
        CHECK(emit == terminal_sink && context == &terminal &&
              fixture.terminal_formats == 1u);
    }
    ++fixture.memory_formats;
    emit('x', context);
}

static void recursive_exception(void)
{
    struct interrupt_frame nested = {.cs = UTAMO_GDT_CODE_SELECTOR, .vector = 2u};
    (void)interrupt_dispatch(&nested);
    CHECK(false);
}

bool vmm_query_page(uint64_t virt, struct vmm_mapping *out)
{
    event('Q');
    CHECK(fixture.serial_formats == 1u); /* Query can itself fault: dump first. */
    CHECK(virt == fault_address && out != NULL);
    ++fixture.query_calls;
    if (fixture.recursive_query) {
        recursive_exception();
    }
    if (!fixture.query_ok) {
        return false;
    }
    *out = fixture.mapping;
    return true;
}

bool terminal_init(struct terminal *term, struct framebuffer *fb)
{
    event('T');
    CHECK(term == &terminal && fb == &framebuffer);
    CHECK(fixture.serial_formats == 1u);
    ++fixture.terminal_calls;
    if (fixture.recursive_terminal) {
        recursive_exception();
    }
    return fixture.terminal_ok;
}

static void reset(void)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.interrupts = true;
    fixture.safe = true;
    fixture.terminal_ok = true;
    fixture.query_ok = true;
    fixture.mapping = (struct vmm_mapping){
        .mapped = true, .physical = UINT64_C(0x1dead000),
        .flags = VMM_PRESENT | VMM_WRITABLE | VMM_NX, .page_size = 4096u
    };
    terminal = (struct terminal){.framebuffer = &framebuffer};
    exception_set_terminal(NULL);
    allow_halt = false;
}

static struct interrupt_frame make_frame(uint64_t vector, bool user)
{
    return (struct interrupt_frame){
        .rax = UINT64_C(0xfedcba9876543210),
        .vector = vector, .error_code = UINT64_C(0x123456789),
        .rip = user ? USER_VM_MIN : UINT64_C(0xffffffff80001234),
        .rsp = user ? USER_VM_END - 8u : UINT64_C(0xffffffff8001fff0),
        .cs = user ? UTAMO_GDT_USER_CODE_SELECTOR : UTAMO_GDT_CODE_SELECTOR,
        .ss = user ? UTAMO_GDT_USER_DATA_SELECTOR : UTAMO_GDT_DATA_SELECTOR,
        .rflags = UINT64_C(0x202)
    };
}

static void check_no_fatal(void)
{
    CHECK(fixture.halts == 0u && fixture.serial_formats == 0u &&
          fixture.terminal_formats == 0u && fixture.query_calls == 0u);
    CHECK(!fixture.interrupts);
}

static void test_irqs(void)
{
    for (unsigned int mode = 0u; mode < 2u; ++mode) {
        for (unsigned int irq = 0u; irq < UTAMO_PIC_IRQ_COUNT; ++irq) {
            reset();
            struct interrupt_frame frame =
                make_frame(UTAMO_PIC_VECTOR_BASE + irq, mode != 0u);
            if (mode != 0u) {
                frame.rsp = UINT64_MAX; /* Recent regression: invalid user RSP. */
                fixture.safe = false;
            }
            const struct interrupt_frame before = frame;
            fixture.frame = &frame;
            CHECK(interrupt_dispatch(&frame) == &scheduled);
            CHECK(strcmp(fixture.events, "DIS") == 0);
            CHECK(fixture.irq_calls == 1u && fixture.irq == (uint8_t)irq);
            CHECK(fixture.schedule_calls == 1u && fixture.safe_calls == 0u);
            CHECK(fixture.fault_calls == 0u && fixture.syscall_calls == 0u);
            CHECK(memcmp(&before, &frame, sizeof(frame)) == 0);
            check_no_fatal();
        }
    }
}

static void test_user_routes(void)
{
    const uint64_t unsafe_vectors[] = {0u, 6u, 14u, UTAMO_SYSCALL_VECTOR,
                                       UTAMO_SCHEDULE_VECTOR, 255u};
    for (size_t i = 0u; i < sizeof(unsafe_vectors) / sizeof(unsafe_vectors[0]); ++i) {
        reset();
        struct interrupt_frame frame = make_frame(unsafe_vectors[i], true);
        frame.rsp = UINT64_MAX;
        fixture.safe = false;
        fixture.frame = &frame;
        CHECK(interrupt_dispatch(&frame) == &fault_result);
        CHECK(strcmp(fixture.events, "DVF") == 0);
        CHECK(fixture.fault_calls == 1u && fixture.fault_vector == 13u &&
              fixture.fault_error == 0u && fixture.fault_address == 0u);
        CHECK(fixture.cr2_calls == 0u && fixture.irq_calls == 0u &&
              fixture.schedule_calls == 0u && fixture.syscall_calls == 0u);
        check_no_fatal();
    }
    reset();
    struct interrupt_frame frame = make_frame(UTAMO_SYSCALL_VECTOR, true);
    fixture.frame = &frame;
    CHECK(interrupt_dispatch(&frame) == &syscall_result);
    CHECK(strcmp(fixture.events, "DVC") == 0);
    CHECK(fixture.syscall_calls == 1u && fixture.fault_calls == 0u);
    check_no_fatal();

    reset();
    frame = make_frame(UTAMO_SCHEDULE_VECTOR, false);
    fixture.frame = &frame;
    CHECK(interrupt_dispatch(&frame) == &scheduled);
    CHECK(strcmp(fixture.events, "DS") == 0);
    CHECK(fixture.schedule_calls == 1u && fixture.safe_calls == 0u &&
          fixture.irq_calls == 0u);
    check_no_fatal();

    const uint64_t vectors[] = {0u, 3u, 6u, 13u, 14u, 31u};
    for (size_t i = 0u; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        reset();
        frame = make_frame(vectors[i], true);
        fixture.frame = &frame;
        CHECK(interrupt_dispatch(&frame) == &fault_result);
        CHECK(strcmp(fixture.events, vectors[i] == 14u ? "DVRF" : "DVF") == 0);
        CHECK(fixture.fault_vector == vectors[i] &&
              fixture.fault_error == frame.error_code);
        CHECK(fixture.fault_address == (vectors[i] == 14u ? fault_address : 0u));
        CHECK(fixture.cr2_calls == (vectors[i] == 14u ? 1u : 0u));
        CHECK(fixture.fault_calls == 1u && fixture.syscall_calls == 0u);
        check_no_fatal();
    }
}

static void catch_halt(struct interrupt_frame *frame)
{
    fixture.frame = frame;
    allow_halt = true;
    if (setjmp(halt_target) == 0) {
        (void)interrupt_dispatch(frame);
        CHECK(false);
    }
    allow_halt = false;
    CHECK(!fixture.interrupts);
}

static void fatal_case(unsigned int which)
{
    reset();
    const uint64_t critical[] = {2u, 8u, 18u};
    struct interrupt_frame frame = make_frame(which < 3u ? critical[which] :
                                              (which == 7u || which == 9u ? 6u :
                                               14u), which < 3u);
    if (which < 3u) {
        fixture.safe = false;
        frame.rsp = UINT64_MAX; /* Critical IST policy overrides bad user state. */
    }
    if (which == 4u || which == 5u || which == 6u || which == 7u || which == 9u) {
        exception_set_terminal(&terminal);
    }
    fixture.terminal_ok = which != 5u;
    fixture.query_ok = which != 6u;
    fixture.recursive_query = which == 8u;
    fixture.recursive_terminal = which == 9u;
    catch_halt(&frame);
    static const char *const sequences[] = {
        "DEH", "DEH", "DEH", "DREQMH", "DREQMTBNH", "DREQMTH",
        "DREQMTBNH", "DETBH", "DREQDWH", "DETDWH"
    };
    CHECK(strcmp(fixture.events, sequences[which]) == 0);
    CHECK(fixture.halts == 1u && fixture.serial_formats == 1u);
    CHECK(fixture.safe_calls == 0u && fixture.fault_calls == 0u &&
          fixture.syscall_calls == 0u && fixture.schedule_calls == 0u &&
          fixture.irq_calls == 0u);
    CHECK(fixture.cr2_calls == (frame.vector == 14u ? 1u : 0u));
    if (which < 3u) {
        CHECK(fixture.query_calls == 0u && fixture.memory_formats == 0u);
    }
    if (which == 8u || which == 9u) {
        CHECK(fixture.recursive_messages == 1u && fixture.terminal_formats == 0u);
    }
    if (which == 0u) {
        /* exception_active survives halt; one emergency message is emitted,
         * then recursion suppresses repeated output and halts immediately. */
        fixture.events[0] = '\0';
        fixture.event_count = 0u;
        catch_halt(&frame);
        CHECK(strcmp(fixture.events, "DWH") == 0);
        CHECK(fixture.recursive_messages == 1u && fixture.serial_formats == 1u);
        fixture.events[0] = '\0';
        fixture.event_count = 0u;
        catch_halt(&frame);
        CHECK(strcmp(fixture.events, "DH") == 0);
        CHECK(fixture.recursive_messages == 1u && fixture.halts == 3u);
    }
}

struct child_report { unsigned int checks, failures; };

static bool transfer(int descriptor, void *bytes, size_t size, bool writing)
{
    unsigned char *cursor = bytes;
    while (size != 0u) {
        const ssize_t result = writing ? write(descriptor, cursor, size) :
                                         read(descriptor, cursor, size);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return false;
        }
        cursor += (size_t)result;
        size -= (size_t)result;
    }
    return true;
}

static void run_fatal_child(unsigned int which)
{
    int descriptors[2];
    const int created = pipe(descriptors);
    CHECK(created == 0);
    if (created != 0) {
        return;
    }
    (void)fflush(NULL);
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child < 0) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return;
    }
    if (child == 0) {
        (void)close(descriptors[0]);
        checks = 0u;
        failures = 0u;
        fatal_case(which);
        struct child_report report = {.checks = checks, .failures = failures};
        const bool sent = transfer(descriptors[1], &report, sizeof(report), true);
        (void)close(descriptors[1]);
        (void)fflush(NULL);
        _exit(sent ? 0 : 3);
    }
    (void)close(descriptors[1]);
    struct child_report report = {0};
    const bool received = transfer(descriptors[0], &report, sizeof(report), false);
    (void)close(descriptors[0]);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    CHECK(received);
    CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (received) {
        checks += report.checks;
        failures += report.failures;
    }
}

int main(void)
{
    test_irqs();
    test_user_routes();
    for (unsigned int which = 0u; which < 10u; ++which) {
        run_fatal_child(which);
    }
    (void)printf("UTAMO interrupt dispatcher host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
