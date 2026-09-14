/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_NET_H
#define UTAMO_NET_H
#include <utamo/net_services.h>
struct net_clock {
    void *context;
    uint64_t (*milliseconds)(void *context);
    bool (*sleep)(void *context, uint64_t milliseconds);
};
struct net_arp_entry { uint32_t ip; unsigned char mac[6]; uint64_t expires; };
struct net_counters {
    uint64_t frames, malformed, ignored, arp, ipv4, icmp, udp, dhcp, dns, echo_replies;
};
struct net_stack {
    const struct nic_device *nic;
    struct net_clock clock;
    struct dhcp_offer configuration, offered;
    struct net_arp_entry arp_cache[8];
    struct net_counters counters;
    uint64_t lease_expires;
    uint32_t random, xid, arp_waiting, echo_peer, udp_peer;
    uint16_t ip_id, echo_sequence, udp_port, udp_remote_port, dns_id;
    uint8_t dhcp_state;
    bool initialized, configured, busy, polling, offered_ready, ack_ready, echo_ready, udp_ready;
    unsigned char echo_data[16], udp_data[1472];
    size_t udp_bytes;
};
/* Single owning boot/kernel thread, IF=1; no IRQ/NMI or concurrent consumers.
 * Blocking operations use bounded polling with the supplied yielding clock.
 * No heap allocation, background thread, socket syscalls or public network use. */
bool net_stack_init(struct net_stack *stack, const struct nic_device *nic, const struct net_clock *clock);
void net_poll(struct net_stack *stack);
bool net_dhcp(struct net_stack *stack);
bool net_ping(struct net_stack *stack, uint32_t destination, uint64_t *milliseconds);
bool net_udp_request(struct net_stack *stack, uint32_t destination, uint16_t port,
                      const void *data, size_t bytes, void *reply, size_t capacity, size_t *received);
bool net_resolve(struct net_stack *stack, const char *name, uint32_t server, uint16_t port, uint32_t *address);
#endif
