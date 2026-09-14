/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_NET_PACKET_H
#define UTAMO_NET_PACKET_H
#include <utamo/nic.h>
/* Callers establish byte bounds before these endian accessors. IP values are
 * numeric network-order values: 10.0.2.15 == 0x0a00020f, not host byte arrays. */
static inline uint16_t net_get16(const unsigned char *p)
{
    return (uint16_t)((uint16_t)((uint16_t)p[0]<<8u)|(uint16_t)p[1]);
}
static inline uint32_t net_get32(const unsigned char *p)
{
    return ((uint32_t)net_get16(p)<<16u)|(uint32_t)net_get16(p+2u);
}
static inline void net_put16(unsigned char *p, uint16_t value)
{
    p[0]=(unsigned char)(value>>8u);p[1]=(unsigned char)value;
}
static inline void net_put32(unsigned char *p, uint32_t value)
{
    net_put16(p,(uint16_t)(value>>16u));net_put16(p+2u,(uint16_t)value);
}
struct ethernet_packet {
    const unsigned char *destination, *source, *payload;
    size_t bytes;
    uint16_t type;
};
struct arp_packet {
    uint16_t operation;
    unsigned char sender_mac[6], target_mac[6];
    uint32_t sender_ip, target_ip;
};
struct ipv4_packet {
    uint32_t source, destination;
    uint8_t protocol;
    const unsigned char *payload;
    size_t bytes;
};
struct udp_packet {
    uint16_t source, destination;
    const unsigned char *payload;
    size_t bytes;
};
bool net_mac_unicast(const unsigned char mac[6]);
bool net_ip_unicast(uint32_t ip);
bool net_mask_valid(uint32_t mask);
bool net_ip_parse(const char *text, uint32_t *out);
uint16_t net_checksum(const void *data, size_t bytes);
bool ethernet_decode(const void *data, size_t bytes, struct ethernet_packet *out);
bool arp_decode(const void *data, size_t bytes, struct arp_packet *out);
bool ipv4_decode(const void *data, size_t bytes, struct ipv4_packet *out);
bool udp_decode(const struct ipv4_packet *ip, struct udp_packet *out);
bool icmp_echo_decode(const void *data, size_t bytes, bool *reply, uint16_t *id, uint16_t *sequence);
/* Trusted output storage; builders preserve output on invalid arguments. */
bool ethernet_build(void *out, size_t capacity, const unsigned char source[6],
                     const unsigned char destination[6], uint16_t type,
                     const void *payload, size_t bytes, size_t *written);
bool arp_build(void *out, size_t capacity, const struct arp_packet *packet);
bool ipv4_build(void *out, size_t capacity, uint32_t source, uint32_t destination,
                 uint8_t protocol, uint16_t id, const void *payload, size_t bytes, size_t *written);
bool udp_build(void *out, size_t capacity, uint32_t source_ip, uint32_t destination_ip,
                uint16_t source, uint16_t destination, const void *payload, size_t bytes, size_t *written);
#endif
