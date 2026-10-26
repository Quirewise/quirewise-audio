#include "platform_unicode.h"

#include <stdint.h>

#if defined(QUIREWISE_PLATFORM_UNICODE_C_IMPLEMENTATION)

static size_t unicode_utf8_length(uint32_t codepoint)
{
    if (codepoint <= 0x7fu) return 1u;
    if (codepoint <= 0x7ffu) return 2u;
    if (codepoint <= 0xffffu) return 3u;
    return 4u;
}

static int unicode_next_utf8(const unsigned char *input, size_t *index,
                             uint32_t *out_codepoint)
{
    const unsigned char first = input[*index];
    unsigned char second;
    unsigned char third;
    unsigned char fourth;

    if (first == 0u) return 0;
    if (first <= 0x7fu) {
        *out_codepoint = first;
        ++*index;
        return 1;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        second = input[*index + 1u];
        if (second < 0x80u || second > 0xbfu) return 0;
        *out_codepoint = ((uint32_t)(first & 0x1fu) << 6u) |
                         (uint32_t)(second & 0x3fu);
        *index += 2u;
        return 1;
    }
    if (first >= 0xe0u && first <= 0xefu) {
        second = input[*index + 1u];
        if (second < 0x80u || second > 0xbfu) return 0;
        third = input[*index + 2u];
        if (third < 0x80u || third > 0xbfu ||
            (first == 0xe0u && second < 0xa0u) ||
            (first == 0xedu && second >= 0xa0u)) return 0;
        *out_codepoint = ((uint32_t)(first & 0x0fu) << 12u) |
                         ((uint32_t)(second & 0x3fu) << 6u) |
                         (uint32_t)(third & 0x3fu);
        *index += 3u;
        return 1;
    }
    if (first >= 0xf0u && first <= 0xf4u) {
        second = input[*index + 1u];
        if (second < 0x80u || second > 0xbfu) return 0;
        third = input[*index + 2u];
        if (third < 0x80u || third > 0xbfu) return 0;
        fourth = input[*index + 3u];
        if (fourth < 0x80u || fourth > 0xbfu ||
            (first == 0xf0u && second < 0x90u) ||
            (first == 0xf4u && second > 0x8fu)) return 0;
        *out_codepoint = ((uint32_t)(first & 0x07u) << 18u) |
                         ((uint32_t)(second & 0x3fu) << 12u) |
                         ((uint32_t)(third & 0x3fu) << 6u) |
                         (uint32_t)(fourth & 0x3fu);
        *index += 4u;
        return 1;
    }
    return 0;
}

static int unicode_next_utf16(const uint16_t *input, size_t *index,
                              uint32_t *out_codepoint)
{
    const uint16_t first = input[*index];

    if (first == 0u) return 0;
    ++*index;
    if (first >= 0xd800u && first <= 0xdbffu) {
        const uint16_t second = input[*index];
        if (second < 0xdc00u || second > 0xdfffu) return 0;
        ++*index;
        *out_codepoint = 0x10000u +
            (((uint32_t)first - 0xd800u) << 10u) +
            ((uint32_t)second - 0xdc00u);
        return 1;
    }
    if (first >= 0xdc00u && first <= 0xdfffu) return 0;
    *out_codepoint = first;
    return 1;
}

static ptrdiff_t unicode_measure_utf8(const char *input)
{
    const unsigned char *bytes = (const unsigned char *)input;
    size_t index = 0u;
    size_t total = 1u;

    if (!input) return -1;
    while (bytes[index] != 0u) {
        uint32_t codepoint;
        size_t units;
        if (!unicode_next_utf8(bytes, &index, &codepoint)) return -1;
        units = codepoint <= 0xffffu ? 1u : 2u;
        if (total > (size_t)PTRDIFF_MAX - units) return -3;
        total += units;
    }
    return (ptrdiff_t)total;
}

static ptrdiff_t unicode_measure_utf16(const uint16_t *input)
{
    size_t index = 0u;
    size_t total = 1u;

    if (!input) return -1;
    while (input[index] != 0u) {
        uint32_t codepoint;
        size_t bytes;
        if (!unicode_next_utf16(input, &index, &codepoint)) return -1;
        bytes = unicode_utf8_length(codepoint);
        if (total > (size_t)PTRDIFF_MAX - bytes) return -3;
        total += bytes;
    }
    return (ptrdiff_t)total;
}

static void unicode_write_utf8(char *output, size_t *index,
                               uint32_t codepoint)
{
    unsigned char *bytes = (unsigned char *)output;
    if (codepoint <= 0x7fu) {
        bytes[(*index)++] = (unsigned char)codepoint;
    } else if (codepoint <= 0x7ffu) {
        bytes[(*index)++] = (unsigned char)(0xc0u | (codepoint >> 6u));
        bytes[(*index)++] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    } else if (codepoint <= 0xffffu) {
        bytes[(*index)++] = (unsigned char)(0xe0u | (codepoint >> 12u));
        bytes[(*index)++] = (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        bytes[(*index)++] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    } else {
        bytes[(*index)++] = (unsigned char)(0xf0u | (codepoint >> 18u));
        bytes[(*index)++] = (unsigned char)(0x80u | ((codepoint >> 12u) & 0x3fu));
        bytes[(*index)++] = (unsigned char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        bytes[(*index)++] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    }
}

ptrdiff_t qw_platform_utf16_to_utf8(const uint16_t *input,
                                    char *output,
                                    size_t output_capacity)
{
    ptrdiff_t required = unicode_measure_utf16(input);
    size_t input_index = 0u;
    size_t output_index = 0u;

    if (required < 0 || !output) return required;
    if (output_capacity < (size_t)required) return -2;
    while (input[input_index] != 0u) {
        uint32_t codepoint;
        if (!unicode_next_utf16(input, &input_index, &codepoint)) return -1;
        unicode_write_utf8(output, &output_index, codepoint);
    }
    output[output_index] = '\0';
    return required;
}

ptrdiff_t qw_platform_utf8_to_utf16(const char *input,
                                    uint16_t *output,
                                    size_t output_capacity)
{
    ptrdiff_t required = unicode_measure_utf8(input);
    const unsigned char *bytes = (const unsigned char *)input;
    size_t input_index = 0u;
    size_t output_index = 0u;

    if (required < 0 || !output) return required;
    if (output_capacity < (size_t)required) return -2;
    while (bytes[input_index] != 0u) {
        uint32_t codepoint;
        if (!unicode_next_utf8(bytes, &input_index, &codepoint)) return -1;
        if (codepoint <= 0xffffu) {
            output[output_index++] = (uint16_t)codepoint;
        } else {
            uint32_t adjusted = codepoint - 0x10000u;
            output[output_index++] = (uint16_t)(0xd800u | (adjusted >> 10u));
            output[output_index++] = (uint16_t)(0xdc00u | (adjusted & 0x3ffu));
        }
    }
    output[output_index] = 0u;
    return required;
}

#else

typedef int qw_platform_unicode_c_translation_unit_not_empty;

#endif
