/* SPDX-License-Identifier: MIT */
#include <utamo/net.h>
#include <stdio.h>
#include <string.h>
static unsigned int checks,failures,head,tail,arp_requests,discoveries,requests,pings,echo_replies;
static uint64_t clock_ms;
static bool drop_dhcp,bad_ack,drop_ping,bad_dns,wrong_udp,link_active=true;
static unsigned char queued[16][1514];static size_t sizes[16];
static const unsigned char guest_mac[6]={0x52,0x54,0,0x12,0x34,0x56},router_mac[6]={0x52,0x55,0x0a,0x17,0,2};
static struct net_stack stack;
#define CLIENT UINT32_C(0x0a170064)
#define ROUTER UINT32_C(0x0a170002)
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL %u: %s\n",(unsigned int)__LINE__,#x);}}while(0)
static void queue_frame(uint16_t type,const void *data,size_t bytes)
{
    if(tail-head==16u){CHECK(false);return;}
    size_t count;
    CHECK(ethernet_build(queued[tail%16u],1514u,router_mac,guest_mac,type,data,bytes,&count));
    sizes[tail%16u]=count;++tail;
}
static void emit_ip(uint32_t from,uint32_t to,uint8_t protocol,const void *payload,size_t bytes)
{
    unsigned char packet[1500];size_t count;
    CHECK(ipv4_build(packet,sizeof(packet),from,to,protocol,1u,payload,bytes,&count));
    queue_frame(0x800u,packet,count);
}
static void emit_udp(uint32_t to,uint16_t source,uint16_t destination,const void *payload,size_t bytes)
{
    unsigned char packet[1480];size_t count;
    CHECK(udp_build(packet,sizeof(packet),ROUTER,to,source,destination,payload,bytes,&count));
    emit_ip(ROUTER,to,17u,packet,count);
}
static void offer_reply(const struct udp_packet *udp)
{
    const unsigned char *request=udp->payload;
    CHECK(udp->bytes==300u && request[0]==1u && request[1]==1u && request[2]==6u &&
          net_get16(request+10u)==0x8000u && memcmp(request+28u,guest_mac,6u)==0);
    const unsigned char kind=request[242];
    CHECK(request[240]==53u && request[241]==1u && (kind==1u || kind==3u));
    if(kind==1u){++discoveries;}else{++requests;}
    if(drop_dhcp){return;}
    unsigned char reply[300]={0};reply[0]=2u;reply[1]=1u;reply[2]=6u;
    net_put32(reply+4u,net_get32(request+4u));
    net_put32(reply+16u,bad_ack && kind==3u?CLIENT+1u:CLIENT);memcpy(reply+28u,guest_mac,6u);
    net_put32(reply+236u,0x63825363u);
    const unsigned char options[]={53,1,2,1,4,255,255,255,0,3,4,10,23,0,2,
        6,4,10,23,0,3,51,4,0,0,14,16,54,4,10,23,0,2,255};
    memcpy(reply+240u,options,sizeof(options));reply[242]=kind==1u?2u:5u;
    emit_udp(UINT32_MAX,67u,68u,reply,sizeof(reply));
}
static bool transmit(void *context,const void *data,size_t bytes)
{
    (void)context;
    struct ethernet_packet eth;CHECK(ethernet_decode(data,bytes,&eth));
    if(!ethernet_decode(data,bytes,&eth)){return false;}
    CHECK(memcmp(eth.source,guest_mac,6u)==0);
    if(eth.type==0x806u){
        struct arp_packet arp;CHECK(arp_decode(eth.payload,eth.bytes,&arp));
        if(!arp_decode(eth.payload,eth.bytes,&arp)){return false;}
        if(arp.operation==2u){return true;}
        ++arp_requests;CHECK(arp.sender_ip==CLIENT && arp.target_ip==ROUTER);
        struct arp_packet reply={.operation=2u,.sender_ip=ROUTER,.target_ip=CLIENT};
        memcpy(reply.sender_mac,router_mac,6u);memcpy(reply.target_mac,guest_mac,6u);
        unsigned char packet[28];CHECK(arp_build(packet,sizeof(packet),&reply));queue_frame(0x806u,packet,28u);
        return true;
    }
    struct ipv4_packet ip;CHECK(eth.type==0x800u && ipv4_decode(eth.payload,eth.bytes,&ip));
    if(!ipv4_decode(eth.payload,eth.bytes,&ip)){return false;}
    if(ip.protocol==17u){
        struct udp_packet udp;CHECK(udp_decode(&ip,&udp));
        if(!udp_decode(&ip,&udp)){return false;}
        if(udp.source==68u && udp.destination==67u){
            CHECK(ip.source==0u && ip.destination==UINT32_MAX);offer_reply(&udp);return true;
        }
        CHECK(ip.source==CLIENT && ip.destination==ROUTER && udp.destination==5300u);
        if(udp.bytes>=5u && memcmp(udp.payload,"UTAMO",5u)==0){
            if(wrong_udp){emit_udp(CLIENT,5301u,udp.source,udp.payload,udp.bytes);}
            emit_udp(CLIENT,5300u,udp.source,udp.payload,udp.bytes);return true;
        }
        unsigned char response[512];
        CHECK(udp.bytes<=496u && net_get16(udp.payload+4u)==1u);
        memcpy(response,udp.payload,udp.bytes);
        net_put16(response+2u,0x8180u);net_put16(response+6u,1u);
        const unsigned char answer[]={0xc0,0x0c,0,1,0,1,0,0,0,60,0,4,192,0,2,123};
        memcpy(response+udp.bytes,answer,sizeof(answer));
        if(bad_dns){response[udp.bytes+1u]=(unsigned char)udp.bytes;}
        emit_udp(CLIENT,5300u,udp.source,response,udp.bytes+sizeof(answer));
    }else{
        CHECK(ip.protocol==1u);
        bool reply;uint16_t id,seq;
        CHECK(icmp_echo_decode(ip.payload,ip.bytes,&reply,&id,&seq));
        if(reply){++echo_replies;return true;}
        ++pings;
        CHECK(ip.source==CLIENT && ip.destination==ROUTER && id==0x5554u && ip.bytes==24u);
        if(drop_ping){return true;}
        unsigned char payload[24];memcpy(payload,ip.payload,24u);payload[0]=0u;
        net_put16(payload+2u,0u);net_put16(payload+2u,net_checksum(payload,sizeof(payload)));
        /* A bad checksum precedes the valid response. It must not satisfy the wait. */
        payload[23]^=1u;emit_ip(ROUTER,CLIENT,1u,payload,sizeof(payload));
        payload[23]^=1u;emit_ip(ROUTER,CLIENT,1u,payload,sizeof(payload));
    }
    return true;
}
static size_t receive(void *context,void *data,size_t capacity)
{
    (void)context;if(head==tail){return 0u;}
    const size_t bytes=sizes[head%16u];CHECK(bytes<=capacity);
    if(bytes>capacity){return 0u;}memcpy(data,queued[head%16u],bytes);++head;return bytes;
}
static bool link_up(void *context){(void)context;return link_active;}
static uint64_t milliseconds(void *context){(void)context;return clock_ms;}
static bool sleep_ms(void *context,uint64_t ms){(void)context;clock_ms+=ms;return true;}
int main(void)
{
    struct nic_device nic={.mtu=1500u,.transmit=transmit,.receive=receive,.link=link_up};
    memcpy(nic.mac,guest_mac,6u);
    const struct net_clock clock={.milliseconds=milliseconds,.sleep=sleep_ms};
    CHECK(net_stack_init(&stack,&nic,&clock));CHECK(!net_stack_init(&stack,&nic,&clock));
    CHECK(net_dhcp(&stack) && stack.configured && stack.configuration.address==CLIENT &&
          stack.configuration.gateway==ROUTER && stack.configuration.dns==0x0a170003u);
    CHECK(discoveries==1u && requests==1u && stack.lease_expires==3600000u);
    uint64_t elapsed=UINT64_MAX;
    CHECK(net_ping(&stack,ROUTER,&elapsed) && elapsed==0u && arp_requests==1u && pings==1u);
    CHECK(stack.counters.malformed==1u);
    CHECK(net_ping(&stack,ROUTER,&elapsed) && arp_requests==1u);
    clock_ms=61000u;
    CHECK(net_ping(&stack,ROUTER,&elapsed) && arp_requests==2u);
    unsigned char response[64],sentinel[64];memset(response,0xa5,sizeof(response));memcpy(sentinel,response,64u);
    size_t received=99u;wrong_udp=true;
    CHECK(net_udp_request(&stack,ROUTER,5300u,"UTAMO echo",10u,response,sizeof(response),&received) &&
          received==10u && memcmp(response,"UTAMO echo",10u)==0);
    memcpy(response,sentinel,64u);received=99u;
    CHECK(!net_udp_request(&stack,ROUTER,5300u,"UTAMO echo",10u,response,4u,&received) &&
          received==99u && memcmp(response,sentinel,64u)==0);
    uint32_t address=0x11223344u;
    CHECK(net_resolve(&stack,"fixture.test",ROUTER,5300u,&address) && address==0xc000027bu);
    bad_dns=true;address=0x11223344u;
    CHECK(!net_resolve(&stack,"fixture.test",ROUTER,5300u,&address) && address==0x11223344u);
    bad_dns=false;drop_ping=true;elapsed=UINT64_MAX;
    CHECK(!net_ping(&stack,ROUTER,&elapsed) && elapsed==UINT64_MAX && !stack.busy && stack.echo_peer==0u);
    drop_ping=false;
    unsigned char echo[24]={8u,0u,0u,0u,0x12u,0x34u,0u,1u};
    net_put16(echo+2u,net_checksum(echo,sizeof(echo)));emit_ip(ROUTER,CLIENT,1u,echo,sizeof(echo));net_poll(&stack);
    CHECK(echo_replies==1u && stack.counters.echo_replies==1u);
    const unsigned int before_echo=echo_replies;
    emit_ip(ROUTER,UINT32_MAX,1u,echo,sizeof(echo));net_poll(&stack);
    CHECK(echo_replies==before_echo); /* No ICMP reply to broadcast IP. */
    const uint32_t bad_sources[]={CLIENT,0x0a170000u,0x0a1700ffu};
    for(size_t i=0u;i<3u;++i){
        emit_ip(bad_sources[i],CLIENT,1u,echo,sizeof(echo));net_poll(&stack);
        CHECK(echo_replies==before_echo);
        struct arp_packet invalid={.operation=1u,.sender_ip=bad_sources[i],.target_ip=CLIENT};
        memcpy(invalid.sender_mac,router_mac,6u);
        unsigned char packet[28];CHECK(arp_build(packet,sizeof(packet),&invalid));
        queue_frame(0x806u,packet,sizeof(packet));net_poll(&stack);
        bool cached=false;
        for(size_t j=0u;j<8u;++j){cached=cached || stack.arp_cache[j].ip==bad_sources[i];}
        CHECK(!cached);
    }
    stack.busy=true;CHECK(!net_dhcp(&stack) && !net_ping(&stack,ROUTER,&elapsed));stack.busy=false;
    clock_ms=stack.lease_expires;net_poll(&stack);
    CHECK(!stack.configured && stack.configuration.address==0u && !net_ping(&stack,ROUTER,&elapsed));
    bad_ack=true;CHECK(!net_dhcp(&stack) && !stack.configured && stack.configuration.address==0u);
    bad_ack=false;drop_dhcp=true;
    const uint64_t start=clock_ms;
    CHECK(!net_dhcp(&stack) && !stack.busy && stack.dhcp_state==0u && clock_ms-start==3000u);
    drop_dhcp=false;CHECK(net_dhcp(&stack));
    link_active=false;drop_ping=true;CHECK(!net_ping(&stack,ROUTER,&elapsed) && !stack.busy);
    const uint64_t down_start=clock_ms;
    CHECK(!net_dhcp(&stack) && !stack.configured && !stack.busy && clock_ms-down_start==2000u);
    link_active=true;drop_ping=false;
    CHECK(net_dhcp(&stack));
    for(unsigned int i=0u;i<32u;++i){
        CHECK(net_ping(&stack,ROUTER,&elapsed));
        CHECK(net_resolve(&stack,"fixture.test",ROUTER,5300u,&address) && address==0xc000027bu);
    }
    CHECK(head==tail && !stack.busy && !stack.polling);
    printf("Network transaction host tests: %u checks, %u failures\n",checks,failures);
    return failures==0u?0:1;
}
