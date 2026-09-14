/* SPDX-License-Identifier: MIT */
#include <utamo/nic.h>
#include <utamo/e1000_ring.h>
#include <utamo/pci.h>
#include <utamo/mmio.h>
#include <utamo/memory.h>
#include <utamo/pmm.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/log.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks,failures,allocated,freed,preemption,tx_commands,next_rx;
static uint64_t ticks;
static const char *scenario;
static bool fail_tx;
static uint32_t registers[32768];
static _Alignas(4096) unsigned char dma[24576],last_tx[1514];
static size_t last_bytes;
static const struct pci_device controller={.vendor_id=0x8086u,.device_id=0x100eu,.class_code=2u};
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
size_t pci_count(void){return 1u;}
const struct pci_device *pci_get(size_t index){return index==0u?&controller:NULL;}
bool pci_size_bar(const struct pci_device *d,unsigned int index,struct pci_bar *out)
{
    CHECK(d==&controller && index==0u);*out=(struct pci_bar){.address=0xfebc0000u,.size=131072u};return true;
}
bool pci_prepare_mmio(const struct pci_device *d){CHECK(d==&controller && allocated==0u);return true;}
bool pci_enable_mmio_dma(const struct pci_device *d)
{
    CHECK(d==&controller && allocated==1u && registers[0x100u/4u]==0u && registers[0x400u/4u]==0u);
    return strcmp(scenario,"enable-fail")!=0;
}
bool vmm_map_mmio(uint64_t physical,size_t bytes,void **out)
{
    CHECK(physical==0xfebc0000u && bytes==131072u);*out=registers;return true;
}
bool pmm_alloc_pages(size_t count,uint64_t *out)
{
    CHECK(count==6u);++allocated;*out=0x100000u;return true;
}
bool pmm_free_pages(uint64_t physical,size_t count)
{
    CHECK(physical==0x100000u && count==6u);++freed;return true;
}
bool memory_phys_to_virt(uint64_t physical,size_t bytes,void **out)
{
    CHECK(physical==0x100000u && bytes==24576u);
    if(strcmp(scenario,"map-fail")==0){return false;}
    *out=dma;return true;
}
void preempt_disable(void){++preemption;}
void preempt_enable(void){CHECK(preemption!=0u);--preemption;}
bool thread_sleep_ms(uint64_t ms){ticks+=(ms+9u)/10u;return true;}
void log_message(enum log_level level,const char *format,...){(void)level;(void)format;}
uint64_t pit_get_ticks(void)
{
    ++ticks;
    if(strcmp(scenario,"reset-timeout")!=0){registers[0]&=~(1u<<26u);}
    uint32_t *eerd=&registers[0x14u/4u];
    if((*eerd&1u)!=0u && (*eerd&16u)==0u){
        static const uint16_t words[3]={0x5452u,0x1200u,0x5634u};
        const uint32_t index=(*eerd>>8u)&255u;
        CHECK(index<3u);
        *eerd|=16u;
        if(index<3u && strcmp(scenario,"mac-invalid")!=0){*eerd|=(uint32_t)words[index]<<16u;}
    }
    const unsigned int head=registers[0x3810u/4u],tail=registers[0x3818u/4u];
    if(head!=tail){
        CHECK(preemption==1u && head<8u && tail<8u);
        if(fail_tx && strcmp(scenario,"tx-timeout")==0){return ticks;}
        struct e1000_tx_descriptor *descriptors=(struct e1000_tx_descriptor *)(void *)(dma+128u);
        struct e1000_tx_descriptor *d=&descriptors[head];
        CHECK(d->address==0x105000u && d->length>=60u && d->length<=1514u && d->command==11u);
        last_bytes=d->length;memcpy(last_tx,dma+20480u,last_bytes);
        d->status=fail_tx?9u:1u;
        registers[0x3810u/4u]=tail;++tx_commands;
    }
    return ticks;
}
static void inject(uint16_t length,uint8_t status,uint8_t errors,unsigned char pattern)
{
    struct e1000_rx_descriptor *r=(struct e1000_rx_descriptor *)(void *)dma;
    CHECK(r[next_rx].status==0u && r[next_rx].address==0x101000u+(uint64_t)next_rx*2048u);
    memset(dma+4096u+(size_t)next_rx*2048u,pattern,length>2048u?2048u:length);
    r[next_rx].length=length;r[next_rx].errors=errors;r[next_rx].status=status;
    next_rx=(next_rx+1u)%8u;
}
int main(int argc,char **argv)
{
    if(argc!=2){return 2;}scenario=argv[1];registers[2]=2u;
    const int result=e1000_init();
    struct nic_stats stats;e1000_stats(&stats);
    if(strcmp(scenario,"reset-timeout")==0 || strcmp(scenario,"mac-invalid")==0){
        CHECK(result==-1 && allocated==0u && !stats.ready);
    }else if(strcmp(scenario,"map-fail")==0){
        CHECK(result==-1 && allocated==1u && freed==1u && stats.dma_pages==0u);
    }else if(strcmp(scenario,"enable-fail")==0){
        CHECK(result==-1 && allocated==1u && freed==0u && stats.quarantined && stats.dma_pages==6u);
    }else{
        const struct nic_device *nic=e1000_device();
        CHECK(result==1 && nic!=NULL && stats.ready && stats.link && stats.dma_pages==6u && freed==0u);
        if(nic!=NULL){
            const unsigned char expected_mac[6]={0x52u,0x54u,0u,0x12u,0x34u,0x56u};
            CHECK(memcmp(nic->mac,expected_mac,6u)==0);
            CHECK(registers[0x2800u/4u]==0x100000u && registers[0x2808u/4u]==128u &&
                  registers[0x3800u/4u]==0x100080u && registers[0x3808u/4u]==128u);
            unsigned char frame[1514],copy[1514];
            for(unsigned int i=0u;i<32u;++i){
                memset(frame,(int)i,sizeof(frame));
                CHECK(nic->transmit(NULL,frame,i==0u?14u:1514u));
                CHECK(last_bytes==(i==0u?60u:1514u));
                bool good=true;
                for(size_t j=0u;j<last_bytes;++j){good=good && last_tx[j]==(j>=14u && i==0u?0u:(unsigned char)i);}
                CHECK(good);
                inject(1514u,3u,0u,(unsigned char)i);
                CHECK(nic->receive(NULL,copy,sizeof(copy))==1514u && memcmp(frame,copy,sizeof(copy))==0);
                CHECK(registers[0x2818u/4u]==(i%8u));
            }
            inject(2000u,3u,0u,1u);CHECK(nic->receive(NULL,copy,sizeof(copy))==0u);
            inject(60u,1u,0u,2u);inject(60u,3u,0u,3u);inject(60u,3u,0u,4u);
            CHECK(nic->receive(NULL,copy,sizeof(copy))==60u && copy[0]==4u);
            inject(60u,3u,1u,5u);CHECK(nic->receive(NULL,copy,sizeof(copy))==0u);
            e1000_stats(&stats);
            CHECK(stats.transmitted==32u && stats.received==33u && stats.dropped==4u);
            if(strcmp(scenario,"link-down")==0){
                registers[2]=0u;const unsigned int before=tx_commands;
                CHECK(!nic->transmit(NULL,frame,60u) && tx_commands==before);
                e1000_stats(&stats);CHECK(stats.ready && !stats.link && !stats.quarantined);
            }else if(strcmp(scenario,"good")!=0){
                fail_tx=true;CHECK(!nic->transmit(NULL,frame,60u));
                e1000_stats(&stats);
                CHECK(!stats.ready && stats.quarantined && freed==0u && preemption==0u &&
                      (registers[0x100u/4u]&2u)==0u && (registers[0x400u/4u]&2u)==0u);
                CHECK(!nic->transmit(NULL,frame,60u) && nic->receive(NULL,copy,sizeof(copy))==0u);
            }
        }
    }
    CHECK(e1000_init()==-1 && preemption==0u && ticks<2000u);
    printf("E1000 driver %s: %u checks, %u failures\n",scenario,checks,failures);
    return failures==0u?0:1;
}
