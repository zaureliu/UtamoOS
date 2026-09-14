/* SPDX-License-Identifier: MIT */
#include <utamo/net_services.h>
#include <utamo/string.h>
bool dhcp_decode(const void *data, size_t bytes, uint32_t xid,
                  const unsigned char mac[6], struct dhcp_offer *out)
{
    if (data==NULL || out==NULL || mac==NULL || bytes<240u || bytes>1472u) { return false; }
    const unsigned char *p=data;
    if (p[0]!=2u || p[1]!=1u || p[2]!=6u || p[3]!=0u || net_get32(p+4u)!=xid ||
        memcmp(p+28u,mac,6u)!=0 || net_get32(p+12u)!=0u || net_get32(p+24u)!=0u ||
        net_get32(p+236u)!=UINT32_C(0x63825363)) { return false; }
    struct dhcp_offer offer={.address=net_get32(p+16u)};
    unsigned int seen=0u;
    bool ended=false;
    for (size_t offset=240u;offset<bytes;) {
        const unsigned char code=p[offset++];
        if (code==0u) { continue; }
        if (code==255u) { ended=true;break; }
        if (offset==bytes) { return false; }
        const size_t length=p[offset++];
        if (length>bytes-offset) { return false; }
        unsigned int bit=0u;
        switch (code) {
        case 1u:
            bit=1u;if(length!=4u){return false;}offer.mask=net_get32(p+offset);break;
        case 3u:
            bit=2u;if(length<4u || (length&3u)!=0u){return false;}offer.gateway=net_get32(p+offset);break;
        case 6u:
            bit=4u;if(length<4u || (length&3u)!=0u){return false;}offer.dns=net_get32(p+offset);break;
        case 51u:
            bit=8u;if(length!=4u){return false;}offer.lease_seconds=net_get32(p+offset);break;
        case 53u:
            bit=16u;if(length!=1u){return false;}offer.type=p[offset];break;
        case 54u:
            bit=32u;if(length!=4u){return false;}offer.server=net_get32(p+offset);break;
        case 52u: return false; /* BOOTP field overload is outside this client. */
        default: break;
        }
        if (bit!=0u && (seen&bit)!=0u) { return false; }
        seen|=bit;offset+=length;
    }
    if (!ended || seen!=63u || (offer.type!=2u && offer.type!=5u) ||
        !net_ip_unicast(offer.address) || !net_mask_valid(offer.mask) ||
        !net_ip_unicast(offer.gateway) || !net_ip_unicast(offer.dns) ||
        !net_ip_unicast(offer.server) || offer.lease_seconds==0u ||
        (offer.address&offer.mask)!=(offer.gateway&offer.mask) ||
        (offer.address&~offer.mask)==0u || (offer.address&~offer.mask)==~offer.mask ||
        (offer.gateway&~offer.mask)==0u || (offer.gateway&~offer.mask)==~offer.mask ||
        offer.address==offer.gateway || offer.address==offer.server ||
        offer.address==offer.dns) { return false; }
    const uint32_t endpoints[]={offer.dns,offer.server};
    for(size_t i=0u;i<2u;++i) {
        if((endpoints[i]&offer.mask)==(offer.address&offer.mask) &&
           ((endpoints[i]&~offer.mask)==0u || (endpoints[i]&~offer.mask)==~offer.mask)) {
            return false;
        }
    }
    *out=offer;return true;
}
bool dhcp_build(void *out, size_t capacity, uint8_t type, uint32_t xid,
                 const unsigned char mac[6], const struct dhcp_offer *offer, size_t *written)
{
    if (out==NULL || written==NULL || !net_mac_unicast(mac) || capacity<300u ||
        (type!=1u && type!=3u) || (type==3u && (offer==NULL ||
        !net_ip_unicast(offer->address) || !net_ip_unicast(offer->server)))) { return false; }
    unsigned char *p=out;memset(p,0,300u);
    p[0]=1u;p[1]=1u;p[2]=6u;net_put32(p+4u,xid);net_put16(p+10u,0x8000u);
    memcpy(p+28u,mac,6u);net_put32(p+236u,UINT32_C(0x63825363));
    size_t at=240u;
    p[at++]=53u;p[at++]=1u;p[at++]=type;
    p[at++]=61u;p[at++]=7u;p[at++]=1u;memcpy(p+at,mac,6u);at+=6u;
    p[at++]=55u;p[at++]=5u;
    p[at++]=1u;p[at++]=3u;p[at++]=6u;p[at++]=51u;p[at++]=54u;
    p[at++]=57u;p[at++]=2u;net_put16(p+at,576u);at+=2u;
    if(type==3u) {
        p[at++]=50u;p[at++]=4u;net_put32(p+at,offer->address);at+=4u;
        p[at++]=54u;p[at++]=4u;net_put32(p+at,offer->server);at+=4u;
    }
    p[at]=255u;*written=300u;return true;
}
