/* SPDX-License-Identifier: MIT */
/* Host-only framebuffer memory tests; never linked into utamo-kernel. */
#include <utamo/font.h>
#include <utamo/framebuffer.h>
#include <utamo/terminal.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static unsigned int checks;
static unsigned int failures;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL video line %u: %s\n", line, expression);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

static void fill_bytes(uint8_t *bytes, size_t count, uint8_t value)
{
    for (size_t i = 0u; i < count; ++i) {
        bytes[i] = value;
    }
}

static bool bytes_equal(const uint8_t *left, const uint8_t *right, size_t count)
{
    for (size_t i = 0u; i < count; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

static struct framebuffer_config config_for(void *address, uint64_t width,
                                             uint64_t height, uint64_t pitch,
                                             uint16_t bpp)
{
    return (struct framebuffer_config){
        .address = address, .width = width, .height = height, .pitch = pitch,
        .bpp = bpp, .red_mask_size = 8u, .red_mask_shift = 16u,
        .green_mask_size = 8u, .green_mask_shift = 8u,
        .blue_mask_size = 8u, .blue_mask_shift = 0u
    };
}

static void test_rgb24_pitch_and_clipping(void)
{
    uint8_t storage[18];
    fill_bytes(storage, sizeof(storage), 0xa5u);
    struct framebuffer fb;
    const struct framebuffer_config config = config_for(storage + 1, 2, 2,
                                                        8, 24);
    CHECK(framebuffer_init(&fb, &config));
    CHECK(fb.width == 2u && fb.height == 2u && fb.pitch == 8u);
    CHECK(fb.bytes_per_pixel == 3u);
    framebuffer_put_pixel(&fb, 1, 1, UINT32_C(0x123456));
    static const uint8_t pixel_expected[18] = {
        0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
        0xa5, 0xa5, 0xa5, 0x56, 0x34, 0x12, 0xa5, 0xa5, 0xa5
    };
    CHECK(bytes_equal(storage, pixel_expected, sizeof(storage)));
    framebuffer_put_pixel(&fb, 2, 0, UINT32_C(0xffffff));
    framebuffer_put_pixel(&fb, 0, 2, UINT32_C(0xffffff));
    framebuffer_put_pixel(&fb, SIZE_MAX, SIZE_MAX, UINT32_C(0xffffff));
    framebuffer_put_pixel(NULL, 0, 0, 0);
    framebuffer_clear(NULL, 0);
    CHECK(bytes_equal(storage, pixel_expected, sizeof(storage)));

    framebuffer_clear(&fb, UINT32_C(0xabcdef));
    static const uint8_t clear_expected[18] = {
        0xa5, 0xef, 0xcd, 0xab, 0xef, 0xcd, 0xab, 0xa5, 0xa5,
        0xef, 0xcd, 0xab, 0xef, 0xcd, 0xab, 0xa5, 0xa5, 0xa5
    };
    CHECK(bytes_equal(storage, clear_expected, sizeof(storage)));
    struct framebuffer inert = {0};
    framebuffer_clear(&inert, UINT32_C(0xffffff));
    framebuffer_put_pixel(&inert, 0, 0, UINT32_C(0xffffff));
    CHECK(bytes_equal(storage, clear_expected, sizeof(storage)));
}

static void test_rgb32_and_scaling(void)
{
    uint8_t storage[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    struct framebuffer fb;
    struct framebuffer_config config = config_for(storage + 1, 1, 1, 4, 32);
    config.red_mask_shift = 0u;
    config.blue_mask_shift = 16u;
    CHECK(framebuffer_init(&fb, &config));
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0xff123456));
    static const uint8_t swapped_expected[] = {0xa5, 0x12, 0x34, 0x56, 0, 0xa5};
    CHECK(bytes_equal(storage, swapped_expected, sizeof(storage)));

    config.red_mask_size = 1u;
    config.red_mask_shift = 7u;
    config.green_mask_size = 1u;
    config.green_mask_shift = 3u;
    config.blue_mask_size = 1u;
    config.blue_mask_shift = 0u;
    CHECK(framebuffer_init(&fb, &config));
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0xffffff));
    static const uint8_t one_bit_white[] = {0xa5, 0x89, 0, 0, 0, 0xa5};
    CHECK(bytes_equal(storage, one_bit_white, sizeof(storage)));
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0x7f0000));
    CHECK(storage[1] == 0u);
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0x800000));
    CHECK(storage[1] == 0x80u);

    config.bpp = 24u;
    config.red_mask_size = 5u;
    config.red_mask_shift = 11u;
    config.green_mask_size = 6u;
    config.green_mask_shift = 5u;
    config.blue_mask_size = 5u;
    config.blue_mask_shift = 0u;
    CHECK(framebuffer_init(&fb, &config));
    storage[4] = 0xa5u; /* Now padding: 24-bpp pixel inside a four-byte row. */
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0x808080));
    static const uint8_t scaled_expected[] = {0xa5, 0x10, 0x84, 0, 0xa5, 0xa5};
    CHECK(bytes_equal(storage, scaled_expected, sizeof(storage)));
    framebuffer_clear(&fb, UINT32_C(0xffffff));
    static const uint8_t scaled_white[] = {0xa5, 0xff, 0xff, 0, 0xa5, 0xa5};
    CHECK(bytes_equal(storage, scaled_white, sizeof(storage)));
}

static void expect_rejected(const struct framebuffer_config *config)
{
    struct framebuffer fb = {.initialized = true, .width = 99u};
    const bool accepted = framebuffer_init(&fb, config);
    CHECK(!accepted);
    CHECK(!fb.initialized && fb.address == NULL && fb.width == 0u);
    /* A regression must not make this harness draw through a synthetic base. */
    if (accepted || fb.initialized) {
        return;
    }
    framebuffer_put_pixel(&fb, 0, 0, UINT32_C(0xffffff));
    framebuffer_clear(&fb, UINT32_C(0xffffff));
}

static void test_framebuffer_policy_limits(void)
{
    /*
     * BSS reserves writable virtual storage without initializing its pixels.
     * No drawing/clearing is performed on these large accepted surfaces.
     */
    static uint8_t boundary_storage[UTAMO_FRAMEBUFFER_MAX_BYTES];
    struct framebuffer fb;
    struct framebuffer_config config = config_for(boundary_storage,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION, 1,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION * 4u, 32);
    CHECK(framebuffer_init(&fb, &config));
    CHECK(fb.width == UTAMO_FRAMEBUFFER_MAX_DIMENSION);
    config = config_for(boundary_storage, 1,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION, 4, 32);
    CHECK(framebuffer_init(&fb, &config));
    CHECK(fb.height == UTAMO_FRAMEBUFFER_MAX_DIMENSION);
    config = config_for(boundary_storage, UTAMO_FRAMEBUFFER_MAX_DIMENSION,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION * 4u, 32);
    CHECK(framebuffer_init(&fb, &config));
    CHECK((uint64_t)fb.pitch * fb.height == UTAMO_FRAMEBUFFER_MAX_BYTES);
    config = config_for(boundary_storage, 1, 1,
        UTAMO_FRAMEBUFFER_MAX_BYTES, 32);
    CHECK(framebuffer_init(&fb, &config)); /* Padding counts toward the cap. */
    CHECK(fb.pitch == UTAMO_FRAMEBUFFER_MAX_BYTES);

    config = config_for(boundary_storage, UTAMO_FRAMEBUFFER_MAX_DIMENSION + 1u,
        1, (UTAMO_FRAMEBUFFER_MAX_DIMENSION + 1u) * 4u, 32);
    expect_rejected(&config);
    config = config_for(boundary_storage, 1,
        UTAMO_FRAMEBUFFER_MAX_DIMENSION + 1u, 4, 32);
    expect_rejected(&config);
    config = config_for(boundary_storage, 1, 1,
        UTAMO_FRAMEBUFFER_MAX_BYTES + 1u, 32);
    expect_rejected(&config);
    config = config_for(boundary_storage, 1, 2,
        UTAMO_FRAMEBUFFER_MAX_BYTES / 2u + 1u, 32);
    expect_rejected(&config);
}

static void test_framebuffer_rejections(void)
{
    uint8_t storage[16] = {0};
    const struct framebuffer_config valid = config_for(storage, 1, 1, 4, 32);
    CHECK(!framebuffer_init(NULL, &valid));
    expect_rejected(NULL);
    struct framebuffer_config bad = valid;
    bad.address = NULL;
    expect_rejected(&bad);
    bad = valid; bad.width = 0;
    expect_rejected(&bad);
    bad = valid; bad.height = 0;
    expect_rejected(&bad);
    bad = valid; bad.pitch = 0;
    expect_rejected(&bad);
    bad = valid; bad.pitch = 3;
    expect_rejected(&bad);
    bad = valid; bad.bpp = 16u;
    expect_rejected(&bad);
    bad = valid; bad.red_mask_size = 0u;
    expect_rejected(&bad);
    bad = valid; bad.green_mask_size = 9u;
    expect_rejected(&bad);
    bad = valid; bad.blue_mask_shift = 32u;
    expect_rejected(&bad);
    bad = valid; bad.red_mask_shift = 25u;
    expect_rejected(&bad);
    bad = valid; bad.red_mask_shift = 8u; /* Overlaps green. */
    expect_rejected(&bad);
    bad = valid; bad.bpp = 24u; bad.red_mask_shift = 17u;
    expect_rejected(&bad);
    bad = valid; bad.width = UINT64_MAX;
    expect_rejected(&bad);
    bad = valid; bad.height = UINT64_MAX;
    expect_rejected(&bad);
    bad = valid; bad.height = 2u; bad.pitch = (uint64_t)SIZE_MAX;
    expect_rejected(&bad);
    /* Flat x86 host address, inspected arithmetically and never dereferenced. */
    bad = valid; bad.address = (void *)(UINTPTR_MAX - 1u);
    expect_rejected(&bad);
    static const uint8_t untouched[16] = {0};
    CHECK(bytes_equal(storage, untouched, sizeof(storage)));
}

static void test_font_bounds(void)
{
    for (unsigned int code = 0x20u; code <= 0x7eu; ++code) {
        bool has_ink = false;
        for (size_t row = 0u; row < UTAMO_FONT_HEIGHT; ++row) {
            const uint8_t pixels = font_glyph_row((unsigned char)code, row);
            CHECK(pixels <= 0x1fu);
            has_ink = has_ink || pixels != 0u;
        }
        CHECK(has_ink == (code != 0x20u));
        CHECK(font_glyph_row((unsigned char)code, UTAMO_FONT_HEIGHT) == 0u);
        CHECK(font_glyph_row((unsigned char)code, SIZE_MAX) == 0u);
    }
    for (unsigned int code = 0u; code <= 0xffu; ++code) {
        if (code < 0x20u || code > 0x7eu) {
            for (size_t row = 0u; row < UTAMO_FONT_HEIGHT; ++row) {
                CHECK(font_glyph_row((unsigned char)code, row) ==
                      font_glyph_row((unsigned char)'?', row));
            }
        }
    }
}

#define VIDEO_WIDTH 48u
#define VIDEO_HEIGHT 32u
#define VIDEO_PITCH (VIDEO_WIDTH * 4u + 3u)
#define VIDEO_BYTES (VIDEO_PITCH * VIDEO_HEIGHT)

static bool pixel_is(const uint8_t *surface, size_t x, size_t y, uint32_t rgb)
{
    const uint8_t *pixel = surface + y * VIDEO_PITCH + x * 4u;
    return pixel[0] == (uint8_t)rgb &&
           pixel[1] == (uint8_t)(rgb >> 8u) &&
           pixel[2] == (uint8_t)(rgb >> 16u) && pixel[3] == 0u;
}

static bool rectangle_is(const uint8_t *surface, size_t x_start,
                          size_t y_start, size_t width, size_t height,
                          uint32_t rgb)
{
    for (size_t y = y_start; y < y_start + height; ++y) {
        for (size_t x = x_start; x < x_start + width; ++x) {
            if (!pixel_is(surface, x, y, rgb)) {
                return false;
            }
        }
    }
    return true;
}

static void test_terminal_rendering_and_controls(void)
{
    uint8_t storage[VIDEO_BYTES + 2u];
    fill_bytes(storage, sizeof(storage), 0xa5u);
    uint8_t *surface = storage + 1;
    struct framebuffer fb;
    const struct framebuffer_config config = config_for(surface, VIDEO_WIDTH,
        VIDEO_HEIGHT, VIDEO_PITCH, 32);
    CHECK(framebuffer_init(&fb, &config));
    struct terminal term;
    CHECK(terminal_init(&term, &fb));
    CHECK(term.columns == 6u && term.rows == 2u);
    CHECK(term.cursor_x == 0u && term.cursor_y == 0u);
    CHECK(rectangle_is(surface, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT,
                       UINT32_C(0x101820)));
    terminal_putc(&term, 'A');
    /* Independent fixed landmarks: top apex and surrounding blank pixels. */
    CHECK(pixel_is(surface, 3, 1, UINT32_C(0xe6edf3)));
    CHECK(pixel_is(surface, 3, 2, UINT32_C(0xe6edf3)));
    CHECK(pixel_is(surface, 1, 1, UINT32_C(0x101820)));
    CHECK(pixel_is(surface, 3, 0, UINT32_C(0x101820)));
    terminal_write(&term, "BCDEF");
    CHECK(term.cursor_x == 6u && term.cursor_y == 0u);
    terminal_sink('G', &term);
    CHECK(term.cursor_x == 1u && term.cursor_y == 1u);
    terminal_putc(&term, '\b');
    CHECK(term.cursor_x == 0u && term.cursor_y == 1u);
    CHECK(rectangle_is(surface, 0, 16, 8, 16, term.background));
    terminal_putc(&term, '\b'); /* No movement to the preceding line. */
    CHECK(term.cursor_x == 0u && term.cursor_y == 1u);
    terminal_putc(&term, '\n'); /* Bottom overflow clears the entire surface. */
    CHECK(term.cursor_x == 0u && term.cursor_y == 0u);
    CHECK(rectangle_is(surface, 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, term.background));

    terminal_write(&term, "A\t");
    CHECK(term.cursor_x == 4u && term.cursor_y == 0u);
    terminal_write(&term, "BC");
    terminal_putc(&term, '\t'); /* Pending wrap before a four-column tab. */
    CHECK(term.cursor_x == 4u && term.cursor_y == 1u);
    terminal_putc(&term, '\r');
    CHECK(term.cursor_x == 0u && term.cursor_y == 1u);
    terminal_write(&term, "X\b");
    CHECK(term.cursor_x == 0u && term.cursor_y == 1u);
    CHECK(rectangle_is(surface, 0, 16, 8, 16, term.background));
    terminal_putc(&term, '\x01');
    terminal_putc(&term, '\x7f');
    terminal_write(&term, NULL);
    terminal_putc(NULL, 'X');
    terminal_write(NULL, "ignored");
    terminal_sink('X', NULL);
    CHECK(term.cursor_x == 0u && term.cursor_y == 1u);

    CHECK(storage[0] == 0xa5u && storage[sizeof(storage) - 1u] == 0xa5u);
    for (size_t y = 0u; y < VIDEO_HEIGHT; ++y) {
        for (size_t pad = VIDEO_WIDTH * 4u; pad < VIDEO_PITCH; ++pad) {
            CHECK(surface[y * VIDEO_PITCH + pad] == 0xa5u);
        }
    }
}

static void test_terminal_rejections(void)
{
    uint8_t storage[1024] = {0};
    struct framebuffer fb = {0};
    struct terminal term = {.initialized = true};
    CHECK(!terminal_init(NULL, &fb));
    CHECK(!terminal_init(&term, NULL));
    CHECK(!term.initialized);
    CHECK(!terminal_init(&term, &fb));
    struct framebuffer_config config = config_for(storage, 7, 16, 28, 32);
    CHECK(framebuffer_init(&fb, &config));
    CHECK(!terminal_init(&term, &fb));
    CHECK(!term.initialized);
    config = config_for(storage, 8, 15, 32, 32);
    CHECK(framebuffer_init(&fb, &config));
    CHECK(!terminal_init(&term, &fb));
    CHECK(!term.initialized);
    terminal_putc(&term, 'X');
    terminal_write(&term, "ignored");
    CHECK(term.cursor_x == 0u && term.cursor_y == 0u);
    static const uint8_t untouched[1024] = {0};
    CHECK(bytes_equal(storage, untouched, sizeof(storage)));
}

int main(void)
{
    test_rgb24_pitch_and_clipping();
    test_rgb32_and_scaling();
    test_framebuffer_rejections();
    test_framebuffer_policy_limits();
    test_font_bounds();
    test_terminal_rendering_and_controls();
    test_terminal_rejections();
    (void)printf("UTAMO video host tests: %u checks, %u failures\n", checks,
                 failures);
    return failures == 0u ? 0 : 1;
}
