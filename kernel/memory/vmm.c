/* SPDX-License-Identifier: MIT */
#include <utamo/memory.h>
#include <utamo/hhdm.h>
#include <utamo/vmm.h>
#include <utamo/vmm_core.h>
#include <utamo/boot.h>
#include <utamo/cpu.h>
#include <utamo/framebuffer.h>
#include <utamo/paging.h>
#include <utamo/pmm.h>
#include <utamo/panic.h>
#include <utamo/log.h>

extern const char __kernel_start[], __kernel_end[];
extern const char __text_start[], __text_end[];
extern const char __rodata_start[], __rodata_end[];
extern const char __data_start[], __bss_start[], __bss_end[];

static struct hhdm_context direct_map;
static struct vmm_space kernel_space;
static struct vmm_info information;
static bool ready;
static uint64_t boot_table_visits;

static const struct memory_region *physical_region(uint64_t phys, size_t bytes)
{
    return hhdm_region(&direct_map, phys, bytes);
}

bool memory_phys_to_virt(uint64_t phys, size_t bytes, void **out)
{
    uint64_t virt;
    if (out == NULL || !hhdm_translate(&direct_map, phys, bytes, &virt)) {
        return false;
    }
    *out = (void *)(uintptr_t)virt;
    return true;
}

bool memory_hhdm_to_phys(const void *virt, size_t bytes, uint64_t *out)
{
    return hhdm_reverse(&direct_map, (uint64_t)(uintptr_t)virt, bytes, out);
}

static uint64_t *table_access(void *context, uint64_t phys)
{
    (void)context;
    const struct memory_region *region =
        physical_region(phys, (size_t)MEMORY_PAGE_SIZE);
    void *pointer;
    if (!memory_is_page_aligned(phys) || region == NULL ||
        (region->type != UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE &&
         region->type != UTAMO_MEMORY_USABLE) ||
        !memory_phys_to_virt(phys, (size_t)MEMORY_PAGE_SIZE, &pointer)) {
        return NULL;
    }
    return pointer;
}

static bool table_allocate(void *context, uint64_t *phys)
{
    (void)context;
    return pmm_alloc_page(phys);
}

static void table_release(void *context, uint64_t phys)
{
    (void)context;
    if (!pmm_free_page(phys)) {
        PANIC("VMM rollback frame ownership mismatch");
    }
}

static void table_commit(void *context, uint64_t phys)
{
    (void)context;
    if (!pmm_pin_page(phys)) {
        PANIC("VMM page-table pin failed");
    }
    ++information.table_pages;
}

static void invalidate(void *context, uint64_t virt)
{
    (void)context;
    cpu_invlpg(virt);
}

/* Only a previously empty PML4 slot can be mutated through the public API.
 * This cannot alter kernel, framebuffer, HHDM or Limine response mappings. */
static bool dynamic_address(uint64_t virt)
{
    return virt >= VMM_DYNAMIC_BASE && virt - VMM_DYNAMIC_BASE < VMM_DYNAMIC_SIZE;
}

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    const uint64_t saved = cpu_irq_save();
    const struct memory_region *region =
        physical_region(phys, (size_t)MEMORY_PAGE_SIZE);
    /* New aliases are WB RAM only; MMIO/framebuffer mappings retain PAT policy.
     * Kernel/bootloader aliases are forbidden, including any USER alias. */
    const bool result = ready && dynamic_address(virt) && region != NULL &&
        region->type == UTAMO_MEMORY_USABLE && pmm_is_allocated_page(phys) &&
        (flags & (VMM_CACHE_DISABLE | VMM_WRITE_THROUGH)) == 0u &&
        vmm_space_map(&kernel_space, virt, phys, flags);
    cpu_irq_restore(saved);
    return result;
}

bool vmm_unmap_page(uint64_t virt)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = ready && dynamic_address(virt) &&
        vmm_space_unmap(&kernel_space, virt);
    cpu_irq_restore(saved);
    return result;
}

bool vmm_protect_page(uint64_t virt, uint64_t flags)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = ready && dynamic_address(virt) &&
        (flags & (VMM_CACHE_DISABLE | VMM_WRITE_THROUGH)) == 0u &&
        vmm_space_protect(&kernel_space, virt, flags);
    cpu_irq_restore(saved);
    return result;
}

bool vmm_query_page(uint64_t virt, struct vmm_mapping *out)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = ready && vmm_space_query(&kernel_space, virt, out);
    cpu_irq_restore(saved);
    return result;
}

bool vmm_get_info(struct vmm_info *out)
{
    const uint64_t saved = cpu_irq_save();
    const bool result = ready && out != NULL;
    if (result) {
        *out = information;
    }
    cpu_irq_restore(saved);
    return result;
}

/* Bounded depth 4; budget rejects pathological aliasing without a static
 * page-table array. Limine owns every inherited table: reject a contradictory
 * USABLE classification before initializing any PMM bitmap. */
static bool validate_boot_tables(uint64_t phys, unsigned int level,
                                 uint64_t prefix, uint64_t ancestors[4])
{
    if (++boot_table_visits > UINT64_C(65536)) {
        return false;
    }
    const struct memory_region *region =
        physical_region(phys, (size_t)MEMORY_PAGE_SIZE);
    if (region == NULL || region->type != UTAMO_MEMORY_BOOTLOADER_RECLAIMABLE) {
        return false;
    }
    for (unsigned int i = level; i < 4u; ++i) {
        if (ancestors[i] == phys) {
            return false;
        }
    }
    ancestors[level - 1u] = phys;
    const uint64_t *table = table_access(NULL, phys);
    if (table == NULL) {
        return false;
    }
    for (unsigned int i = 0; i < 512u; ++i) {
        const uint64_t entry = table[i];
        if ((entry & VMM_PRESENT) == 0u) {
            continue;
        }
        if ((entry & VMM_ADDRESS_MASK & ~kernel_space.physical_mask) != 0u ||
            (!information.nx_enabled && (entry & VMM_NX) != 0u) ||
            (level == 4u && (entry & VMM_HUGE) != 0u)) {
            return false;
        }
        uint64_t virt = prefix | ((uint64_t)i << (12u + (level - 1u) * 9u));
        if ((virt & (UINT64_C(1) << 47u)) != 0u) {
            virt |= UINT64_C(0xffff000000000000);
        }
        if (level == 1u || (entry & VMM_HUGE) != 0u) {
            struct vmm_mapping mapping;
            if (!vmm_space_query(&kernel_space, virt, &mapping) || !mapping.mapped) {
                return false;
            }
        } else if (!validate_boot_tables(entry & kernel_space.physical_mask,
                                         level - 1u, virt, ancestors)) {
            return false;
        }
    }
    return true;
}

static bool check_linear_mapping(uint64_t virt, uint64_t phys, uint64_t bytes,
                                  bool require_write)
{
    while (bytes != 0u) {
        struct vmm_mapping mapping;
        if (!vmm_space_query(&kernel_space, virt, &mapping) || !mapping.mapped ||
            mapping.physical != phys || (mapping.flags & VMM_USER) != 0u ||
            (require_write && (mapping.flags & VMM_WRITABLE) == 0u)) {
            return false;
        }
        uint64_t step = mapping.page_size - (virt & (mapping.page_size - 1u));
        if (step > bytes) {
            step = bytes;
        }
        bytes -= step;
        if (bytes != 0u) {
            virt += step;
            phys += step;
        }
    }
    return true;
}

static bool protect_sections(void)
{
    const uint64_t start = (uint64_t)(uintptr_t)__kernel_start;
    const uint64_t end = (uint64_t)(uintptr_t)__kernel_end;
    const uint64_t text_start = (uint64_t)(uintptr_t)__text_start;
    const uint64_t text_end = (uint64_t)(uintptr_t)__text_end;
    const uint64_t rodata_start = (uint64_t)(uintptr_t)__rodata_start;
    const uint64_t rodata_end = (uint64_t)(uintptr_t)__rodata_end;
    /* Preflight complete extent before changing any permission. Large inherited
     * kernel leaves are preserved rather than split just for hardening. */
    for (uint64_t virt = start; virt < end; virt += MEMORY_PAGE_SIZE) {
        struct vmm_mapping mapping;
        const bool text = virt >= text_start && virt < text_end;
        const bool writable = !text && !(virt >= rodata_start && virt < rodata_end);
        if (!vmm_space_query(&kernel_space, virt, &mapping) || !mapping.mapped ||
            mapping.page_size != MEMORY_PAGE_SIZE ||
            (text && (mapping.flags & VMM_NX) != 0u) ||
            (writable && (mapping.flags & VMM_WRITABLE) == 0u)) {
            LOG_WARN("Kernel protection deferred: incompatible inherited mapping");
            return false;
        }
    }
    for (uint64_t virt = start; virt < end; virt += MEMORY_PAGE_SIZE) {
        uint64_t flags = VMM_PRESENT;
        if (virt >= text_start && virt < text_end) {
            /* Read/execute. */
        } else {
            if (information.nx_enabled) {
                flags |= VMM_NX;
            }
            if (!(virt >= rodata_start && virt < rodata_end)) {
                flags |= VMM_WRITABLE;
            }
        }
        if (!vmm_space_protect(&kernel_space, virt, flags)) {
            PANIC("Kernel section protection failed");
        }
    }
    return true;
}

bool memory_init(const struct memory_map *map, const struct framebuffer *fb)
{
    if (ready || direct_map.initialized || map == NULL || fb == NULL ||
        !fb->initialized || map->count == 0u ||
        map->count > UTAMO_MEMORY_REGION_LIMIT ||
        (cpu_read_cr4() & UTAMO_CR4_LA57) != 0u) {
        return false;
    }
    struct boot_memory_layout layout;
    if (!boot_read_memory_layout(&layout)) {
        return false;
    }
    struct cpu_cpuid_result cpuid;
    cpu_cpuid(UINT32_C(0x80000000), 0, &cpuid);
    if (cpuid.eax < UINT32_C(0x80000008)) {
        return false;
    }
    cpu_cpuid(UINT32_C(0x80000008), 0, &cpuid);
    information.physical_bits = cpuid.eax & 0xffu;
    uint64_t mask;
    if (!memory_physical_mask(information.physical_bits, &mask)) {
        return false;
    }
    cpu_cpuid(UINT32_C(0x80000001), 0, &cpuid);
    information.nx_supported = (cpuid.edx & (UINT32_C(1) << 20u)) != 0u;
    uint64_t efer = cpu_read_msr(UTAMO_EFER_MSR);
    if (information.nx_supported && (efer & UTAMO_EFER_NXE) == 0u) {
        cpu_write_msr(UTAMO_EFER_MSR, efer | UTAMO_EFER_NXE);
        efer = cpu_read_msr(UTAMO_EFER_MSR);
    }
    information.nx_enabled = information.nx_supported && (efer & UTAMO_EFER_NXE) != 0u;
    if (information.nx_supported && !information.nx_enabled) {
        return false;
    }
    const uint64_t kernel_start = (uint64_t)(uintptr_t)__kernel_start;
    const uint64_t kernel_end = (uint64_t)(uintptr_t)__kernel_end;
    if (layout.kernel_virt != kernel_start || kernel_end <= kernel_start ||
        !memory_is_page_aligned(layout.kernel_phys) ||
        !memory_is_page_aligned(layout.hhdm_offset) ||
        layout.hhdm_offset < UINT64_C(0xffff800000000000)) {
        return false;
    }
    information.hhdm_offset = layout.hhdm_offset;
    information.kernel_base = kernel_start;
    if (!hhdm_init(&direct_map, map, layout.hhdm_offset, information.physical_bits)) {
        return false;
    }
    const struct memory_region *image =
        physical_region(layout.kernel_phys, (size_t)(kernel_end - kernel_start));
    if (image == NULL || image->type != UTAMO_MEMORY_KERNEL_AND_MODULES) {
        return false;
    }
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (!hhdm_type_mapped(region->type)) {
            continue;
        }
        const uint64_t last = region->base + region->length - 1u;
        const uint64_t start = layout.hhdm_offset + region->base;
        const uint64_t end = layout.hhdm_offset + last;
        /* hhdm_init checked addition, width and canonical endpoints. */
        if ((start < VMM_DYNAMIC_BASE + VMM_DYNAMIC_SIZE && end >= VMM_DYNAMIC_BASE) ||
            end >= kernel_start) {
            return false;
        }
    }
    const uint64_t original_cr3 = cpu_read_cr3();
    information.root_phys = original_cr3 & mask;
    const struct vmm_ops ops = {
        .context = NULL, .table = table_access, .alloc = table_allocate,
        .free = table_release, .commit = table_commit, .invalidate = invalidate
    };
    if (!vmm_space_init(&kernel_space, information.root_phys,
                        information.physical_bits, information.nx_enabled, &ops)) {
        return false;
    }
    uint64_t ancestors[4] = {0};
    if (!validate_boot_tables(information.root_phys, 4u, 0u, ancestors)) {
        return false;
    }
    const uint64_t *root = table_access(NULL, information.root_phys);
    if (root == NULL || root[memory_page_index(VMM_DYNAMIC_BASE, 4u)] != 0u ||
        !check_linear_mapping(kernel_start, layout.kernel_phys,
                               kernel_end - kernel_start, false)) {
        return false;
    }
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *region = &map->regions[i];
        if (hhdm_type_mapped(region->type) &&
            !check_linear_mapping(layout.hhdm_offset + region->base,
                                   region->base, region->length, true)) {
            return false;
        }
    }
    uint64_t framebuffer_phys;
    if (fb->height > SIZE_MAX / fb->pitch ||
        !memory_hhdm_to_phys((const void *)(uintptr_t)fb->address,
                             fb->height * fb->pitch, &framebuffer_phys)) {
        return false;
    }
    const struct memory_region *framebuffer_region =
        physical_region(framebuffer_phys, fb->height * fb->pitch);
    if (framebuffer_region == NULL ||
        framebuffer_region->type != UTAMO_MEMORY_FRAMEBUFFER) {
        return false;
    }
    struct pmm_plan plan;
    void *storage;
    if (!pmm_plan(map, &plan) ||
        !memory_phys_to_virt(plan.storage_phys, (size_t)plan.storage_bytes, &storage) ||
        !pmm_init(map, &plan, storage) ||
        !pmm_reserve_range(layout.kernel_phys, (size_t)(kernel_end - kernel_start)) ||
        !pmm_reserve_range(framebuffer_phys, fb->height * fb->pitch)) {
        return false;
    }
    struct pmm_stats stats;
    if (!pmm_get_stats(&stats)) {
        return false;
    }
    LOG_OK("PMM initialized");
    LOG_INFO("Physical frames: %llu", (unsigned long long)stats.total_frames);
    LOG_INFO("Free frames: %llu", (unsigned long long)stats.free_frames);
    LOG_INFO("PMM metadata: phys=0x%llx, %llu bytes",
             (unsigned long long)stats.bitmap_phys,
             (unsigned long long)stats.storage_bytes);
    cpu_write_cr0(cpu_read_cr0() | UTAMO_CR0_WP);
    if ((cpu_read_cr0() & UTAMO_CR0_WP) == 0u) {
        return false;
    }
    information.sections_protected = protect_sections();
    if (cpu_read_cr3() != original_cr3) {
        return false;
    }
    ready = true;
    LOG_OK("VMM initialized");
    LOG_INFO("HHDM offset: 0x%llx", (unsigned long long)information.hhdm_offset);
    LOG_INFO("CR3 preserved: 0x%llx; inherited table visits: %llu",
             (unsigned long long)information.root_phys,
             (unsigned long long)boot_table_visits);
    LOG_INFO("NX supported: %s; enabled: %s",
             (const char *)(information.nx_supported ? "Yes" : "No"),
             (const char *)(information.nx_enabled ? "Yes" : "No"));
    LOG_INFO("Kernel section protections: %s",
             (const char *)(information.sections_protected ? "applied" : "deferred"));
    return true;
}
