/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_NIC_H
#define UTAMO_NIC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UTAMO_NET_FRAME_MAX 1514u
struct nic_device {
    unsigned char mac[6];
    uint16_t mtu;
    void *context;
    bool (*transmit)(void *context, const void *frame, size_t bytes);
    size_t (*receive)(void *context, void *frame, size_t capacity);
    bool (*link)(void *context);
};
struct nic_stats {
    uint64_t transmitted, received, dropped, errors, timeouts, dma_physical;
    uint32_t dma_pages;
    bool ready, link, quarantined;
};
int e1000_init(void); /* Once, boot thread IF=1: 0=absent, 1=ready, -1=rejected. */
const struct nic_device *e1000_device(void);
void e1000_stats(struct nic_stats *out);
#endif
