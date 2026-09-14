/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_NET_SERVICES_H
#define UTAMO_NET_SERVICES_H
#include <utamo/net_packet.h>
#define UTAMO_DNS_NAME_MAX 254u
#define UTAMO_DNS_MESSAGE_MAX 512u
struct dhcp_offer {
    uint32_t address, mask, gateway, dns, server, lease_seconds;
    uint8_t type;
};
bool dhcp_decode(const void *data, size_t bytes, uint32_t xid,
                  const unsigned char mac[6], struct dhcp_offer *out);
bool dhcp_build(void *out, size_t capacity, uint8_t type, uint32_t xid,
                 const unsigned char mac[6], const struct dhcp_offer *offer, size_t *written);
bool dns_name_valid(const char *name);
bool dns_decode_name(const void *message, size_t bytes, size_t offset,
                      char out[UTAMO_DNS_NAME_MAX], size_t *consumed);
bool dns_build_query(void *out, size_t capacity, const char *name, uint16_t id, size_t *written);
bool dns_decode_reply(const void *data, size_t bytes, const char *name, uint16_t id, uint32_t *address);
#endif
