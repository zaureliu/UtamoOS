/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_E1000_RING_H
#define UTAMO_E1000_RING_H
#include <utamo/nic.h>
#define E1000_RING_COUNT 8u
#define E1000_BUFFER_SIZE 2048u
struct e1000_rx_descriptor {
    uint64_t address;
    uint16_t length, checksum;
    uint8_t status, errors;
    uint16_t special;
};
struct e1000_tx_descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset, command, status, checksum_start;
    uint16_t special;
};
_Static_assert(sizeof(struct e1000_rx_descriptor)==16u,"E1000 receive descriptor size");
_Static_assert(sizeof(struct e1000_tx_descriptor)==16u,"E1000 transmit descriptor size");
/* Consume one DD descriptor. A split frame is discarded through its EOP. */
bool e1000_rx_valid(uint16_t length, uint8_t status, uint8_t errors, bool *discarding);
bool e1000_tx_prepare(struct e1000_tx_descriptor *out, uint64_t physical, size_t bytes);
#endif
