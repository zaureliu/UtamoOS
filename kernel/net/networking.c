/* SPDX-License-Identifier: MIT */
#include <utamo/networking.h>
#include <utamo/net.h>
#include <utamo/pit.h>
#include <utamo/scheduler.h>
#include <utamo/heap.h>
#include <utamo/pmm.h>
#include <utamo/log.h>
#include <utamo/string.h>
static struct net_stack stack;
static bool attempted;
static uint64_t milliseconds(void *context)
{
    (void)context;return pit_ticks_to_milliseconds(pit_get_ticks());
}
static bool sleep_ms(void *context,uint64_t ms){(void)context;return thread_sleep_ms(ms);}
static void print_ip(uint32_t address)
{
    kprintf("%u.%u.%u.%u",(unsigned int)(address>>24u),(unsigned int)((address>>16u)&255u),
        (unsigned int)((address>>8u)&255u),(unsigned int)(address&255u));
}
static bool target_ip(const char *name,uint32_t *out)
{
    if(!stack.initialized){return false;}
    net_poll(&stack);
    if(!stack.configured){return false;}
    if(name==NULL || strcmp(name,"dns")==0){*out=stack.configuration.dns;return true;}
    if(strcmp(name,"gateway")==0){*out=stack.configuration.gateway;return true;}
    return net_ip_parse(name,out) && net_ip_unicast(*out);
}
bool networking_dhcp(void)
{
    if(!stack.initialized){return false;}
    const bool good=net_dhcp(&stack);
    if(good){
        kprintf("DHCP bound: IP=");print_ip(stack.configuration.address);
        kprintf(" mask=");print_ip(stack.configuration.mask);
        kprintf(" gateway=");print_ip(stack.configuration.gateway);
        kprintf(" DNS=");print_ip(stack.configuration.dns);
        kprintf(" lease_seconds=%u\n",(unsigned int)stack.configuration.lease_seconds);
    }
    return good;
}
void networking_init(void)
{
    if(attempted){return;}attempted=true;
    const int result=e1000_init();
    if(result==0){return;}
    const struct net_clock clock={.milliseconds=milliseconds,.sleep=sleep_ms};
    if(result<0 || !net_stack_init(&stack,e1000_device(),&clock)){
        LOG_WARN("E1000/network initialization rejected");return;
    }
    for(unsigned int i=0u;i<100u && !stack.nic->link(stack.nic->context);++i){
        if(!thread_sleep_ms(10u)){break;}
    }
    if(!networking_dhcp()){LOG_WARN("DHCP unavailable; network remains unconfigured");}
}
void networking_status(void)
{
    struct nic_stats nic;e1000_stats(&nic);
    if(stack.initialized){net_poll(&stack);}
    kprintf("E1000 ready=%s link=%s DMA_pages=%u quarantine=%s\n",
        (const char *)(nic.ready?"Yes":"No"),(const char *)(nic.link?"Up":"Down"),
        (unsigned int)nic.dma_pages,(const char *)(nic.quarantined?"Yes":"No"));
    kprintf("NIC tx=%llu rx=%llu dropped=%llu errors=%llu timeouts=%llu\n",
        (unsigned long long)nic.transmitted,(unsigned long long)nic.received,
        (unsigned long long)nic.dropped,(unsigned long long)nic.errors,(unsigned long long)nic.timeouts);
    kprintf("IPv4 configured=%s IP=",(const char *)(stack.configured?"Yes":"No"));
    print_ip(stack.configuration.address);kprintf(" mask=");print_ip(stack.configuration.mask);
    kprintf(" gateway=");print_ip(stack.configuration.gateway);kprintf(" DNS=");print_ip(stack.configuration.dns);
    kprintf("\nNetwork frames=%llu malformed=%llu ignored=%llu ARP=%llu IPv4=%llu ICMP=%llu UDP=%llu DHCP=%llu DNS=%llu echo_replies=%llu\n",
        (unsigned long long)stack.counters.frames,(unsigned long long)stack.counters.malformed,
        (unsigned long long)stack.counters.ignored,(unsigned long long)stack.counters.arp,
        (unsigned long long)stack.counters.ipv4,(unsigned long long)stack.counters.icmp,
        (unsigned long long)stack.counters.udp,(unsigned long long)stack.counters.dhcp,
        (unsigned long long)stack.counters.dns,(unsigned long long)stack.counters.echo_replies);
}
void networking_ping(const char *target)
{
    uint32_t address;
    if(!target_ip(target,&address)){kprintf("Ping unavailable or invalid address.\n");return;}
    unsigned int received=0u;
    for(unsigned int i=0u;i<4u;++i){
        uint64_t elapsed;
        if(net_ping(&stack,address,&elapsed)){
            ++received;kprintf("Reply from ");print_ip(address);
            kprintf(" seq=%u time=%llu ms\n",i+1u,(unsigned long long)elapsed);
        }else{kprintf("Ping timeout seq=%u\n",i+1u);}
    }
    kprintf("Ping: sent=4 received=%u lost=%u\n",received,4u-received);
}
void networking_resolve(const char *name,const char *server,uint16_t port)
{
    uint32_t source,address;
    if(!target_ip(server,&source) || !net_resolve(&stack,name,source,port,&address)){
        kprintf("DNS resolution failed.\n");return;
    }
    kprintf("DNS A %s = ",name);print_ip(address);kprintf("\n");
}
bool networking_selftest(uint16_t fixture_port)
{
    uint32_t gateway;
    if(fixture_port==0u || !target_ip("gateway",&gateway)){return false;}
    struct heap_stats before,after;struct pmm_stats pbefore,pafter;
    if(!heap_get_stats(&before) || !pmm_get_stats(&pbefore)){return false;}
    bool good=true;
    unsigned char payload[64],reply[64];
    memcpy(payload,"UTAMO",5u);
    for(size_t i=5u;i<sizeof(payload);++i){payload[i]=(unsigned char)(i*17u);}
    for(unsigned int i=0u;good && i<16u;++i){
        uint64_t elapsed;uint32_t address;size_t received=0u;
        good=net_ping(&stack,gateway,&elapsed) &&
            net_resolve(&stack,"fixture.test",gateway,fixture_port,&address) && address==0xc000027bu &&
            net_udp_request(&stack,gateway,fixture_port,payload,sizeof(payload),reply,sizeof(reply),&received) &&
            received==sizeof(payload) && memcmp(payload,reply,sizeof(payload))==0;
    }
    good=good && heap_get_stats(&after) && pmm_get_stats(&pafter) &&
        before.used_bytes==after.used_bytes && before.live_allocations==after.live_allocations &&
        pbefore.free_frames==pafter.free_frames && heap_validate();
    kprintf("Network stress: rounds=16 ping_dns_udp=%s accounting=%s\n",
        (const char *)(good?"PASS":"FAIL"),(const char *)(good?"PASS":"FAIL"));
    return good;
}
