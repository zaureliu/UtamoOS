/* SPDX-License-Identifier: MIT */
#ifndef UTAMO_FRAMEBUFFER_H
#define UTAMO_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bootstrap work/mapping limits; increase only with corresponding validation. */
#define UTAMO_FRAMEBUFFER_MAX_DIMENSION UINT64_C(8192)
#define UTAMO_FRAMEBUFFER_MAX_BYTES (UINT64_C(256) * 1024u * 1024u)

/* Boot-protocol-neutral description. The caller verifies RGB memory model. */
struct framebuffer_config {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

/* Treat fields as read-only after initialization; all sizes are in bytes/pixels. */
struct framebuffer {
    volatile uint8_t *address;
    size_t width;
    size_t height;
    size_t pitch;
    uint8_t bytes_per_pixel;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    bool initialized;
};

/*
 * Accepts packed little-endian RGB 24/32-bpp surfaces with disjoint 1..8-bit
 * channels, up to 8192 pixels per axis and 256 MiB including row padding.
 * The caller owns a writable mapping covering pitch * height bytes
 * and keeps it mapped throughout use. This API checks arithmetic, not mappings.
 * Failure leaves *fb inert. fb/config must point to valid, distinct objects.
 */
bool framebuffer_init(struct framebuffer *fb,
                      const struct framebuffer_config *config);

/* Colors use 0x00RRGGBB. Out-of-bounds pixels and inert surfaces are ignored. */
void framebuffer_put_pixel(struct framebuffer *fb, size_t x, size_t y,
                           uint32_t rgb);
void framebuffer_clear(struct framebuffer *fb, uint32_t rgb);

#endif
