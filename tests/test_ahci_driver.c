/* SPDX-License-Identifier: MIT */
/* Actual AHCI driver, deterministic MMIO/DMA model; one scenario per process. */
#include <utamo/ahci.h>
#include <utamo/pci.h>
#include <utamo/mmio.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/log.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks, failures, allocated, freed, preemption, commands;
static uint64_t ticks;
static const char *scenario;
static bool read_failure;
static uint32_t registers[1024];
static _Alignas(4096) unsigned char dma[16384];
static const struct pci_device controller = {.class_code=1u,.subclass=6u,.interface=1u};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %u: %s\n", (unsigned int)__LINE__, #x); } } while (0)
size_t pci_count(void) { return 1u; }
const struct pci_device *pci_get(size_t index) { return index == 0u ? &controller : NULL; }
bool pci_size_bar(const struct pci_device *device, unsigned int index, struct pci_bar *out)
{
    CHECK(device==&controller && index==5u);
    *out=(struct pci_bar){.address=0xfebf1000u,.size=4096u};
    return true;
}
bool vmm_map_mmio(uint64_t physical, size_t bytes, void **out)
{
    CHECK(physical==0xfebf1000u && bytes==4096u);*out=registers;return true;
}
bool pci_enable_mmio_dma(const struct pci_device *device)
{
    CHECK(device==&controller);
    CHECK((registers[(0x100u+0x18u)/4u]&17u)==0u);
    return strcmp(scenario,"enable-fail")!=0;
}
bool pmm_alloc_pages(size_t count, uint64_t *out)
{
    CHECK(count==4u);
    if(strcmp(scenario,"alloc-fail")==0) { return false; }
    ++allocated;*out=0x100000u;return true;
}
bool pmm_free_pages(uint64_t physical, size_t count)
{
    CHECK(physical==0x100000u && count==4u);++freed;return true;
}
bool memory_phys_to_virt(uint64_t physical, size_t bytes, void **out)
{
    CHECK(physical==0x100000u && bytes==16384u);
    if(strcmp(scenario,"map-fail")==0) { return false; }
    *out=dma;return true;
}
void preempt_disable(void) { ++preemption; }
void preempt_enable(void) { CHECK(preemption!=0u);--preemption; }
void kprintf(const char *format, ...) { (void)format; }
void log_message(enum log_level level, const char *format, ...) { (void)level;(void)format; }
uint64_t pit_get_ticks(void)
{
    ++ticks;
    uint32_t *port=registers+0x100u/4u;
    port[0x10u/4u]=0u; /* Model PxIS write-one-clear. */
    if(port[0x38u/4u]!=0u) {
        const unsigned char command=dma[8194];
        if(read_failure && strcmp(scenario,"timeout")==0) { return ticks; }
        if(read_failure && (strcmp(scenario,"task-error")==0 || strcmp(scenario,"bus-error")==0)) {
            port[0x10u/4u]=1u<<(strcmp(scenario,"bus-error")==0 ? 29u : 30u);
            port[0x20u/4u]=strcmp(scenario,"bus-error")==0 ? 0u : 1u;return ticks;
        }
        CHECK(port[0]==0x100000u && port[2]==0x101000u &&
              (port[0x18u/4u]&17u)==17u && port[0x14u/4u]==0u);
        CHECK(dma[0]==5u && dma[2]==1u && dma[9]==0x20u && dma[10]==0x10u);
        CHECK(dma[8192]==0x27u && dma[8193]==0x80u &&
              dma[8192+129]==0x30u && dma[8192+130]==0x10u);
        uint32_t bytes;
        if(command==0xecu) {
            CHECK(commands==0u);
            dma[12288+167]=strcmp(scenario,"identify-fail")==0?0u:0x44u;
            dma[12288+202]=2u; bytes=512u;
        } else {
            CHECK(command==0x25u && preemption==1u);
            bytes=(uint32_t)dma[8192+12]*512u;
            for(size_t i=0u;i<bytes;++i) { dma[12288+i]=(unsigned char)(i+dma[8192+4]); }
        }
        ++commands;
        if(read_failure && strcmp(scenario,"short-dma")==0) { bytes-=2u; }
        for(size_t i=0u;i<4u;++i) { dma[4+i]=(unsigned char)(bytes>>(i*8u)); }
        port[0x38u/4u]=0u;
    }
    return ticks;
}
int main(int argc, char **argv)
{
    if(argc!=2) { return 2; }
    scenario=argv[1];
    registers[0]=1u<<31u;registers[0xcu/4u]=1u;registers[0x10u/4u]=0x10000u;
    registers[(0x100u+0x28u)/4u]=0x103u;registers[(0x100u+0x24u)/4u]=0x101u;
    if(strcmp(scenario,"stop-timeout")==0) { registers[(0x100u+0x18u)/4u]=1u<<15u; }
    if(strcmp(scenario,"absent")==0) { registers[(0x100u+0x24u)/4u]=0xeb140101u; }
    const int result=ahci_init();
    if(strcmp(scenario,"absent")==0) {
        CHECK(result==0 && allocated==0u && ahci_device()==NULL);
    } else if(strcmp(scenario,"alloc-fail")==0 || strcmp(scenario,"stop-timeout")==0) {
        CHECK(result==-1 && allocated==0u && ahci_device()==NULL);
    } else if(strcmp(scenario,"map-fail")==0) {
        CHECK(result==-1 && allocated==1u && freed==1u && ahci_device()==NULL);
    } else if(strcmp(scenario,"enable-fail")==0 || strcmp(scenario,"identify-fail")==0) {
        CHECK(result==-1 && allocated==1u && freed==0u && ahci_device()==NULL);
        CHECK((registers[(0x100u+0x18u)/4u]&17u)==0u);
    } else {
        CHECK(result==1 && allocated==1u && freed==0u && ahci_device()!=NULL);
        const struct block_device *disk=ahci_device();
        if(disk!=NULL) {
            unsigned char buffer[4608];memset(buffer,0xa5,sizeof(buffer));
            CHECK(disk->sectors==131072u && disk->sector_size==512u);
            CHECK(block_read(disk,10u,9u,buffer) && commands==3u);
            bool match=true;
            for(size_t i=0u;i<sizeof(buffer);++i) {
                match=match && buffer[i]==(unsigned char)(i<4096u?i+10u:i-4096u+18u);
            }
            CHECK(match && preemption==0u);
            if(strcmp(scenario,"good")!=0) {
                read_failure=true;memset(buffer,0xa5,sizeof(buffer));
                CHECK(!block_read(disk,20u,1u,buffer));
                CHECK(ahci_device()==NULL && freed==0u && preemption==0u);
                CHECK((registers[(0x100u+0x18u)/4u]&17u)==0u);
                bool unchanged=true;
                for(size_t i=0u;i<sizeof(buffer);++i) { unchanged=unchanged && buffer[i]==0xa5u; }
                CHECK(unchanged);
                const unsigned int previous=commands;
                CHECK(!block_read(disk,0u,1u,buffer) && commands==previous);
            }
        }
    }
    CHECK(ahci_init()==-1);
    CHECK(preemption==0u && ticks<1000u);
    printf("AHCI driver %s: %u checks, %u failures\n",scenario,checks,failures);
    return failures==0u?0:1;
}
