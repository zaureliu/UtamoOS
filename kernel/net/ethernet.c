/* SPDX-License-Identifier: MIT */
#include <utamo/net_packet.h>
#include <utamo/string.h>
bool net_mac_unicast(const unsigned char mac[6])
{
    if (mac==NULL || (mac[0]&1u)!=0u) { return false; }
    unsigned int bits=0u;
    for (size_t i=0u;i<6u;++i) { bits|=mac[i]; }
    return bits!=0u;
}
bool ethernet_decode(const void *data, size_t bytes, struct ethernet_packet *out)
{
    if (data==NULL || out==NULL || bytes<14u || bytes>UTAMO_NET_FRAME_MAX) { return false; }
    const unsigned char *p=data;
    if (!net_mac_unicast(p+6u)) { return false; }
    *out=(struct ethernet_packet){.destination=p,.source=p+6u,.type=net_get16(p+12u),
        .payload=p+14u,.bytes=bytes-14u};
    return true;
}
bool ethernet_build(void *out, size_t capacity, const unsigned char source[6],
                     const unsigned char destination[6], uint16_t type,
                     const void *payload, size_t bytes, size_t *written)
{
    if (out==NULL || written==NULL || !net_mac_unicast(source) || destination==NULL ||
        (bytes!=0u && payload==NULL) || bytes>1500u || capacity<bytes+14u) { return false; }
    unsigned char *p=out;
    memcpy(p,destination,6u);memcpy(p+6u,source,6u);net_put16(p+12u,type);
    if (bytes!=0u) { memcpy(p+14u,payload,bytes); }
    *written=bytes+14u;
    return true;
}
