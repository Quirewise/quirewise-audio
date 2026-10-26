#include "platform_unicode.h"

#include <stdlib.h>

#if defined(_WIN32)
_Static_assert(sizeof(wchar_t) == sizeof(uint16_t),
               "Windows wchar_t must be UTF-16");

wchar_t *qw_platform_utf8_to_wide(const char *input)
{
    ptrdiff_t required;
    wchar_t *output;

    required = qw_platform_utf8_to_utf16(input, NULL, 0u);
    if (required <= 0 || (size_t)required > SIZE_MAX / sizeof(*output)) {
        return NULL;
    }
    output = (wchar_t *)malloc((size_t)required * sizeof(*output));
    if (!output) return NULL;
    if (qw_platform_utf8_to_utf16(input, (uint16_t *)(void *)output,
                                  (size_t)required) != required) {
        free(output);
        return NULL;
    }
    return output;
}

char *qw_platform_wide_to_utf8(const wchar_t *input)
{
    ptrdiff_t required;
    char *output;

    required = qw_platform_utf16_to_utf8((const uint16_t *)(const void *)input,
                                         NULL, 0u);
    if (required <= 0) return NULL;
    output = (char *)malloc((size_t)required);
    if (!output) return NULL;
    if (qw_platform_utf16_to_utf8((const uint16_t *)(const void *)input,
                                  output, (size_t)required) != required) {
        free(output);
        return NULL;
    }
    return output;
}
#endif
