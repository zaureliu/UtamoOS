# UTAMO OS. Run only in the personal Linux/WSL development environment.
# The default target builds the kernel; it does not fetch tools or run a VM.
SHELL := /bin/sh
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SUFFIXES:

ifneq ($(words $(CURDIR)),1)
$(error Use a project path without whitespace for this GNU Make build)
endif
ifneq ($(abspath $(CURDIR)),$(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
$(error Run make from the project root, or use make -C /path/to/UtamoOS)
endif

CROSS_COMPILE ?= x86_64-elf-
KERNEL_CC := $(CROSS_COMPILE)gcc
READELF := $(CROSS_COMPILE)readelf
NM := $(CROSS_COMPILE)nm
NASM ?= nasm
HOST_CC ?= cc
QEMU ?= qemu-system-x86_64
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd

# Generated files have a fixed project-local destination.
override BUILD_DIR := build
override KERNEL := $(BUILD_DIR)/utamo-kernel.elf
UTAMO_VERSION := $(shell awk '$$2 == "UTAMO_VERSION" { gsub(/"/, "", $$3); print $$3 }' kernel/include/utamo/version.h)
override ISO := $(BUILD_DIR)/utamo-os-$(UTAMO_VERSION).iso
LINKER_SCRIPT := kernel/arch/x86_64/linker.ld

KERNEL_CPPFLAGS := -Ikernel/include -Ithird_party/limine
KERNEL_CFLAGS := -std=c17 -O2 -g -gdwarf-4 \
    -ffreestanding -fno-builtin -fno-common \
    -fno-stack-protector -fno-pic -fno-pie \
    -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -fno-unwind-tables \
    -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections \
    -m64 -march=x86-64 -mno-red-zone -mcmodel=kernel -mgeneral-regs-only \
    -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion \
    -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wvla
KERNEL_LDFLAGS := -nostdlib -static -no-pie -m64 -mcmodel=kernel \
    -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map,$(BUILD_DIR)/utamo-kernel.map \
    -Wl,--build-id=none -Wl,--gc-sections -Wl,--orphan-handling=error \
    -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack
NASMFLAGS := -f elf64 -g -F dwarf -Wall -Werror -Wno-error=reloc-rel-dword
HOST_CFLAGS := -std=c17 -O2 -g -fno-builtin -fno-tree-loop-distribute-patterns \
    -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion \
    -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wvla

C_SOURCES := $(sort $(shell find kernel -type f -name '*.c'))
ASM_SOURCES := $(sort $(shell find kernel/arch/x86_64 -type f -name '*.asm'))
C_OBJECTS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(C_SOURCES)))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(ASM_SOURCES)))
OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)
HOST_SOURCES := tests/test_main.c kernel/lib/string.c kernel/lib/format.c \
    kernel/memory/memory_map.c
HOST_HEADERS := $(sort $(shell find kernel/include -type f -name '*.h'))
HOST_TEST := $(BUILD_DIR)/tests/utamo-host-tests
VIDEO_TEST_SOURCES := tests/test_video.c kernel/drivers/video/framebuffer.c \
    kernel/drivers/video/terminal.c kernel/drivers/video/font.c
VIDEO_TEST := $(BUILD_DIR)/tests/utamo-video-tests

QEMU_FLAGS := -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1 \
    -display none -serial stdio -monitor none -nic none -no-reboot -no-shutdown -boot d

.PHONY: all kernel iso run debug run-uefi test-host inspect clean guard-build

all: kernel

kernel: $(KERNEL)

# Refuse symlinks in the output tree before any build writes. Do not run two
# independent make invocations against the same build directory concurrently.
guard-build:
	@test ! -L "$(BUILD_DIR)" || { echo 'Refusing symlink: build' >&2; exit 1; }
	@mkdir -p -- "$(BUILD_DIR)"
	@test "$$(cd "$(BUILD_DIR)" && pwd -P)" = "$$(pwd -P)/build" || { echo 'Invalid build path' >&2; exit 1; }
	@build_links=$$(find "$(BUILD_DIR)" -type l -print -quit) || exit 1; \
	    test -z "$$build_links" || { echo 'Refusing symlink inside build' >&2; exit 1; }

$(BUILD_DIR)/%.c.o: %.c Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(KERNEL_CC) $(KERNEL_CPPFLAGS) $(KERNEL_CFLAGS) -MMD -MP -MF "$(@:.o=.d)" -c "$<" -o "$@"

$(BUILD_DIR)/generated/constants.stamp: scripts/gen-nasm-constants.py kernel/include/utamo/syscall_abi.h kernel/include/utamo/user_probe.h | guard-build
	python3 scripts/gen-nasm-constants.py
	@touch "$@"

$(BUILD_DIR)/kernel/arch/x86_64/user_probe.asm.o: $(BUILD_DIR)/generated/constants.stamp

$(BUILD_DIR)/%.asm.o: %.asm Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(NASM) $(NASMFLAGS) -MD "$(@:.o=.d)" "$<" -o "$@"

$(KERNEL): $(OBJECTS) $(LINKER_SCRIPT) Makefile | guard-build
	$(KERNEL_CC) $(KERNEL_LDFLAGS) $(OBJECTS) -o "$@"

# Always rebuild the ISO: locally supplied Limine assets can change independently.
iso: $(KERNEL) initramfs | guard-build
	bash scripts/make-iso.sh

run: iso
	$(QEMU) $(QEMU_FLAGS) -cdrom "$(ISO)"

debug: iso
	$(QEMU) $(QEMU_FLAGS) -cdrom "$(ISO)" -S -gdb tcp:127.0.0.1:1234

run-uefi: iso
	@test -f "$(OVMF_CODE)" && test -f "$(OVMF_VARS)" || { echo 'Set OVMF_CODE and OVMF_VARS to a matching firmware pair' >&2; exit 1; }
	cp -- "$(OVMF_VARS)" "$(BUILD_DIR)/OVMF_VARS.fd"
	$(QEMU) $(QEMU_FLAGS) -cdrom "$(ISO)" \
	    -drive if=pflash,format=raw,readonly=on,file="$(OVMF_CODE)" \
	    -drive if=pflash,format=raw,file="$(BUILD_DIR)/OVMF_VARS.fd"

$(HOST_TEST): $(HOST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(HOST_SOURCES) -o "$@"

$(VIDEO_TEST): $(VIDEO_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(VIDEO_TEST_SOURCES) -o "$@"

GDT_TEST := $(BUILD_DIR)/tests/utamo-gdt-tests
$(GDT_TEST): tests/test_gdt.c kernel/arch/x86_64/gdt_layout.c $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include tests/test_gdt.c kernel/arch/x86_64/gdt_layout.c -o "$@"

INTERRUPT_TEST := $(BUILD_DIR)/tests/utamo-interrupt-tests
INTERRUPT_TEST_SOURCES := tests/test_interrupts.c kernel/arch/x86_64/idt_layout.c kernel/interrupts/exception_info.c kernel/interrupts/exception_format.c kernel/interrupts/exception_memory_format.c kernel/memory/memory_helpers.c kernel/lib/format.c
$(INTERRUPT_TEST): $(INTERRUPT_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(INTERRUPT_TEST_SOURCES) -o "$@"

PIC_TEST := $(BUILD_DIR)/tests/utamo-pic-tests
PIC_TEST_SOURCES := tests/test_pic.c kernel/arch/x86_64/pic.c
$(PIC_TEST): $(PIC_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(PIC_TEST_SOURCES) -o "$@"

PIT_TEST := $(BUILD_DIR)/tests/utamo-pit-tests
PIT_TEST_SOURCES := tests/test_pit.c kernel/drivers/timer/pit.c kernel/drivers/timer/pit_time.c
$(PIT_TEST): $(PIT_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(PIT_TEST_SOURCES) -o "$@"

INPUT_TEST := $(BUILD_DIR)/tests/utamo-input-tests
INPUT_TEST_SOURCES := tests/test_input_shell.c kernel/input/input.c kernel/lib/shell_line.c kernel/lib/string.c
$(INPUT_TEST): $(INPUT_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(INPUT_TEST_SOURCES) -o "$@"

KEYBOARD_TEST := $(BUILD_DIR)/tests/utamo-keyboard-tests
KEYBOARD_TEST_SOURCES := tests/test_keyboard.c kernel/input/input.c kernel/drivers/input/keyboard.c
$(KEYBOARD_TEST): $(KEYBOARD_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(KEYBOARD_TEST_SOURCES) -o "$@"

SHELL_TEST := $(BUILD_DIR)/tests/utamo-shell-tests
SHELL_TEST_SOURCES := tests/test_shell_commands.c kernel/core/shell.c kernel/lib/shell_line.c kernel/lib/string.c kernel/lib/format.c
$(SHELL_TEST): $(SHELL_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(SHELL_TEST_SOURCES) -o "$@"

PMM_TEST := $(BUILD_DIR)/tests/utamo-pmm-tests
PMM_TEST_SOURCES := tests/test_pmm.c kernel/memory/pmm.c kernel/memory/pmm_core.c kernel/memory/memory_map.c kernel/memory/memory_helpers.c kernel/lib/string.c
$(PMM_TEST): $(PMM_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(PMM_TEST_SOURCES) -o "$@"

VMM_TEST := $(BUILD_DIR)/tests/utamo-vmm-tests
VMM_TEST_SOURCES := tests/test_vmm.c kernel/memory/vmm_core.c kernel/memory/memory_helpers.c kernel/lib/string.c
$(VMM_TEST): $(VMM_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(VMM_TEST_SOURCES) -o "$@"

MEMORY_HELPER_TEST := $(BUILD_DIR)/tests/utamo-memory-helper-tests
MEMORY_HELPER_TEST_SOURCES := tests/test_memory_helpers.c kernel/memory/memory_helpers.c kernel/memory/hhdm.c kernel/memory/memory_map.c
$(MEMORY_HELPER_TEST): $(MEMORY_HELPER_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(MEMORY_HELPER_TEST_SOURCES) -o "$@"

HEAP_TEST := $(BUILD_DIR)/tests/utamo-heap-tests
HEAP_TEST_SOURCES := tests/test_heap.c kernel/memory/heap_core.c kernel/lib/string.c
$(HEAP_TEST): $(HEAP_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(HEAP_TEST_SOURCES) -o "$@"

HEAP_PAGES_TEST := $(BUILD_DIR)/tests/utamo-heap-pages-tests
HEAP_PAGES_TEST_SOURCES := tests/test_heap_pages.c kernel/memory/heap_pages.c kernel/memory/memory_helpers.c
$(HEAP_PAGES_TEST): $(HEAP_PAGES_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(HEAP_PAGES_TEST_SOURCES) -o "$@"

SCHED_TEST := $(BUILD_DIR)/tests/utamo-sched-tests
SCHED_TEST_SOURCES := tests/test_sched_core.c kernel/core/sched_core.c kernel/lib/string.c
$(SCHED_TEST): $(SCHED_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(SCHED_TEST_SOURCES) -o "$@"

THREAD_STACK_TEST := $(BUILD_DIR)/tests/utamo-thread-stack-tests
THREAD_STACK_TEST_SOURCES := tests/test_thread_stack.c kernel/core/thread_stack.c kernel/memory/heap_pages.c kernel/memory/memory_helpers.c kernel/lib/string.c
$(THREAD_STACK_TEST): $(THREAD_STACK_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(THREAD_STACK_TEST_SOURCES) -o "$@"

ARCH_USER_TEST := $(BUILD_DIR)/tests/utamo-arch-user-tests
ARCH_USER_TEST_SOURCES := tests/test_arch_user.c kernel/arch/x86_64/arch_user.c kernel/arch/x86_64/gdt.c kernel/arch/x86_64/gdt_layout.c kernel/arch/x86_64/idt.c kernel/arch/x86_64/idt_layout.c kernel/memory/memory_helpers.c
$(ARCH_USER_TEST): $(ARCH_USER_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(ARCH_USER_TEST_SOURCES) -o "$@"

USER_VM_TEST := $(BUILD_DIR)/tests/utamo-user-vm-tests
USER_VM_TEST_SOURCES := tests/test_user_vm.c kernel/memory/user_vm.c kernel/memory/vmm_core.c kernel/memory/memory_helpers.c kernel/lib/string.c
$(USER_VM_TEST): $(USER_VM_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(USER_VM_TEST_SOURCES) -o "$@"

PROCESS_SYSCALL_TEST := $(BUILD_DIR)/tests/utamo-process-syscall-tests
PROCESS_SYSCALL_TEST_SOURCES := tests/test_process_syscall.c kernel/core/process_syscall.c kernel/core/process_policy.c kernel/memory/memory_helpers.c kernel/lib/string.c
$(PROCESS_SYSCALL_TEST): $(PROCESS_SYSCALL_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(PROCESS_SYSCALL_TEST_SOURCES) -o "$@"

DISPATCH_TEST := $(BUILD_DIR)/tests/utamo-dispatch-tests
DISPATCH_TEST_SOURCES := tests/test_dispatch.c kernel/interrupts/exceptions.c kernel/lib/string.c
$(DISPATCH_TEST): $(DISPATCH_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(DISPATCH_TEST_SOURCES) -o "$@"

VFS_ELF_TEST := $(BUILD_DIR)/tests/utamo-vfs-elf-tests
VFS_ELF_TEST_SOURCES := tests/test_vfs_elf.c kernel/fs/vfs.c kernel/fs/initramfs.c kernel/core/elf.c kernel/lib/string.c
$(VFS_ELF_TEST): $(VFS_ELF_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(VFS_ELF_TEST_SOURCES) -o "$@"

ELF_LOAD_TEST := $(BUILD_DIR)/tests/utamo-elf-load-tests
ELF_LOAD_TEST_SOURCES := tests/test_elf_load.c kernel/core/elf.c kernel/core/elf_load.c kernel/lib/string.c
$(ELF_LOAD_TEST): $(ELF_LOAD_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(ELF_LOAD_TEST_SOURCES) -o "$@"

PROCESS_FILES_TEST := $(BUILD_DIR)/tests/utamo-process-files-tests
PROCESS_FILES_TEST_SOURCES := tests/test_process_files.c kernel/core/process_files.c kernel/fs/vfs.c kernel/lib/string.c
$(PROCESS_FILES_TEST): $(PROCESS_FILES_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(PROCESS_FILES_TEST_SOURCES) -o "$@"

PROCESS_EXEC_TEST := $(BUILD_DIR)/tests/utamo-process-exec-tests
PROCESS_EXEC_TEST_SOURCES := tests/test_process_exec.c kernel/core/process.c kernel/lib/string.c
$(PROCESS_EXEC_TEST): $(PROCESS_EXEC_TEST_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -Ikernel/include $(PROCESS_EXEC_TEST_SOURCES) -o "$@"

STORAGE_CORE_TEST := $(BUILD_DIR)/tests/utamo-storage-core-tests
STORAGE_CORE_SOURCES := tests/test_storage_core.c kernel/drivers/pci_core.c kernel/drivers/pci.c kernel/drivers/block.c kernel/drivers/ahci_core.c kernel/memory/mmio_policy.c kernel/lib/string.c
$(STORAGE_CORE_TEST): $(STORAGE_CORE_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(STORAGE_CORE_SOURCES) -o "$@"

FAT32_TEST := $(BUILD_DIR)/tests/utamo-fat32-tests
FAT32_SOURCES := tests/test_fat32.c kernel/fs/fat32.c kernel/fs/vfs.c kernel/drivers/block.c kernel/lib/string.c
$(BUILD_DIR)/tests/fat32-fixtures.json: scripts/make-test-disk.py | guard-build
	python3 scripts/make-test-disk.py
$(FAT32_TEST): $(FAT32_SOURCES) $(HOST_HEADERS) Makefile $(BUILD_DIR)/tests/fat32-fixtures.json | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(FAT32_SOURCES) -o "$@"

AHCI_DRIVER_TEST := $(BUILD_DIR)/tests/utamo-ahci-driver-tests
AHCI_DRIVER_SOURCES := tests/test_ahci_driver.c kernel/drivers/ahci.c kernel/drivers/ahci_core.c kernel/drivers/block.c kernel/lib/string.c
$(AHCI_DRIVER_TEST): $(AHCI_DRIVER_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(AHCI_DRIVER_SOURCES) -o "$@"

NETWORK_TEST := $(BUILD_DIR)/tests/utamo-network-tests
NETWORK_SOURCES := tests/test_network.c kernel/net/ethernet.c kernel/net/arp.c kernel/net/ipv4.c kernel/net/icmp.c kernel/net/udp.c kernel/net/dhcp.c kernel/net/dns.c kernel/drivers/e1000_ring.c kernel/lib/string.c
$(NETWORK_TEST): $(NETWORK_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(NETWORK_SOURCES) -o "$@"

E1000_TEST := $(BUILD_DIR)/tests/utamo-e1000-tests
E1000_SOURCES := tests/test_e1000_driver.c kernel/drivers/e1000.c kernel/drivers/e1000_ring.c kernel/lib/string.c
$(E1000_TEST): $(E1000_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(E1000_SOURCES) -o "$@"

NET_STACK_TEST := $(BUILD_DIR)/tests/utamo-net-stack-tests
NET_STACK_SOURCES := tests/test_net_stack.c kernel/net/stack.c kernel/net/ethernet.c kernel/net/arp.c kernel/net/ipv4.c kernel/net/icmp.c kernel/net/udp.c kernel/net/dhcp.c kernel/net/dns.c kernel/lib/string.c
$(NET_STACK_TEST): $(NET_STACK_SOURCES) $(HOST_HEADERS) Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(HOST_CC) $(HOST_CFLAGS) -Ikernel/include $(NET_STACK_SOURCES) -o "$@"

test-host: $(NET_STACK_TEST) $(E1000_TEST) $(NETWORK_TEST) $(AHCI_DRIVER_TEST) $(STORAGE_CORE_TEST) $(FAT32_TEST) $(PROCESS_EXEC_TEST) $(PROCESS_FILES_TEST) $(ELF_LOAD_TEST) $(VFS_ELF_TEST) $(HOST_TEST) $(VIDEO_TEST) $(GDT_TEST) $(INTERRUPT_TEST) $(PIC_TEST) $(PIT_TEST) $(INPUT_TEST) $(KEYBOARD_TEST) $(SHELL_TEST) $(PMM_TEST) $(VMM_TEST) $(MEMORY_HELPER_TEST) $(HEAP_TEST) $(HEAP_PAGES_TEST) $(SCHED_TEST) $(THREAD_STACK_TEST) $(ARCH_USER_TEST) $(USER_VM_TEST) $(PROCESS_SYSCALL_TEST) $(DISPATCH_TEST)
	"./$(NETWORK_TEST)"
	"./$(NET_STACK_TEST)"
	@for scenario in good reset-timeout mac-invalid map-fail enable-fail tx-timeout tx-error link-down; do "./$(E1000_TEST)" "$$scenario" || exit $$?; done
	@for scenario in good absent alloc-fail map-fail enable-fail identify-fail stop-timeout timeout task-error bus-error short-dma; do "./$(AHCI_DRIVER_TEST)" "$$scenario" || exit $$?; done
	"./$(STORAGE_CORE_TEST)"
	"./$(FAT32_TEST)"
	"./$(VFS_ELF_TEST)"
	"./$(ELF_LOAD_TEST)"
	"./$(PROCESS_FILES_TEST)"
	"./$(PROCESS_EXEC_TEST)"
	"./$(HOST_TEST)"
	"./$(VIDEO_TEST)"
	"./$(GDT_TEST)"
	"./$(INTERRUPT_TEST)"
	"./$(PIC_TEST)"
	"./$(PIT_TEST)"
	"./$(INPUT_TEST)"
	"./$(KEYBOARD_TEST)"
	"./$(SHELL_TEST)"
	"./$(PMM_TEST)"
	"./$(VMM_TEST)"
	"./$(MEMORY_HELPER_TEST)"
	"./$(HEAP_TEST)"
	"./$(HEAP_PAGES_TEST)"
	"./$(SCHED_TEST)"
	"./$(THREAD_STACK_TEST)"
	"./$(ARCH_USER_TEST)"
	"./$(USER_VM_TEST)"
	"./$(PROCESS_SYSCALL_TEST)"
	"./$(DISPATCH_TEST)"

inspect: $(KERNEL) userspace
	$(READELF) -h -l -S "$(KERNEL)"
	$(NM) -u "$(KERNEL)"
	python3 scripts/inspect-elf.py "$(KERNEL)"
	python3 scripts/inspect-userspace.py

# Only the literal build directory is removed; sources and vendor remain intact.
clean:
	@test ! -L build || { echo 'Refusing symlink: build' >&2; exit 1; }
	@if test -e build; then \
	    test -d build && test "$$(cd build && pwd -P)" = "$$(pwd -P)/build" || exit 1; \
	    rm -rf -- build; \
	fi

-include $(DEPS)

USER_PROGRAMS := init hello echo sysinfo filetest badptr diskread
USER_BINARIES := $(addprefix $(BUILD_DIR)/userspace/,$(USER_PROGRAMS))
USER_CFLAGS := $(filter-out -mcmodel=kernel,$(KERNEL_CFLAGS)) -mcmodel=small -fdebug-prefix-map=$(CURDIR)=. -ffile-prefix-map=$(CURDIR)=.
USER_COMMON := userspace/libc/runtime.c userspace/include/utamo.h kernel/include/utamo/syscall_abi.h userspace/linker/user.ld
$(BUILD_DIR)/userspace/start.o: userspace/crt/start.asm Makefile | guard-build
	@mkdir -p -- "$(@D)"
	$(NASM) $(NASMFLAGS) "$<" -o "$@"
$(BUILD_DIR)/userspace/%: userspace/bin/%.c $(USER_COMMON) $(BUILD_DIR)/userspace/start.o Makefile | guard-build
	$(KERNEL_CC) $(USER_CFLAGS) -Iuserspace/include -Ikernel/include -nostdlib -static -no-pie -Wl,-T,userspace/linker/user.ld -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -Wl,-z,noexecstack $(BUILD_DIR)/userspace/start.o userspace/libc/runtime.c "$<" -o "$@"
.PHONY: userspace initramfs
userspace: $(USER_BINARIES)
initramfs: $(USER_BINARIES) | guard-build
	python3 scripts/make-initramfs.py
