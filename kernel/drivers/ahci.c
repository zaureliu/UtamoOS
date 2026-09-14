/* SPDX-License-Identifier: MIT */
#include <utamo/ahci.h>
#include <utamo/pci.h>
#include <utamo/mmio.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/log.h>
#include <utamo/string.h>
#define AHCI_ERROR_MASK UINT32_C(0x7d000000)
struct ahci_state {
    volatile uint32_t *port;
    unsigned char *dma;
    uint64_t physical, commands, failures;
    unsigned int port_index;
    bool attempted, ready, quarantined, busy;
};
static struct ahci_state state;
static struct block_device disk;
static uint32_t reg(volatile uint32_t *base, unsigned int offset) { return base[offset / 4u]; }
static void set(volatile uint32_t *base, unsigned int offset, uint32_t value)
{
    base[offset / 4u] = value;
    (void)base[offset / 4u]; /* Flush posted register writes. */
}
static bool wait_clear(volatile uint32_t *base, unsigned int offset, uint32_t bits)
{
    const uint64_t start = pit_get_ticks();
    for (uint64_t i = 0u; i < UINT64_C(10000000); ++i) {
        if ((reg(base, offset) & bits) == 0u) { return true; }
        if (pit_get_ticks() - start >= 200u) { return false; }
        __asm__ volatile ("pause");
    }
    return false;
}
static bool stop_port(volatile uint32_t *port)
{
    set(port, 0x14u, 0u);
    set(port, 0x18u, reg(port, 0x18u) & ~UINT32_C(1));
    if (!wait_clear(port, 0x18u, 1u << 15u)) { return false; }
    set(port, 0x18u, reg(port, 0x18u) & ~UINT32_C(16));
    return wait_clear(port, 0x18u, 1u << 14u);
}
static bool issue(uint8_t command, uint64_t lba, size_t count)
{
    volatile uint32_t *p = state.port;
    if (state.quarantined || !wait_clear(p, 0x20u, 0x88u) ||
        reg(p, 0x38u) != 0u || reg(p, 0x34u) != 0u ||
        !ahci_build_read(state.dma, state.dma + 8192u, state.physical + 8192u,
                         state.physical + 12288u, command, lba, count)) {
        return false;
    }
    memset(state.dma + 12288u, 0, 4096u);
    set(p, 0x10u, UINT32_MAX);
    set(p, 0x30u, UINT32_MAX);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    set(p, 0x38u, 1u);
    ++state.commands;
    const uint64_t start = pit_get_ticks();
    bool complete = false;
    for (uint64_t i = 0u; i < UINT64_C(10000000); ++i) {
        const uint32_t status = reg(p, 0x10u);
        if ((status & AHCI_ERROR_MASK) != 0u) { break; }
        if ((reg(p, 0x38u) & 1u) == 0u) { complete = true; break; }
        if (pit_get_ticks() - start >= 200u) { break; }
        __asm__ volatile ("pause");
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const volatile uint32_t *header = (const volatile uint32_t *)(const void *)state.dma;
    if (!complete || (reg(p, 0x20u) & 0x89u) != 0u ||
        (reg(p, 0x10u) & AHCI_ERROR_MASK) != 0u || header[1] != count * 512u) {
        ++state.failures;
        state.quarantined = true;
        state.ready = false;
        (void)stop_port(p);
        /* Never free/reuse any DMA frame after an ambiguous device failure. */
        return false;
    }
    return true;
}
static bool read_disk(void *context, uint64_t lba, size_t count, void *buffer)
{
    (void)context;
    preempt_disable();
    if (!state.ready || state.busy) { preempt_enable(); return false; }
    state.busy = true;
    unsigned char *out = buffer;
    bool good = true;
    while (good && count != 0u) {
        const size_t batch = count < 8u ? count : 8u;
        good = issue(0x25u, lba, batch);
        if (good) {
            memcpy(out, state.dma + 12288u, batch * 512u);
            out += batch * 512u; lba += batch; count -= batch;
        }
    }
    state.busy = false;
    preempt_enable();
    return good;
}
int ahci_init(void)
{
    if (state.attempted) { return -1; }
    state.attempted = true;
    const struct pci_device *controller = NULL;
    for (size_t i = 0u; i < pci_count(); ++i) {
        const struct pci_device *d = pci_get(i);
        if (d->class_code == 1u && d->subclass == 6u && d->interface == 1u &&
            (d->header_type & 0x7fu) == 0u) { controller = d; break; }
    }
    if (controller == NULL) { return 0; }
    struct pci_bar bar;
    void *mapped;
    if (!pci_size_bar(controller, 5u, &bar) || bar.io || bar.size < 0x180u ||
        bar.size > UTAMO_MMIO_MAX || (bar.address & 4095u) != 0u ||
        !vmm_map_mmio(bar.address, (size_t)((bar.size + 4095u) & ~UINT64_C(4095)), &mapped)) {
        LOG_WARN("AHCI rejected: BAR/MMIO contract");
        return -1;
    }
    volatile uint32_t *abar = mapped;
    const uint32_t capability = reg(abar, 0u), implemented = reg(abar, 0xcu);
    const uint32_t version = reg(abar, 0x10u);
    if (version < 0x10000u || version > 0x10301u || implemented == 0u) { return -1; }
    if (version >= 0x10200u && (reg(abar, 0x24u) & 1u) != 0u) {
        set(abar, 0x28u, reg(abar, 0x28u) | 2u);
        if (!wait_clear(abar, 0x28u, 0x11u)) { return -1; }
    }
    set(abar, 4u, (reg(abar, 4u) | (1u << 31u)) & ~UINT32_C(2));
    for (unsigned int i = 0u; i < 32u; ++i) {
        if ((implemented & (1u << i)) == 0u) { continue; }
        if (i > (capability & 31u) || 0x100u + (uint64_t)(i + 1u) * 0x80u > bar.size) {
            return -1;
        }
        volatile uint32_t *p = abar + (0x100u + i * 0x80u) / 4u;
        if (!stop_port(p)) { return -1; }
        if (state.port == NULL && (reg(p, 0x28u) & 0xf0fu) == 0x103u &&
            reg(p, 0x24u) == 0x101u) { state.port = p; state.port_index = i; }
    }
    if (state.port == NULL) { LOG_INFO("AHCI: no SATA ATA disk"); return 0; }
    if (!pmm_alloc_pages(4u, &state.physical)) { return -1; }
    if (((capability & (1u << 31u)) == 0u && state.physical > UINT32_MAX - 16383u) ||
        !memory_phys_to_virt(state.physical, 16384u, &mapped)) {
        (void)pmm_free_pages(state.physical, 4u);
        state.physical = 0u;
        return -1;
    }
    state.dma = mapped;
    memset(state.dma, 0, 16384u);
    volatile uint32_t *p = state.port;
    set(p, 0u, (uint32_t)state.physical);
    set(p, 4u, (uint32_t)(state.physical >> 32u));
    set(p, 8u, (uint32_t)(state.physical + 4096u));
    set(p, 12u, (uint32_t)((state.physical + 4096u) >> 32u));
    set(p, 0x10u, UINT32_MAX); set(p, 0x30u, UINT32_MAX);
    /* From this point the DMA allocation is permanent, even on failure. */
    state.quarantined = true;
    if (!pci_enable_mmio_dma(controller)) { return -1; }
    set(p, 0x18u, reg(p, 0x18u) | 16u);
    set(p, 0x18u, reg(p, 0x18u) | 1u);
    state.quarantined = false;
    if (!issue(0xecu, 0u, 1u) || !ahci_identify(state.dma + 12288u, &disk.sectors)) {
        state.quarantined = true; (void)stop_port(p); return -1;
    }
    disk.sector_size = 512u; disk.read = read_disk;
    state.ready = true;
    LOG_OK("AHCI readonly disk: port=%u sectors=%llu DMA=0x%llx",
        state.port_index, (unsigned long long)disk.sectors, (unsigned long long)state.physical);
    return 1;
}
const struct block_device *ahci_device(void) { return state.ready ? &disk : NULL; }
void ahci_status(void)
{
    kprintf("AHCI ready=%s readonly=Yes port=%u sectors=%llu commands=%llu failures=%llu DMA_pages=%u quarantine=%s\n",
        (const char *)(state.ready ? "Yes" : "No"), state.port_index,
        (unsigned long long)disk.sectors, (unsigned long long)state.commands,
        (unsigned long long)state.failures, state.physical != 0u ? 4u : 0u,
        (const char *)(state.quarantined ? "Yes" : "No"));
}
