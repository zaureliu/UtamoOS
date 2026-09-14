/* SPDX-License-Identifier: MIT */
#include <utamo/nic.h>
#include <utamo/e1000_ring.h>
#include <utamo/pci.h>
#include <utamo/mmio.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/string.h>
#include <utamo/log.h>
#define E1000_DMA_PAGES 6u
static struct {
    volatile uint32_t *registers;
    unsigned char *dma;
    volatile struct e1000_rx_descriptor *rx;
    volatile struct e1000_tx_descriptor *tx;
    struct nic_stats stats;
    unsigned int rx_next, tx_next;
    bool attempted, busy, discarding;
} state;
static struct nic_device device;
static uint32_t get(unsigned int offset) { return state.registers[offset/4u]; }
static void put(unsigned int offset, uint32_t value)
{
    state.registers[offset/4u]=value;
    (void)get(8u); /* STATUS flushes posted register writes. */
}
static bool wait_bit(unsigned int offset, uint32_t bit, bool set)
{
    const uint64_t start=pit_get_ticks();
    for (uint64_t i=0u;i<UINT64_C(10000000);++i) {
        if (((get(offset)&bit)!=0u)==set) { return true; }
        if (pit_get_ticks()-start>=100u) { return false; }
        __asm__ volatile ("pause");
    }
    return false;
}
static void quarantine(void)
{
    ++state.stats.errors;
    state.stats.ready=false;
    state.stats.quarantined=true;
    put(0xd8u,UINT32_MAX);
    put(0x100u,get(0x100u)&~UINT32_C(2));
    put(0x400u,get(0x400u)&~UINT32_C(2));
    /* The device may still own DMA. Never release or reuse the allocation. */
}
static bool link_up(void *context)
{
    (void)context;
    return state.stats.ready && (get(8u)&2u)!=0u;
}
static bool transmit(void *context, const void *frame, size_t bytes)
{
    (void)context;
    if (frame==NULL || bytes<14u || bytes>UTAMO_NET_FRAME_MAX) { return false; }
    preempt_disable();
    if (!state.stats.ready || state.busy || !link_up(NULL)) {
        preempt_enable();return false;
    }
    state.busy=true;
    const unsigned int index=state.tx_next;
    if ((state.tx[index].status&1u)==0u) {
        quarantine();state.busy=false;preempt_enable();return false;
    }
    const size_t padded=bytes<60u?60u:bytes;
    memcpy(state.dma+20480u,frame,bytes);
    if (bytes<padded) { memset(state.dma+20480u+bytes,0,padded-bytes); }
    struct e1000_tx_descriptor descriptor;
    if (!e1000_tx_prepare(&descriptor,state.stats.dma_physical+20480u,padded)) {
        state.busy=false;preempt_enable();return false;
    }
    state.tx[index]=descriptor;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    state.tx_next=(index+1u)%E1000_RING_COUNT;
    put(0x3818u,state.tx_next);
    const uint64_t start=pit_get_ticks();
    bool complete=false;
    for (uint64_t i=0u;i<UINT64_C(10000000);++i) {
        if ((state.tx[index].status&1u)!=0u) { complete=true;break; }
        if (pit_get_ticks()-start>=100u) { break; }
        __asm__ volatile ("pause");
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (!complete || (state.tx[index].status&14u)!=0u) {
        if (!complete) { ++state.stats.timeouts; }
        quarantine();
    } else { ++state.stats.transmitted; }
    state.busy=false;
    preempt_enable();
    return complete && state.stats.ready;
}
static size_t receive(void *context, void *frame, size_t capacity)
{
    (void)context;
    if (frame==NULL || capacity==0u) { return 0u; }
    preempt_disable();
    if (!state.stats.ready || state.busy) { preempt_enable();return 0u; }
    size_t delivered=0u;
    for (unsigned int n=0u;n<E1000_RING_COUNT;++n) {
        const unsigned int index=state.rx_next;
        const uint8_t status=state.rx[index].status;
        if ((status&1u)==0u) { break; }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        const uint16_t bytes=state.rx[index].length;
        const bool valid=e1000_rx_valid(bytes,status,state.rx[index].errors,&state.discarding);
        if (valid && bytes<=capacity) {
            /* CPU pointer derives from software-owned index, never DMA metadata. */
            memcpy(frame,state.dma+4096u+(size_t)index*E1000_BUFFER_SIZE,bytes);
            delivered=bytes;++state.stats.received;
        } else { ++state.stats.dropped; }
        state.rx[index].length=0u;
        state.rx[index].errors=0u;
        state.rx[index].status=0u;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        put(0x2818u,index);
        state.rx_next=(index+1u)%E1000_RING_COUNT;
        if (delivered!=0u) { break; }
    }
    preempt_enable();
    return delivered;
}
static bool read_mac(void)
{
    for (unsigned int i=0u;i<3u;++i) {
        put(0x14u,1u|(i<<8u));
        if (!wait_bit(0x14u,16u,true)) { return false; }
        const uint32_t value=get(0x14u)>>16u;
        device.mac[i*2u]=(unsigned char)value;
        device.mac[i*2u+1u]=(unsigned char)(value>>8u);
    }
    unsigned int any=0u;
    for (size_t i=0u;i<6u;++i) { any|=device.mac[i]; }
    return any!=0u && (device.mac[0]&1u)==0u;
}
int e1000_init(void)
{
    if (state.attempted) { return -1; }
    state.attempted=true;
    const struct pci_device *pci=NULL;
    for (size_t i=0u;i<pci_count();++i) {
        const struct pci_device *d=pci_get(i);
        if (d->vendor_id==0x8086u && d->device_id==0x100eu &&
            d->class_code==2u && (d->header_type&0x7fu)==0u) { pci=d;break; }
    }
    if (pci==NULL) { return 0; }
    struct pci_bar bar;void *mapped;
    if (!pci_size_bar(pci,0u,&bar) || bar.io || bar.size<0x6000u ||
        bar.size>UTAMO_MMIO_MAX || (bar.address&4095u)!=0u ||
        !vmm_map_mmio(bar.address,(size_t)bar.size,&mapped)) { return -1; }
    if (!pci_prepare_mmio(pci)) { return -1; }
    state.registers=mapped;
    put(0xd8u,UINT32_MAX);put(0x100u,0u);put(0x400u,0u);
    put(0u,get(0u)|(1u<<26u));
    if (!wait_bit(0u,1u<<26u,false) || !thread_sleep_ms(10u)) { return -1; }
    put(0xd8u,UINT32_MAX);(void)get(0xc0u);
    if (!read_mac()) { return -1; }
    if (!pmm_alloc_pages(E1000_DMA_PAGES,&state.stats.dma_physical)) { return -1; }
    if (!memory_phys_to_virt(state.stats.dma_physical,E1000_DMA_PAGES*4096u,&mapped)) {
        (void)pmm_free_pages(state.stats.dma_physical,E1000_DMA_PAGES);
        state.stats.dma_physical=0u;return -1;
    }
    state.dma=mapped;
    memset(state.dma,0,E1000_DMA_PAGES*4096u);
    state.rx=(volatile struct e1000_rx_descriptor *)(void *)state.dma;
    state.tx=(volatile struct e1000_tx_descriptor *)(void *)(state.dma+128u);
    for (unsigned int i=0u;i<E1000_RING_COUNT;++i) {
        state.rx[i].address=state.stats.dma_physical+4096u+(uint64_t)i*E1000_BUFFER_SIZE;
        state.tx[i].status=1u;
    }
    state.stats.dma_pages=E1000_DMA_PAGES;
    /* Register publication makes the DMA allocation permanent, including failure. */
    state.stats.quarantined=true;
    if (!pci_enable_mmio_dma(pci)) { return -1; }
    put(0x2800u,(uint32_t)state.stats.dma_physical);
    put(0x2804u,(uint32_t)(state.stats.dma_physical>>32u));put(0x2808u,128u);
    put(0x2810u,0u);put(0x2818u,E1000_RING_COUNT-1u);
    put(0x3800u,(uint32_t)(state.stats.dma_physical+128u));
    put(0x3804u,(uint32_t)((state.stats.dma_physical+128u)>>32u));put(0x3808u,128u);
    put(0x3810u,0u);put(0x3818u,0u);
    for (unsigned int i=0u;i<128u;++i) { put(0x5200u+i*4u,0u); }
    for (unsigned int i=1u;i<16u;++i) { put(0x5400u+i*8u+4u,0u); }
    uint32_t low=0u;
    for (unsigned int i=0u;i<4u;++i) { low|=(uint32_t)device.mac[i]<<(i*8u); }
    put(0x5400u,low);
    put(0x5404u,(uint32_t)device.mac[4]|((uint32_t)device.mac[5]<<8u)|(1u<<31u));
    put(0x28u,0u);put(0x2cu,0u);put(0x30u,0u);put(0x170u,0u);
    put(0x5000u,0u); /* Software owns protocol checksum validation. */
    put(0u,(get(0u)|0x60u)&~((1u<<31u)|(1u<<30u)|(1u<<28u)|(1u<<27u)|
        (1u<<12u)|(1u<<11u)|(1u<<7u)|(1u<<3u))); /* ASDE/SLU, automatic link. */
    put(0x410u,10u|(10u<<10u)|(10u<<20u));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    put(0x400u,2u|8u|(0x10u<<4u)|(0x40u<<12u));
    put(0x100u,2u|(1u<<15u)|(1u<<26u)); /* 2048 bytes, broadcast, strip CRC. */
    device.mtu=1500u;device.transmit=transmit;device.receive=receive;device.link=link_up;
    state.stats.quarantined=false;state.stats.ready=true;
    LOG_OK("E1000 initialized: MAC=%x:%x:%x:%x:%x:%x DMA_pages=%u",
        (unsigned int)device.mac[0],(unsigned int)device.mac[1],(unsigned int)device.mac[2],
        (unsigned int)device.mac[3],(unsigned int)device.mac[4],(unsigned int)device.mac[5],
        (unsigned int)state.stats.dma_pages);
    return 1;
}
const struct nic_device *e1000_device(void) { return state.stats.ready?&device:NULL; }
void e1000_stats(struct nic_stats *out)
{
    if (out==NULL) { return; }
    preempt_disable();*out=state.stats;out->link=state.stats.ready && link_up(NULL);preempt_enable();
}
