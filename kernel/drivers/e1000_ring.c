/* SPDX-License-Identifier: MIT */
#include <utamo/e1000_ring.h>
bool e1000_rx_valid(uint16_t length, uint8_t status, uint8_t errors, bool *discarding)
{
    if (discarding == NULL || (status & 1u) == 0u) { return false; }
    const bool fragmented = *discarding || (status & 2u) == 0u;
    *discarding = (status & 2u) == 0u;
    return !fragmented && errors == 0u && length >= 14u &&
        length <= UTAMO_NET_FRAME_MAX && length <= E1000_BUFFER_SIZE;
}
bool e1000_tx_prepare(struct e1000_tx_descriptor *out, uint64_t physical, size_t bytes)
{
    if (out == NULL || (physical & 15u) != 0u || bytes < 60u ||
        bytes > UTAMO_NET_FRAME_MAX || bytes > UINT64_MAX - physical) { return false; }
    *out = (struct e1000_tx_descriptor){
        .address=physical,.length=(uint16_t)bytes,.command=0x0bu /* EOP|IFCS|RS */
    };
    return true;
}
