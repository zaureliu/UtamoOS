/* SPDX-License-Identifier: MIT */
#include <utamo/input.h>

void input_ring_init(struct input_ring *ring)
{
    if (ring != NULL) {
        *ring = (struct input_ring){0};
    }
}

void input_ring_discard(struct input_ring *ring)
{
    if (ring != NULL) {
        ring->tail = ring->head;
        ring->reset_pending = true;
    }
}

bool input_ring_push(struct input_ring *ring, uint8_t byte)
{
    if (ring == NULL) {
        return false;
    }
    const size_t next = (ring->head + 1u) % UTAMO_INPUT_RING_CAPACITY;
    if (next == ring->tail) {
        input_ring_discard(ring);
        return false;
    }
    ring->bytes[ring->head] = byte;
    ring->head = next;
    return true;
}

bool input_ring_pop(struct input_ring *ring, uint8_t *byte, bool *reset)
{
    if (ring == NULL || byte == NULL || reset == NULL) {
        return false;
    }
    *reset = ring->reset_pending;
    ring->reset_pending = false;
    if (ring->head == ring->tail) {
        return false;
    }
    *byte = ring->bytes[ring->tail];
    ring->tail = (ring->tail + 1u) % UTAMO_INPUT_RING_CAPACITY;
    return true;
}

bool input_ring_has_pending(const struct input_ring *ring)
{
    return ring != NULL &&
           (ring->head != ring->tail || ring->reset_pending);
}

void input_decoder_init(struct input_decoder *decoder)
{
    if (decoder != NULL) {
        *decoder = (struct input_decoder){0};
    }
}

bool input_decode_set1(struct input_decoder *decoder, uint8_t byte,
                       char *character)
{
    static const char plain[128] = {
        [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
        [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
        [0x0e] = '\b', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w',
        [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
        [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
        [0x1a] = '[', [0x1b] = ']', [0x1c] = '\n', [0x1e] = 'a',
        [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
        [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
        [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2b] = '\\',
        [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
        [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
        [0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' '
    };
    static const char shifted[128] = {
        [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
        [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
        [0x1a] = '{', [0x1b] = '}', [0x27] = ':', [0x28] = '"',
        [0x29] = '~', [0x2b] = '|', [0x33] = '<', [0x34] = '>',
        [0x35] = '?'
    };
    if (decoder == NULL || character == NULL) {
        return false;
    }
    if (decoder->pause_remaining != 0u) {
        --decoder->pause_remaining;
        return false;
    }
    if (byte == 0xe1u) {
        decoder->pause_remaining = 5u;
        decoder->extended = false;
        return false;
    }
    if (byte == 0xe0u) {
        decoder->extended = true;
        return false;
    }
    const bool released = (byte & 0x80u) != 0u;
    const uint8_t code = byte & 0x7fu;
    if (decoder->extended) {
        decoder->extended = false;
        /* Filter fake Shift from Print Screen, arrows, Ctrl/Alt and keypads. */
        if (!released && (code == 0x1cu || code == 0x35u)) {
            *character = code == 0x1cu ? '\n' : '/';
            return true;
        }
        return false;
    }
    if (code == 0x2au || code == 0x36u) {
        if (code == 0x2au) {
            decoder->left_shift = !released;
        } else {
            decoder->right_shift = !released;
        }
        return false;
    }
    if (code == 0x3au) {
        if (!released && !decoder->caps_down) {
            decoder->caps_lock = !decoder->caps_lock;
        }
        decoder->caps_down = !released;
        return false;
    }
    if (released || plain[code] == '\0') {
        return false;
    }
    const bool shift = decoder->left_shift || decoder->right_shift;
    char result = plain[code];
    if (result >= 'a' && result <= 'z') {
        if (shift != decoder->caps_lock) {
            result = (char)(result - 'a' + 'A');
        }
    } else if (shift && shifted[code] != '\0') {
        result = shifted[code];
    }
    *character = result;
    return true;
}
