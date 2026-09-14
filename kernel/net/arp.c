/* SPDX-License-Identifier: MIT */
#include <utamo/net_packet.h>
#include <utamo/string.h>
bool arp_decode(const void *data, size_t bytes, struct arp_packet *out)
{
    if (data==NULL || out==NULL || bytes<28u) { return false; }
    const unsigned char *p=data;
    const uint16_t operation=net_get16(p+6u);
    const uint32_t sender=net_get32(p+14u),target=net_get32(p+24u);
    if (net_get16(p)!=1u || net_get16(p+2u)!=0x800u || p[4]!=6u || p[5]!=4u ||
        (operation!=1u && operation!=2u) || !net_mac_unicast(p+8u) ||
        (sender!=0u && !net_ip_unicast(sender)) || !net_ip_unicast(target)) { return false; }
    struct arp_packet packet={.operation=operation,.sender_ip=sender,.target_ip=target};
    memcpy(packet.sender_mac,p+8u,6u);memcpy(packet.target_mac,p+18u,6u);
    *out=packet;return true;
}
bool arp_build(void *out, size_t capacity, const struct arp_packet *packet)
{
    if (out==NULL || packet==NULL || capacity<28u ||
        (packet->operation!=1u && packet->operation!=2u) ||
        !net_mac_unicast(packet->sender_mac) ||
        (packet->sender_ip!=0u && !net_ip_unicast(packet->sender_ip)) ||
        !net_ip_unicast(packet->target_ip)) { return false; }
    unsigned char *p=out;
    net_put16(p,1u);net_put16(p+2u,0x800u);p[4]=6u;p[5]=4u;
    net_put16(p+6u,packet->operation);
    memcpy(p+8u,packet->sender_mac,6u);net_put32(p+14u,packet->sender_ip);
    memcpy(p+18u,packet->target_mac,6u);net_put32(p+24u,packet->target_ip);
    return true;
}
