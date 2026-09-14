/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_INPUT_H
#define UTAMO_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTAMO_INPUT_RING_CAPACITY 128u

/*
 * Pure raw-byte queue. All access must be serialized: IRQ producer and main
 * consumer use IF=0 on our single CPU. No volatile or lock-free/SMP claim.
 * One slot remains empty. Overflow drops the whole damaged sequence; the next
 * pop reports reset=true so held modifiers cannot remain stuck indefinitely.
 */
struct input_ring {
    uint8_t bytes[UTAMO_INPUT_RING_CAPACITY];
    size_t head;
    size_t tail;
    bool reset_pending;
};

void input_ring_init(struct input_ring *ring);
bool input_ring_push(struct input_ring *ring, uint8_t byte);
bool input_ring_pop(struct input_ring *ring, uint8_t *byte, bool *reset);
bool input_ring_has_pending(const struct input_ring *ring);
void input_ring_discard(struct input_ring *ring);

/* US ASCII subset, translated from hardware set 1. Caller owns the state. */
struct input_decoder {
    bool left_shift;
    bool right_shift;
    bool caps_lock;
    bool caps_down;
    bool extended;
    uint8_t pause_remaining;
};

void input_decoder_init(struct input_decoder *decoder);
/* False means prefix, release, modifier, unsupported key or invalid argument. */
bool input_decode_set1(struct input_decoder *decoder, uint8_t byte,
                       char *character);

#endif
