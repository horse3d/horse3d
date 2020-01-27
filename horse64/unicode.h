#ifndef HORSE3D_UNICODE_H_
#define HORSE3D_UNICODE_H_

#include <stdint.h>

typedef uint32_t unicodechar;

int is_valid_utf8_char(
    const unsigned char *p, int size
);

int get_utf8_codepoint(
    const unsigned char *p, int size,
    unicodechar *out, int *outlen
);

int utf8_char_len(const unsigned char *p);

unicodechar *utf8_to_utf32_ex(
    const char *input,
    int *out_len,
    int surrogatereplaceinvalid,
    int *was_aborted_invalid,
    int *was_aborted_outofmemory
);

unicodechar *utf8_to_utf32(
    const char *input, int *out_len
);

#endif  // HORSE3D_UNICODE_H_
