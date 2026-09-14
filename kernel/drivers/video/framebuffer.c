/* SPDX-License-Identifier: MIT */
#include <utamo/framebuffer.h>

static bool channel_valid(uint8_t size, uint8_t shift, uint16_t bpp)
{
    return size >= 1u && size <= 8u &&
           (unsigned int)shift + (unsigned int)size <= (unsigned int)bpp;
}

static uint32_t channel_mask(uint8_t size, uint8_t shift)
{
    return ((UINT32_C(1) << size) - UINT32_C(1)) << shift;
}

bool framebuffer_init(struct framebuffer *fb,
                      const struct framebuffer_config *config)
{
    if (fb == NULL) {
        return false;
    }
    *fb = (struct framebuffer){0};

    if (config == NULL || config->address == NULL ||
        config->width == 0u || config->height == 0u ||
        config->width > UTAMO_FRAMEBUFFER_MAX_DIMENSION ||
        config->height > UTAMO_FRAMEBUFFER_MAX_DIMENSION ||
        (config->bpp != 24u && config->bpp != 32u)) {
        return false;
    }
    if (!channel_valid(config->red_mask_size, config->red_mask_shift,
                       config->bpp) ||
        !channel_valid(config->green_mask_size, config->green_mask_shift,
                       config->bpp) ||
        !channel_valid(config->blue_mask_size, config->blue_mask_shift,
                       config->bpp)) {
        return false;
    }

    const uint32_t red = channel_mask(config->red_mask_size,
                                      config->red_mask_shift);
    const uint32_t green = channel_mask(config->green_mask_size,
                                        config->green_mask_shift);
    const uint32_t blue = channel_mask(config->blue_mask_size,
                                       config->blue_mask_shift);
    if ((red & green) != 0u || (red & blue) != 0u || (green & blue) != 0u) {
        return false;
    }

    const uint8_t bytes_per_pixel = (uint8_t)(config->bpp / 8u);
    if (config->width > SIZE_MAX / bytes_per_pixel ||
        config->pitch < config->width * bytes_per_pixel) {
        return false;
    }
    /* pitch is nonzero after the width/product checks. */
    if (config->height > SIZE_MAX / config->pitch) {
        return false;
    }
    const size_t extent = (size_t)(config->pitch * config->height);
    if (extent > UTAMO_FRAMEBUFFER_MAX_BYTES) {
        return false;
    }
    const uintptr_t start = (uintptr_t)config->address;
    if (extent - 1u > UINTPTR_MAX - start) {
        return false;
    }

    fb->address = config->address;
    fb->width = (size_t)config->width;
    fb->height = (size_t)config->height;
    fb->pitch = (size_t)config->pitch;
    fb->bytes_per_pixel = bytes_per_pixel;
    fb->red_mask_size = config->red_mask_size;
    fb->red_mask_shift = config->red_mask_shift;
    fb->green_mask_size = config->green_mask_size;
    fb->green_mask_shift = config->green_mask_shift;
    fb->blue_mask_size = config->blue_mask_size;
    fb->blue_mask_shift = config->blue_mask_shift;
    fb->initialized = true;
    return true;
}

static uint32_t pack_channel(uint32_t component, uint8_t size, uint8_t shift)
{
    const uint32_t maximum = (UINT32_C(1) << size) - UINT32_C(1);
    /* Round 8-bit input into the firmware's channel width. */
    return ((component * maximum + UINT32_C(127)) / UINT32_C(255)) << shift;
}

static uint32_t pack_color(const struct framebuffer *fb, uint32_t rgb)
{
    return pack_channel((rgb >> 16u) & UINT32_C(255),
                        fb->red_mask_size, fb->red_mask_shift) |
           pack_channel((rgb >> 8u) & UINT32_C(255),
                        fb->green_mask_size, fb->green_mask_shift) |
           pack_channel(rgb & UINT32_C(255),
                        fb->blue_mask_size, fb->blue_mask_shift);
}

static void write_pixel(volatile uint8_t *pixel, uint8_t bytes_per_pixel,
                        uint32_t packed)
{
    /* Byte accesses also handle 24-bpp rows and unaligned framebuffer bases. */
    for (uint8_t byte = 0u; byte < bytes_per_pixel; ++byte) {
        pixel[byte] = (uint8_t)(packed >> ((unsigned int)byte * 8u));
    }
}

void framebuffer_put_pixel(struct framebuffer *fb, size_t x, size_t y,
                           uint32_t rgb)
{
    if (fb == NULL || !fb->initialized || x >= fb->width || y >= fb->height) {
        return;
    }
    const size_t offset = y * fb->pitch + x * fb->bytes_per_pixel;
    write_pixel(fb->address + offset, fb->bytes_per_pixel, pack_color(fb, rgb));
}

void framebuffer_clear(struct framebuffer *fb, uint32_t rgb)
{
    if (fb == NULL || !fb->initialized) {
        return;
    }
    const uint32_t packed = pack_color(fb, rgb);
    for (size_t y = 0u; y < fb->height; ++y) {
        volatile uint8_t *row = fb->address + y * fb->pitch;
        for (size_t x = 0u; x < fb->width; ++x) {
            write_pixel(row + x * fb->bytes_per_pixel, fb->bytes_per_pixel,
                        packed);
        }
    }
}
