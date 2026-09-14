/* SPDX-License-Identifier: MIT */
#include <utamo/net.h>
#include <utamo/string.h>
static const unsigned char broadcast[6]={255u,255u,255u,255u,255u,255u};
static uint64_t now(struct net_stack *s) { return s->clock.milliseconds(s->clock.context); }
static uint64_t deadline(uint64_t current, uint64_t duration)
{
    return duration>UINT64_MAX-current?UINT64_MAX:current+duration;
}
static uint32_t random_value(struct net_stack *s)
{
    s->random=s->random*1664525u+1013904223u;
    return s->random;
}
static bool send_ethernet(struct net_stack *s, const unsigned char mac[6], uint16_t type,
                           const void *payload, size_t bytes)
{
    unsigned char frame[UTAMO_NET_FRAME_MAX];size_t written;
    return ethernet_build(frame,sizeof(frame),s->nic->mac,mac,type,payload,bytes,&written) &&
        s->nic->transmit(s->nic->context,frame,written);
}
static bool send_ip(struct net_stack *s, const unsigned char mac[6], uint32_t source,
                    uint32_t destination, uint8_t protocol, const void *payload, size_t bytes)
{
    unsigned char packet[1500];size_t written;
    ++s->ip_id;
    return ipv4_build(packet,sizeof(packet),source,destination,protocol,s->ip_id,payload,bytes,&written) &&
        send_ethernet(s,mac,0x800u,packet,written);
}
static bool send_udp(struct net_stack *s, const unsigned char mac[6], uint32_t source,
                     uint32_t destination, uint16_t local, uint16_t remote, const void *payload, size_t bytes)
{
    unsigned char packet[1480];size_t written;
    return udp_build(packet,sizeof(packet),source,destination,local,remote,payload,bytes,&written) &&
        send_ip(s,mac,source,destination,17u,packet,written);
}
static bool arp_lookup(struct net_stack *s, uint32_t ip, unsigned char mac[6])
{
    const uint64_t current=now(s);
    for(size_t i=0u;i<8u;++i) {
        if(s->arp_cache[i].ip==ip && s->arp_cache[i].expires>current) {
            memcpy(mac,s->arp_cache[i].mac,6u);return true;
        }
    }
    return false;
}
static void arp_remember(struct net_stack *s, uint32_t ip, const unsigned char mac[6])
{
    size_t chosen=0u;
    for(size_t i=0u;i<8u;++i) {
        if(s->arp_cache[i].ip==ip) { chosen=i;break; }
        if(s->arp_cache[i].expires<s->arp_cache[chosen].expires) { chosen=i; }
    }
    s->arp_cache[chosen].ip=ip;
    memcpy(s->arp_cache[chosen].mac,mac,6u);
    s->arp_cache[chosen].expires=deadline(now(s),60000u);
}
static bool local_peer(struct net_stack *s, uint32_t ip)
{
    const uint32_t mask=s->configuration.mask,host=ip&~mask;
    return net_ip_unicast(ip) && ip!=s->configuration.address &&
        (ip&mask)==(s->configuration.address&mask) && host!=0u && host!=~mask;
}
static void handle_arp(struct net_stack *s, const struct ethernet_packet *eth)
{
    struct arp_packet arp;
    if(!arp_decode(eth->payload,eth->bytes,&arp) || memcmp(eth->source,arp.sender_mac,6u)!=0) {
        ++s->counters.malformed;return;
    }
    ++s->counters.arp;
    if(!s->configured || arp.target_ip!=s->configuration.address || !local_peer(s,arp.sender_ip)) {
        return;
    }
    unsigned char existing[6];
    if(arp.operation==1u || (memcmp(arp.target_mac,s->nic->mac,6u)==0 &&
       (s->arp_waiting==arp.sender_ip || arp_lookup(s,arp.sender_ip,existing)))) {
        arp_remember(s,arp.sender_ip,arp.sender_mac);
    }
    if(arp.operation==1u) {
        struct arp_packet response={.operation=2u,.sender_ip=s->configuration.address,.target_ip=arp.sender_ip};
        memcpy(response.sender_mac,s->nic->mac,6u);memcpy(response.target_mac,arp.sender_mac,6u);
        unsigned char packet[28];
        if(arp_build(packet,sizeof(packet),&response)) {
            (void)send_ethernet(s,arp.sender_mac,0x806u,packet,sizeof(packet));
        }
    }
}
static void handle_udp(struct net_stack *s, const struct ipv4_packet *ip)
{
    struct udp_packet udp;
    if(!udp_decode(ip,&udp)) { ++s->counters.malformed;return; }
    ++s->counters.udp;
    if(s->dhcp_state!=0u && udp.source==67u && udp.destination==68u) {
        struct dhcp_offer offer;
        if(!dhcp_decode(udp.payload,udp.bytes,s->xid,s->nic->mac,&offer) ||
           offer.server!=ip->source ||
           (ip->destination!=UINT32_MAX && ip->destination!=offer.address)) {
            ++s->counters.malformed;return;
        }
        ++s->counters.dhcp;
        if(s->dhcp_state==1u && offer.type==2u && !s->offered_ready) {
            s->offered=offer;s->offered_ready=true;
        } else if(s->dhcp_state==2u && offer.type==5u &&
                  offer.server==s->offered.server && offer.address==s->offered.address) {
            s->offered=offer;s->ack_ready=true;
        }
    } else if(s->configured && ip->destination==s->configuration.address &&
              s->udp_port!=0u && udp.destination==s->udp_port &&
              udp.source==s->udp_remote_port && ip->source==s->udp_peer &&
              udp.bytes<=sizeof(s->udp_data) && !s->udp_ready) {
        memcpy(s->udp_data,udp.payload,udp.bytes);s->udp_bytes=udp.bytes;s->udp_ready=true;
    }
}
static void handle_ip(struct net_stack *s, const struct ethernet_packet *eth)
{
    struct ipv4_packet ip;
    if(!ipv4_decode(eth->payload,eth->bytes,&ip) || !net_ip_unicast(ip.source)) {
        ++s->counters.malformed;return;
    }
    if(s->configured && ((ip.source&s->configuration.mask)==
       (s->configuration.address&s->configuration.mask)) && !local_peer(s,ip.source)) {
        ++s->counters.malformed;return;
    }
    ++s->counters.ipv4;
    const bool dhcp=s->dhcp_state!=0u && ip.protocol==17u;
    if(!dhcp && (!s->configured || ip.destination!=s->configuration.address)) {
        ++s->counters.ignored;return;
    }
    if(ip.protocol==17u) { handle_udp(s,&ip);return; }
    if(ip.protocol!=1u || !s->configured || ip.destination!=s->configuration.address) {
        ++s->counters.ignored;return;
    }
    bool reply;uint16_t id,sequence;
    if(!icmp_echo_decode(ip.payload,ip.bytes,&reply,&id,&sequence)) {
        ++s->counters.malformed;return;
    }
    ++s->counters.icmp;
    if(reply) {
        if(s->echo_peer==ip.source && id==0x5554u && sequence==s->echo_sequence &&
           ip.bytes==24u && memcmp(ip.payload+8u,s->echo_data,16u)==0) { s->echo_ready=true; }
    } else {
        unsigned char response[1480];memcpy(response,ip.payload,ip.bytes);
        response[0]=0u;net_put16(response+2u,0u);
        net_put16(response+2u,net_checksum(response,ip.bytes));
        if(send_ip(s,eth->source,s->configuration.address,ip.source,1u,response,ip.bytes)) {
            ++s->counters.echo_replies;
        }
    }
}
bool net_stack_init(struct net_stack *s, const struct nic_device *nic, const struct net_clock *clock)
{
    if(s==NULL || s->initialized || nic==NULL || !net_mac_unicast(nic->mac) ||
       nic->mtu!=1500u || nic->transmit==NULL || nic->receive==NULL || nic->link==NULL ||
       clock==NULL || clock->milliseconds==NULL || clock->sleep==NULL) { return false; }
    memset(s,0,sizeof(*s));s->nic=nic;s->clock=*clock;s->initialized=true;
    s->random=(uint32_t)now(s)^net_get32(nic->mac+2u)^0x41535452u;
    return true;
}
void net_poll(struct net_stack *s)
{
    if(s==NULL || !s->initialized || s->polling) { return; }
    s->polling=true;
    if(s->configured && now(s)>=s->lease_expires) {
        s->configured=false;memset(&s->configuration,0,sizeof(s->configuration));
        memset(s->arp_cache,0,sizeof(s->arp_cache));
    }
    for(unsigned int i=0u;i<32u;++i) {
        unsigned char frame[UTAMO_NET_FRAME_MAX];
        const size_t bytes=s->nic->receive(s->nic->context,frame,sizeof(frame));
        if(bytes==0u) { break; }
        ++s->counters.frames;
        struct ethernet_packet eth;
        if(!ethernet_decode(frame,bytes,&eth)) { ++s->counters.malformed;continue; }
        if(memcmp(eth.destination,s->nic->mac,6u)!=0 && memcmp(eth.destination,broadcast,6u)!=0) {
            ++s->counters.ignored;continue;
        }
        if(eth.type==0x806u) { handle_arp(s,&eth); }
        else if(eth.type==0x800u) { handle_ip(s,&eth); }
        else { ++s->counters.ignored; }
    }
    s->polling=false;
}
static bool wait_flag(struct net_stack *s, const bool *flag, uint64_t milliseconds)
{
    const uint64_t start=now(s);
    for(unsigned int i=0u;i<401u;++i) {
        net_poll(s);
        if(*flag) { return true; }
        if(now(s)-start>=milliseconds || !s->nic->link(s->nic->context) ||
           !s->clock.sleep(s->clock.context,10u)) { return false; }
    }
    return false;
}
static bool route(struct net_stack *s, uint32_t destination, unsigned char mac[6])
{
    net_poll(s);
    if(!s->configured || !net_ip_unicast(destination) ||
       destination==s->configuration.address) { return false; }
    const uint32_t mask=s->configuration.mask;
    const uint32_t next=(destination&mask)==(s->configuration.address&mask)?
        destination:s->configuration.gateway;
    if((next&~mask)==0u || (next&~mask)==~mask) { return false; }
    if(arp_lookup(s,next,mac)) { return true; }
    s->arp_waiting=next;
    struct arp_packet request={.operation=1u,.sender_ip=s->configuration.address,.target_ip=next};
    memcpy(request.sender_mac,s->nic->mac,6u);
    unsigned char packet[28];
    bool good=arp_build(packet,sizeof(packet),&request),found=false;
    for(unsigned int attempt=0u;good && attempt<2u && !found;++attempt) {
        good=send_ethernet(s,broadcast,0x806u,packet,sizeof(packet));
        const uint64_t start=now(s);
        for(unsigned int step=0u;good && step<101u;++step) {
            net_poll(s);
            if(arp_lookup(s,next,mac)) { found=true;break; }
            if(now(s)-start>=500u || !s->configured || !s->nic->link(s->nic->context)) { break; }
            good=s->clock.sleep(s->clock.context,10u);
        }
    }
    s->arp_waiting=0u;return good && found;
}
bool net_dhcp(struct net_stack *s)
{
    if(s==NULL || !s->initialized || s->busy) { return false; }
    s->busy=true;s->configured=false;s->lease_expires=0u;
    memset(&s->configuration,0,sizeof(s->configuration));
    memset(s->arp_cache,0,sizeof(s->arp_cache));
    /* Link negotiation after cable restoration is asynchronous. Give it a
     * bounded opportunity to complete before consuming DHCP attempts. */
    const uint64_t link_start=now(s);
    for(unsigned int step=0u;!s->nic->link(s->nic->context);++step) {
        if(step>=201u || now(s)-link_start>=2000u ||
           !s->clock.sleep(s->clock.context,10u)) {
            s->dhcp_state=0u;s->busy=false;return false;
        }
    }
    bool success=false;
    for(unsigned int attempt=0u;attempt<3u && !success;++attempt) {
        s->xid=random_value(s);s->offered_ready=false;s->ack_ready=false;s->dhcp_state=1u;
        unsigned char packet[300];size_t bytes;
        bool good=dhcp_build(packet,sizeof(packet),1u,s->xid,s->nic->mac,NULL,&bytes) &&
            send_udp(s,broadcast,0u,UINT32_MAX,68u,67u,packet,bytes) &&
            wait_flag(s,&s->offered_ready,1000u);
        if(good) {
            s->dhcp_state=2u;
            good=dhcp_build(packet,sizeof(packet),3u,s->xid,s->nic->mac,&s->offered,&bytes) &&
                send_udp(s,broadcast,0u,UINT32_MAX,68u,67u,packet,bytes) &&
                wait_flag(s,&s->ack_ready,1000u);
        }
        if(good) {
            s->configuration=s->offered;
            s->lease_expires=deadline(now(s),(uint64_t)s->configuration.lease_seconds*1000u);
            s->configured=true;success=true;
        }
    }
    s->dhcp_state=0u;s->busy=false;return success;
}
bool net_ping(struct net_stack *s, uint32_t destination, uint64_t *milliseconds)
{
    if(s==NULL || !s->initialized || s->busy || milliseconds==NULL) { return false; }
    s->busy=true;unsigned char mac[6];
    bool good=route(s,destination,mac);
    if(good) {
        unsigned char packet[24]={0};packet[0]=8u;net_put16(packet+4u,0x5554u);
        ++s->echo_sequence;net_put16(packet+6u,s->echo_sequence);
        for(size_t i=0u;i<16u;++i) { s->echo_data[i]=(unsigned char)(i*17u+s->echo_sequence); }
        memcpy(packet+8u,s->echo_data,16u);net_put16(packet+2u,net_checksum(packet,sizeof(packet)));
        s->echo_peer=destination;s->echo_ready=false;
        const uint64_t start=now(s);
        good=send_ip(s,mac,s->configuration.address,destination,1u,packet,sizeof(packet)) &&
            wait_flag(s,&s->echo_ready,1000u);
        if(good) { *milliseconds=now(s)-start; }
    }
    s->echo_peer=0u;s->busy=false;return good;
}
bool net_udp_request(struct net_stack *s, uint32_t destination, uint16_t port,
                      const void *data, size_t bytes, void *reply, size_t capacity, size_t *received)
{
    if(s==NULL || !s->initialized || s->busy || reply==NULL || received==NULL ||
       (bytes!=0u && data==NULL) || bytes>1472u || port==0u) { return false; }
    s->busy=true;unsigned char mac[6];
    bool good=route(s,destination,mac);
    if(good) {
        s->udp_port=(uint16_t)(49152u+(random_value(s)&16383u));
        s->udp_remote_port=port;s->udp_peer=destination;s->udp_ready=false;s->udp_bytes=0u;
        good=send_udp(s,mac,s->configuration.address,destination,s->udp_port,port,data,bytes) &&
            wait_flag(s,&s->udp_ready,1000u);
        if(good && s->udp_bytes<=capacity) {
            memcpy(reply,s->udp_data,s->udp_bytes);*received=s->udp_bytes;
        } else { good=false; }
    }
    s->udp_port=0u;s->udp_peer=0u;s->busy=false;return good;
}
bool net_resolve(struct net_stack *s, const char *name, uint32_t server, uint16_t port, uint32_t *address)
{
    if(s==NULL || !s->initialized || s->busy || address==NULL) { return false; }
    unsigned char query[UTAMO_DNS_MESSAGE_MAX],response[UTAMO_DNS_MESSAGE_MAX];
    size_t bytes,received;
    s->dns_id=(uint16_t)random_value(s);
    if(!dns_build_query(query,sizeof(query),name,s->dns_id,&bytes) ||
       !net_udp_request(s,server,port,query,bytes,response,sizeof(response),&received) ||
       !dns_decode_reply(response,received,name,s->dns_id,address)) { return false; }
    ++s->counters.dns;return true;
}
