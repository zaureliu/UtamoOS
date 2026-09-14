/* SPDX-License-Identifier: MIT */
#include <utamo/net_packet.h>
bool icmp_echo_decode(const void *data, size_t bytes, bool *reply, uint16_t *id, uint16_t *sequence)
{
    if (data==NULL || reply==NULL || id==NULL || sequence==NULL ||
        bytes<8u || bytes>1480u) { return false; }
    const unsigned char *p=data;
    if ((p[0]!=0u && p[0]!=8u) || p[1]!=0u || net_checksum(p,bytes)!=0u) { return false; }
    *reply=p[0]==0u;*id=net_get16(p+4u);*sequence=net_get16(p+6u);
    return true;
}
