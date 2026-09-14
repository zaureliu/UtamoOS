/* SPDX-License-Identifier: MIT */
#include <utamo/net_packet.h>
#include <utamo/string.h>
static uint16_t checksum(uint32_t source, uint32_t destination, const void *data, size_t bytes)
{
    unsigned char pseudo[1492];
    net_put32(pseudo,source);net_put32(pseudo+4u,destination);
    pseudo[8]=0u;pseudo[9]=17u;net_put16(pseudo+10u,(uint16_t)bytes);
    memcpy(pseudo+12u,data,bytes);
    return net_checksum(pseudo,bytes+12u);
}
bool udp_decode(const struct ipv4_packet *ip, struct udp_packet *out)
{
    if (ip==NULL || out==NULL || ip->payload==NULL || ip->protocol!=17u ||
        ip->bytes<8u || ip->bytes>1480u) { return false; }
    const unsigned char *p=ip->payload;
    const size_t bytes=net_get16(p+4u);
    if (bytes!=ip->bytes || net_get16(p)==0u || net_get16(p+2u)==0u ||
        (net_get16(p+6u)!=0u && checksum(ip->source,ip->destination,p,bytes)!=0u)) {
        return false;
    }
    *out=(struct udp_packet){.source=net_get16(p),.destination=net_get16(p+2u),
        .payload=p+8u,.bytes=bytes-8u};
    return true;
}
bool udp_build(void *out, size_t capacity, uint32_t source_ip, uint32_t destination_ip,
                uint16_t source, uint16_t destination, const void *payload, size_t bytes, size_t *written)
{
    if (out==NULL || written==NULL || (bytes!=0u && payload==NULL) ||
        bytes>1472u || capacity<bytes+8u || source==0u || destination==0u) { return false; }
    unsigned char *p=out;
    net_put16(p,source);net_put16(p+2u,destination);
    net_put16(p+4u,(uint16_t)(bytes+8u));net_put16(p+6u,0u);
    if (bytes!=0u) { memcpy(p+8u,payload,bytes); }
    uint16_t sum=checksum(source_ip,destination_ip,p,bytes+8u);
    if (sum==0u) { sum=0xffffu; }
    net_put16(p+6u,sum);*written=bytes+8u;return true;
}
