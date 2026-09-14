/* SPDX-License-Identifier: MIT */
/* Host-only: pure input and shell logic, no privileged instruction or I/O. */
#include <utamo/input.h>
#include <utamo/shell_line.h>
#include <utamo/string.h>

#include <stdio.h>
#include <stdint.h>

static unsigned int checks;
static unsigned int failures;

static void check(bool condition, const char *expression, unsigned int line)
{
    ++checks;
    if (!condition) {
        ++failures;
        (void)printf("FAIL line %u: %s\n", line, expression);
    }
}
#define CHECK(expression) check((expression), #expression, __LINE__)

static void test_ring(void)
{
    struct input_ring ring;
    input_ring_init(&ring);
    uint8_t byte = 99u;
    bool reset = true;
    CHECK(!input_ring_has_pending(&ring));
    CHECK(!input_ring_pop(&ring, &byte, &reset));
    CHECK(!reset && byte == 99u);
    for (unsigned int pass = 0u; pass < 3u; ++pass) {
        for (size_t i = 0u; i < UTAMO_INPUT_RING_CAPACITY - 1u; ++i) {
            CHECK(input_ring_push(&ring, (uint8_t)i));
        }
        CHECK(input_ring_has_pending(&ring));
        for (size_t i = 0u; i < UTAMO_INPUT_RING_CAPACITY - 1u; ++i) {
            CHECK(input_ring_pop(&ring, &byte, &reset));
            CHECK(byte == i && !reset);
        }
        CHECK(!input_ring_has_pending(&ring));
    }
    for (size_t i = 0u; i < UTAMO_INPUT_RING_CAPACITY - 1u; ++i) {
        CHECK(input_ring_push(&ring, 0x2au));
    }
    CHECK(!input_ring_push(&ring, 0xaau));
    CHECK(input_ring_has_pending(&ring));
    CHECK(!input_ring_pop(&ring, &byte, &reset));
    CHECK(reset && !input_ring_has_pending(&ring));
    CHECK(input_ring_push(&ring, 0x1eu));
    CHECK(input_ring_pop(&ring, &byte, &reset));
    CHECK(byte == 0x1eu && !reset);
    CHECK(input_ring_push(&ring, 0xe0u));
    input_ring_discard(&ring);
    CHECK(input_ring_push(&ring, 0x30u));
    CHECK(input_ring_pop(&ring, &byte, &reset));
    CHECK(byte == 0x30u && reset);
    CHECK(!input_ring_pop(NULL, &byte, &reset));
    CHECK(!input_ring_pop(&ring, NULL, &reset));
    CHECK(!input_ring_pop(&ring, &byte, NULL));
    CHECK(!input_ring_push(NULL, 0u));
    CHECK(!input_ring_has_pending(NULL));
    input_ring_init(NULL);
    input_ring_discard(NULL);
}

static void test_scancodes(void)
{
    struct input_decoder decoder;
    input_decoder_init(&decoder);
    char output = '?';
    CHECK(input_decode_set1(&decoder, 0x1eu, &output) && output == 'a');
    CHECK(!input_decode_set1(&decoder, 0x9eu, &output));
    CHECK(!input_decode_set1(&decoder, 0x2au, &output));
    CHECK(input_decode_set1(&decoder, 0x1eu, &output) && output == 'A');
    CHECK(!input_decode_set1(&decoder, 0x36u, &output));
    CHECK(!input_decode_set1(&decoder, 0xaau, &output));
    CHECK(input_decode_set1(&decoder, 0x02u, &output) && output == '!');
    CHECK(!input_decode_set1(&decoder, 0xb6u, &output));
    CHECK(input_decode_set1(&decoder, 0x02u, &output) && output == '1');
    CHECK(!input_decode_set1(&decoder, 0x3au, &output));
    CHECK(!input_decode_set1(&decoder, 0x3au, &output)); /* Repeat is not toggle. */
    CHECK(input_decode_set1(&decoder, 0x30u, &output) && output == 'B');
    CHECK(input_decode_set1(&decoder, 0x02u, &output) && output == '1');
    CHECK(!input_decode_set1(&decoder, 0x2au, &output));
    CHECK(input_decode_set1(&decoder, 0x30u, &output) && output == 'b');
    CHECK(!input_decode_set1(&decoder, 0xaau, &output));
    CHECK(!input_decode_set1(&decoder, 0xbau, &output));
    CHECK(!input_decode_set1(&decoder, 0x3au, &output));
    CHECK(input_decode_set1(&decoder, 0x30u, &output) && output == 'b');

    static const uint8_t plain_codes[] = {
        0x03u, 0x0bu, 0x0cu, 0x0du, 0x1au, 0x1bu, 0x27u, 0x28u,
        0x29u, 0x2bu, 0x33u, 0x34u, 0x35u, 0x39u, 0x0eu, 0x1cu
    };
    static const char expected[] = "20-=[];'`\\,./ \b\n";
    CHECK(sizeof(plain_codes) == sizeof(expected) - 1u);
    for (size_t i = 0u; i < sizeof(plain_codes); ++i) {
        CHECK(input_decode_set1(&decoder, plain_codes[i], &output));
        CHECK(output == expected[i]);
    }
    CHECK(!input_decode_set1(&decoder, 0x2au, &output));
    static const char shifted[] = "@)_+{}:\"~|<>? \b\n";
    for (size_t i = 0u; i < sizeof(plain_codes); ++i) {
        CHECK(input_decode_set1(&decoder, plain_codes[i], &output));
        CHECK(output == shifted[i]);
    }
    input_decoder_init(&decoder);
    const uint8_t special[] = {
        0xe0u, 0x2au, 0xe0u, 0x37u, /* PrintScreen fake shift. */
        0xe0u, 0xb7u, 0xe0u, 0xaau,
        0xe1u, 0x1du, 0x45u, 0xe1u, 0x9du, 0xc5u, /* Pause. */
        0xe0u, 0x48u, 0xe0u, 0xc8u /* Arrow up. */
    };
    for (size_t i = 0u; i < sizeof(special); ++i) {
        CHECK(!input_decode_set1(&decoder, special[i], &output));
    }
    CHECK(input_decode_set1(&decoder, 0x1eu, &output) && output == 'a');
    CHECK(!decoder.left_shift && !decoder.right_shift);
    CHECK(!input_decode_set1(&decoder, 0xe0u, &output));
    CHECK(input_decode_set1(&decoder, 0x1cu, &output) && output == '\n');
    CHECK(!input_decode_set1(&decoder, 0xe0u, &output));
    CHECK(!input_decode_set1(&decoder, 0x9cu, &output));
    CHECK(!input_decode_set1(&decoder, 0xe0u, &output));
    CHECK(input_decode_set1(&decoder, 0x35u, &output) && output == '/');
    CHECK(!input_decode_set1(&decoder, 0xffu, &output));
    CHECK(!input_decode_set1(NULL, 0x1eu, &output));
    CHECK(!input_decode_set1(&decoder, 0x1eu, NULL));
    input_decoder_init(NULL);
    /* Broken sequence recovery is the ring consumer's explicit responsibility. */
    CHECK(!input_decode_set1(&decoder, 0xe1u, &output));
    input_decoder_init(&decoder);
    CHECK(input_decode_set1(&decoder, 0x1eu, &output) && output == 'a');
}

static void test_line(void)
{
    struct shell_line line;
    shell_line_init(&line, 3u);
    CHECK(line.limit == 3u && line.length == 0u && line.bytes[0] == '\0');
    CHECK(shell_line_feed(&line, '\b') == UTAMO_SHELL_IGNORED);
    CHECK(shell_line_feed(&line, '\n') == UTAMO_SHELL_SUBMITTED);
    CHECK(shell_line_feed(&line, 'a') == UTAMO_SHELL_APPENDED);
    CHECK(shell_line_feed(&line, '\t') == UTAMO_SHELL_APPENDED);
    CHECK(shell_line_feed(&line, 'b') == UTAMO_SHELL_APPENDED);
    CHECK(line.bytes[0] == 'a' && line.bytes[1] == ' ' &&
          line.bytes[2] == 'b' && line.bytes[3] == '\0');
    CHECK(shell_line_feed(&line, 'X') == UTAMO_SHELL_FULL);
    CHECK(line.length == 3u && line.bytes[2] == 'b' && line.bytes[3] == '\0');
    CHECK(shell_line_feed(&line, '\b') == UTAMO_SHELL_ERASED);
    CHECK(line.length == 2u && line.bytes[2] == '\0');
    CHECK(shell_line_feed(&line, 'c') == UTAMO_SHELL_APPENDED);
    CHECK(shell_line_feed(&line, '\r') == UTAMO_SHELL_SUBMITTED);
    CHECK(line.length == 3u && line.bytes[2] == 'c');
    CHECK(shell_line_feed(&line, (char)0xff) == UTAMO_SHELL_IGNORED);
    CHECK(shell_line_feed(&line, '\x01') == UTAMO_SHELL_IGNORED);
    shell_line_init(&line, SIZE_MAX);
    CHECK(line.limit == UTAMO_SHELL_LINE_CAPACITY - 1u);
    for (size_t i = 0u; i < UTAMO_SHELL_LINE_CAPACITY - 1u; ++i) {
        CHECK(shell_line_feed(&line, 'z') == UTAMO_SHELL_APPENDED);
    }
    CHECK(shell_line_feed(&line, 'X') == UTAMO_SHELL_FULL);
    CHECK(line.bytes[UTAMO_SHELL_LINE_CAPACITY - 1u] == '\0');
    for (size_t i = 0u; i < UTAMO_SHELL_LINE_CAPACITY - 1u; ++i) {
        CHECK(shell_line_feed(&line, '\b') == UTAMO_SHELL_ERASED);
    }
    CHECK(shell_line_feed(&line, '\b') == UTAMO_SHELL_IGNORED);
    shell_line_init(&line, 0u);
    CHECK(shell_line_feed(&line, 'a') == UTAMO_SHELL_FULL);
    CHECK(shell_line_feed(&line, '\n') == UTAMO_SHELL_SUBMITTED);
    CHECK(shell_line_feed(NULL, 'a') == UTAMO_SHELL_IGNORED);
    shell_line_init(NULL, 3u);
}

static void test_parser(void)
{
    struct shell_command command;
    char input[] = "  \techo\t hello  world  \t";
    CHECK(shell_parse_line(input, &command));
    CHECK(strcmp(command.name, "echo") == 0);
    CHECK(strcmp(command.arguments, "hello  world") == 0);
    char *cursor = command.arguments;
    char *token = shell_next_token(&cursor);
    CHECK(token != NULL && strcmp(token, "hello") == 0);
    token = shell_next_token(&cursor);
    CHECK(token != NULL && strcmp(token, "world") == 0);
    CHECK(shell_next_token(&cursor) == NULL);
    CHECK(shell_next_token(&cursor) == NULL);
    char no_args[] = "version";
    CHECK(shell_parse_line(no_args, &command));
    CHECK(strcmp(command.name, "version") == 0 && *command.arguments == '\0');
    char empty[] = " \t ";
    CHECK(!shell_parse_line(empty, &command));
    CHECK(command.name == NULL && command.arguments == NULL);
    char blank[] = "";
    CHECK(!shell_parse_line(blank, &command));
    CHECK(!shell_parse_line(NULL, &command));
    CHECK(!shell_parse_line(no_args, NULL));
    cursor = NULL;
    CHECK(shell_next_token(&cursor) == NULL);
    CHECK(shell_next_token(NULL) == NULL);
    char arguments[] = " ud2\t extra\t";
    cursor = arguments;
    token = shell_next_token(&cursor);
    CHECK(token != NULL && strcmp(token, "ud2") == 0);
    token = shell_next_token(&cursor);
    CHECK(token != NULL && strcmp(token, "extra") == 0);
    CHECK(shell_next_token(&cursor) == NULL);
}

int main(void)
{
    test_ring();
    test_scancodes();
    test_line();
    test_parser();
    (void)printf("UTAMO input/shell host tests: %u checks, %u failures\n",
                 checks, failures);
    return failures == 0u ? 0 : 1;
}
