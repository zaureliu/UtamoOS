/* SPDX-License-Identifier: MIT */
#include <utamo/net_packet.h>
#include <utamo/string.h>
bool net_ip_unicast(uint32_t ip)
{
    const uint32_t first=ip>>24u;
    return first!=0u && first!=127u && first<224u;
}
bool net_mask_valid(uint32_t mask)
{
    const uint32_t inverse=~mask;
    return mask!=0u && inverse>=3u && (inverse&(inverse+1u))==0u;
}
bool net_ip_parse(const char *text, uint32_t *out)
{
    if (text==NULL || out==NULL) { return false; }
    uint32_t value=0u;
    for (unsigned int part=0u;part<4u;++part) {
        unsigned int digits=0u,octet=0u;
        while (*text>='0' && *text<='9') {
            if (++digits>3u) { return false; }
            octet=octet*10u+(unsigned int)(*text-'0');
            if (octet>255u) { return false; }
            ++text;
        }
        if (digits==0u || (part<3u && *text!='.') || (part==3u && *text!='\0')) { return false; }
        value=(value<<8u)|octet;
        if (part<3u) { ++text; }
    }
    *out=value;return true;
}
uint16_t net_checksum(const void *data, size_t bytes)
{
    const unsigned char *p=data;
    uint32_t sum=0u;
    while (bytes>=2u) {
        sum+=net_get16(p);sum=(sum&0xffffu)+(sum>>16u);
        p+=2u;bytes-=2u;
    }
    if (bytes!=0u) { sum+=(uint32_t)*p<<8u; }
    while (sum>>16u) { sum=(sum&0xffffu)+(sum>>16u); }
    return (uint16_t)~sum;
}
bool ipv4_decode(const void *data, size_t bytes, struct ipv4_packet *out)
{
    if (data==NULL || out==NULL || bytes<20u) { return false; }
    const unsigned char *p=data;
    const size_t header=(size_t)(p[0]&15u)*4u,total=net_get16(p+2u);
    if ((p[0]>>4u)!=4u || header!=20u || header>bytes || total<header ||
        total>bytes || total>1500u || p[8]==0u ||
        (net_get16(p+6u)&0xbfffu)!=0u || net_checksum(p,header)!=0u) { return false; }
    *out=(struct ipv4_packet){.source=net_get32(p+12u),.destination=net_get32(p+16u),
        .protocol=p[9],.payload=p+header,.bytes=total-header};
    return true;
}
bool ipv4_build(void *out, size_t capacity, uint32_t source, uint32_t destination,
                 uint8_t protocol, uint16_t id, const void *payload, size_t bytes, size_t *written)
{
    if (out==NULL || written==NULL || (bytes!=0u && payload==NULL) ||
        bytes>1480u || capacity<20u+bytes ||
        (source!=0u && !net_ip_unicast(source)) ||
        (!net_ip_unicast(destination) && destination!=UINT32_MAX)) { return false; }
    unsigned char *p=out;
    memset(p,0,20u);p[0]=0x45u;net_put16(p+2u,(uint16_t)(20u+bytes));
    net_put16(p+4u,id);net_put16(p+6u,0x4000u);p[8]=64u;p[9]=protocol;
    net_put32(p+12u,source);net_put32(p+16u,destination);
    net_put16(p+10u,net_checksum(p,20u));
    if (bytes!=0u) { memcpy(p+20u,payload,bytes); }
    *written=20u+bytes;return true;
}
