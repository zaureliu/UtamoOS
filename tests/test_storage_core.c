/* SPDX-License-Identifier: MIT */
#include <utamo/pci.h>
#include <utamo/mmio.h>
#include <utamo/ahci.h>
#include <utamo/io.h>
#include <utamo/cpu.h>
#include <utamo/log.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks, failures, block_calls, config_writes;
static uint32_t selected, config[64], masks[6];
static uint64_t irq = 512u;
static bool unsafe_io, overflow_scan;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
uint64_t cpu_irq_save(void) { const uint64_t old = irq; irq = 0u; return old; }
void cpu_irq_restore(uint64_t flags) { irq = flags; }
void io_out32(uint16_t port, uint32_t value)
{
    if (irq != 0u) { unsafe_io = true; }
    if (port == 0xcf8u) { selected = value; }
    else if (port == 0xcfcu) { config[(selected & 255u) / 4u] = value; ++config_writes; }
}
uint32_t io_in32(uint16_t port)
{
    if (irq != 0u || port != 0xcfcu) { unsafe_io = true; }
    const size_t index = (selected & 255u) / 4u;
    if (index >= 4u && index < 10u && config[index] == UINT32_MAX) { return masks[index - 4u]; }
    return config[index];
}
void io_out16(uint16_t port, uint16_t value)
{
    if (irq != 0u || port != 0xcfcu || (selected & 255u) != 4u) { unsafe_io = true; }
    config[1] = (config[1] & UINT32_C(0xffff0000)) | value;
}
void kprintf(const char *format, ...) { (void)format; }
static uint32_t scan(void *context, uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    (void)context;
    if (overflow_scan) { return offset == 0u ? 0x12348086u : 0u; }
    if (!((bus == 0u && slot == 2u && (function == 0u || function == 7u)) ||
          (bus == 255u && slot == 31u && function == 0u))) { return UINT32_MAX; }
    if (offset == 0u) { return 0x29228086u; }
    if (offset == 8u) { return 0x01060102u; }
    if (offset == 12u) { return bus == 0u ? 0x800000u : 0u; }
    return offset == 36u ? 0xfebf1000u : 0u;
}
static bool read_block(void *context, uint64_t lba, size_t count, void *buffer)
{
    (void)context; (void)lba; (void)count; (void)buffer; ++block_calls; return true;
}
static void pci_tests(void)
{
    struct pci_inventory inventory = {0};
    const struct pci_ops ops = {.read = scan};
    CHECK(!pci_scan(NULL, &ops)); CHECK(!pci_scan(&inventory, NULL));
    CHECK(pci_scan(&inventory, &ops));
    CHECK(inventory.count == 3u && inventory.devices[1].function == 7u &&
          inventory.devices[2].bus == 255u && inventory.devices[2].slot == 31u);
    CHECK(inventory.devices[0].class_code == 1u && inventory.devices[0].interface == 1u &&
          inventory.devices[0].bars[5] == 0xfebf1000u);
    CHECK(!pci_scan(&inventory, &ops));
    inventory.count = 0u; overflow_scan = true;
    CHECK(!pci_scan(&inventory, &ops) && inventory.count == 0u);
    struct pci_device d = {.header_type = 0u};
    struct pci_bar out = {0}, sentinel;
    memset(&sentinel, 0x5a, sizeof(sentinel));
    CHECK(!pci_decode_bar(&d, 0u, &out));
    d.bars[0] = 0xfebf1000u;
    CHECK(pci_decode_bar(&d, 0u, &out) && out.address == 0xfebf1000u && !out.io && !out.wide);
    d.bars[1] = 0x4001u;
    CHECK(pci_decode_bar(&d, 1u, &out) && out.address == 0x4000u && out.io);
    d.bars[2] = 0x8000000cu; d.bars[3] = 1u;
    CHECK(pci_decode_bar(&d, 2u, &out) && out.address == UINT64_C(0x180000000) && out.wide && out.prefetch);
    CHECK(!pci_decode_bar(&d, 3u, &out));
    d.bars[4] = 0x90000004u; d.bars[5] = 2u;
    CHECK(pci_decode_bar(&d, 4u, &out) && out.address == UINT64_C(0x290000000));
    CHECK(!pci_decode_bar(&d, 5u, &out));
    d.bars[2] = 0u; d.bars[3] = 0u; d.bars[4] = 0u; d.bars[5] = 0x80000004u;
    CHECK(!pci_decode_bar(&d, 5u, &out));
    d.header_type = 1u;
    CHECK(!pci_decode_bar(&d, 2u, &out));
    d.header_type = 0u; d.slot = 31u; d.function = 7u; d.bus = 255u;
    config[1] = 0xf9000007u; config[4] = d.bars[0]; masks[0] = 0xfffff000u;
    CHECK(pci_size_bar(&d, 0u, &out) && out.size == 4096u && out.address == d.bars[0]);
    CHECK(config[4] == d.bars[0] && config[1] == 0xf9000007u && irq == 512u && !unsafe_io);
    CHECK((selected & 0xffffff00u) == 0x80ffff00u);
    masks[0] = 0xffffd000u; out = sentinel;
    CHECK(!pci_size_bar(&d, 0u, &out) && memcmp(&out, &sentinel, sizeof(out)) == 0);
    CHECK(config[4] == d.bars[0] && config[1] == 0xf9000007u);
    d.bars[2] = 0x80000004u; d.bars[3] = 1u; config[6] = d.bars[2]; config[7] = d.bars[3];
    masks[2] = 0xfffe0004u; masks[3] = UINT32_MAX;
    CHECK(pci_size_bar(&d, 2u, &out) && out.size == 131072u && out.address == UINT64_C(0x180000000));
    CHECK(config[6] == d.bars[2] && config[7] == 1u && config[1] == 0xf9000007u);
    CHECK(pci_enable_mmio_dma(&d) && config[1] == 0xf9000407u && !unsafe_io && irq == 512u);
    const unsigned int before = config_writes;
    CHECK(!pci_size_bar(NULL, 0u, &out) && !pci_size_bar(&d, 6u, &out) && config_writes == before);
}
static void bounds_tests(void)
{
    unsigned char buffer[4096];
    struct block_device d = {.sectors = 10u, .sector_size = 512u, .read = read_block};
    CHECK(block_read(&d, 9u, 1u, buffer) && block_calls == 1u);
    CHECK(!block_read(&d, 10u, 1u, buffer));
    CHECK(!block_read(&d, 9u, 2u, buffer));
    CHECK(!block_read(&d, UINT64_MAX, 2u, buffer));
    CHECK(!block_read(&d, 0u, SIZE_MAX, buffer));
    CHECK(!block_read(&d, 0u, 0u, buffer));
    CHECK(!block_read(&d, 0u, 1u, NULL) && block_calls == 1u);
    struct memory_map map = {.count = 3u, .regions = {
        {.base=0x100000u,.length=0x100000u,.type=UTAMO_MEMORY_USABLE},
        {.base=0x200000u,.length=0x100000u,.type=UTAMO_MEMORY_RESERVED},
        {.base=0x300000u,.length=0x100000u,.type=UTAMO_MEMORY_FRAMEBUFFER}}};
    CHECK(mmio_range_allowed(&map,0x200000u,4096u));
    CHECK(mmio_range_allowed(&map,0xfebf1000u,4096u));
    CHECK(!mmio_range_allowed(&map,0x1ff000u,8192u));
    CHECK(!mmio_range_allowed(&map,0x2ff000u,8192u));
    CHECK(!mmio_range_allowed(&map,0xfffff000u,4095u));
    CHECK(!mmio_range_allowed(&map,0x80000u,4096u));
    CHECK(!mmio_range_allowed(&map,UINT64_MAX-4095u,4096u));
    for (int type = UTAMO_MEMORY_USABLE; type <= UTAMO_MEMORY_FRAMEBUFFER; ++type) {
        map.regions[1].type = (enum memory_type)type;
        CHECK(mmio_range_allowed(&map,0x200000u,4096u) == (type == UTAMO_MEMORY_RESERVED));
    }
}
static void command_tests(void)
{
    unsigned char h[32], t[144], id[512] = {0};
    for (size_t count = 1u; count <= 8u; ++count) {
        CHECK(ahci_build_read(h,t,0x100002000u,0x100003000u,0x25u,0x123456789abcu,count));
        CHECK(h[0] == 5u && h[2] == 1u && h[12] == 1u && h[9] == 0x20u);
        CHECK(t[0] == 0x27u && t[1] == 0x80u && t[2] == 0x25u && t[7] == 0x40u &&
              t[4] == 0xbcu && t[5] == 0x9au && t[6] == 0x78u &&
              t[8] == 0x56u && t[9] == 0x34u && t[10] == 0x12u && t[12] == count);
        CHECK(t[132] == 1u && t[140] == 255u && t[141] == count * 2u - 1u && t[143] == 0u);
    }
    CHECK(ahci_build_read(h,t,8192u,12288u,0xecu,0u,1u) && t[12] == 0u && t[7] == 0u);
    unsigned char saved_h[32], saved_t[144]; memcpy(saved_h,h,32); memcpy(saved_t,t,144);
    CHECK(!ahci_build_read(h,t,8192u,12288u,0x35u,0u,1u)); /* Writes forbidden. */
    CHECK(!ahci_build_read(h,t,8192u,12288u,0x25u,UINT64_C(1)<<48u,1u));
    CHECK(!ahci_build_read(h,t,8192u,12288u,0x25u,(UINT64_C(1)<<48u)-1u,2u));
    CHECK(!ahci_build_read(h,t,8193u,12288u,0x25u,0u,1u));
    CHECK(!ahci_build_read(h,t,8192u,12289u,0x25u,0u,1u));
    CHECK(!ahci_build_read(h,t,8192u,12288u,0xecu,0u,2u));
    CHECK(memcmp(saved_h,h,32)==0 && memcmp(saved_t,t,144)==0);
    uint64_t sectors = 99u;
    CHECK(!ahci_identify(id,&sectors) && sectors == 99u);
    id[166] = 0u; id[167] = 0x44u; id[202] = 2u;
    CHECK(ahci_identify(id,&sectors) && sectors == 131072u);
    id[213] = 0x50u; id[235] = 8u;
    CHECK(!ahci_identify(id,&sectors) && sectors == 131072u);
    id[235] = 1u;
    CHECK(ahci_identify(id,&sectors));
    id[1] = 0x80u;
    CHECK(!ahci_identify(id,&sectors));
}
int main(void)
{
    pci_tests(); bounds_tests(); command_tests();
    printf("Storage core host tests: %u checks, %u failures\n", checks, failures);
    return failures == 0u ? 0 : 1;
}
