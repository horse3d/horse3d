
#include <assert.h>
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unicode.h"

static int is_utf8_start(uint8_t c) {
    if ((int)(c & 0xE0) == (int)0xC0) {  // 110xxxxx
        return 1;
    } else if ((int)(c & 0xF0) == (int)0xE0) {  // 1110xxxx
        return 1;
    } else if ((int)(c & 0xF8) == (int)0xF0) {  // 11110xxx
        return 1;
    }
    return 0;
}

int utf8_char_len(const unsigned char *p) {
    if ((int)((*p) & 0xE0) == (int)0xC0)
        return 2;
    if ((int)((*p) & 0xF0) == (int)0xE0)
        return 3;
    if ((int)((*p) & 0xF8) == (int)0xF0)
        return 4;
    return 1;
}

int get_utf8_codepoint(
        const unsigned char *p, int size,
        unicodechar *out, int *outlen
        ) {
    if (size < 1)
        return 0;
    if (!is_utf8_start(*p)) {
        if (*p > 127)
            return 0;
        if (out) *out = (unicodechar)(*p);
        if (outlen) *outlen = 1;
        return 1;
    }
    uint8_t c = (*(uint8_t*)p);
    if ((int)(c & 0xE0) == (int)0xC0 && size >= 2) {  // p[0] == 110xxxxx
        if ((int)(*(p + 1) & 0xC0) != (int)0x80) { // p[1] != 10xxxxxx
            return 0;
        }
        if (size >= 3 &&
                (int)(*(p + 2) & 0xC0) == (int)0x80) { // p[2] == 10xxxxxx
            return 0;
        }
        unicodechar c = (   // 00011111 of first byte
            (unicodechar)(*p) & (unicodechar)0x1FULL
        ) << (unicodechar)6ULL;
        c += (  // 00111111 of second byte
            (unicodechar)(*(p + 1)) & (unicodechar)0x3FULL
        );
        if (c <= 127ULL)
            return 0;  // not valid to be encoded with two bytes.
        if (out) *out = c;
        if (outlen) *outlen = 2;
        return 1;
    }
    if ((int)(c & 0xF0) == (int)0xE0 && size >= 3) {  // p[0] == 1110xxxx
        if ((int)(*(p + 1) & 0xC0) != (int)0x80) { // p[1] != 10xxxxxx
            return 0;
        }
        if ((int)(*(p + 2) & 0xC0) != (int)0x80) { // p[2] != 10xxxxxx
            return 0;
        }
        if (size >= 4 &&
                (int)(*(p + 3) & 0xC0) == (int)0x80) { // p[3] == 10xxxxxx
            return 0;
        }
        unicodechar c = (   // 00011111 of first byte
            (unicodechar)(*p) & (unicodechar)0x1FULL
        ) << (unicodechar)12ULL;
        c += (  // 00111111 of second byte
            (unicodechar)(*(p + 1)) & (unicodechar)0x3FULL
        ) << (unicodechar)6ULL;
        c += (  // 00111111 of third byte
            (unicodechar)(*(p + 2)) & (unicodechar)0x3FULL
        );
        if (c <= 0x7FFULL)
            return 0;  // not valid to be encoded with three bytes.
        if (c >= 0xD800ULL && c <= 0xDFFFULL) {
            // UTF-16 surrogate code points may not be used in UTF-8
            // (in part because we re-use them to store invalid bytes)
            return 0;
        }
        if (out) *out = c;
        if (outlen) *outlen = 3;
        return 1;
    }
    if ((int)(c & 0xF8) == (int)0xF0 && size >= 4) {  // p[0] == 11110xxx
        if ((int)(*(p + 1) & 0xC0) != (int)0x80) { // p[1] != 10xxxxxx
            return 0;
        }
        if ((int)(*(p + 2) & 0xC0) != (int)0x80) { // p[2] != 10xxxxxx
            return 0;
        }
        if ((int)(*(p + 3) & 0xC0) != (int)0x80) { // p[3] != 10xxxxxx
            return 0;
        }
        if (size >= 5 &&
                (int)(*(p + 4) & 0xC0) == (int)0x80) { // p[4] == 10xxxxxx
            return 0;
        }
        unicodechar c = (   // 00011111 of first byte
            (unicodechar)(*p) & (unicodechar)0x1FULL
        ) << (unicodechar)18ULL;
        c += (  // 00111111 of second byte
            (unicodechar)(*(p + 1)) & (unicodechar)0x3FULL
        ) << (unicodechar)12ULL;
        c += (  // 00111111 of third byte
            (unicodechar)(*(p + 2)) & (unicodechar)0x3FULL
        ) << (unicodechar)6ULL;
        c += (  // 00111111 of fourth byte
            (unicodechar)(*(p + 3)) & (unicodechar)0x3FULL
        );
        if (c <= 0xFFFFULL)
            return 0;  // not valid to be encoded with four bytes.
        if (out) *out = c;
        if (outlen) *outlen = 4;
        return 1;
    }
    return 0;
}

int is_valid_utf8_char(
        const unsigned char *p, int size
        ) {
    if (!get_utf8_codepoint(p, size, NULL, NULL))
        return 0;
    return 1;
}

unicodechar *utf8_to_utf32(
        const char *input, int *out_len
        ) {
    return utf8_to_utf32_ex(input, out_len, 1, NULL, NULL);
}

unicodechar *utf8_to_utf32_ex(
        const char *input,
        int *out_len,
        int surrogatereplaceinvalid,
        int *was_aborted_invalid,
        int *was_aborted_outofmemory
        ) {
    int free_temp_buf = 0;
    int ilen = strlen(input);
    char *temp_buf = NULL;
    int temp_buf_len = ilen * sizeof(unicodechar);
    if (temp_buf_len < 1024 * 2) {
        temp_buf = alloca(temp_buf_len);
    } else {
        free_temp_buf = 1;
        temp_buf = malloc(temp_buf_len);
        if (!temp_buf) {
            if (was_aborted_invalid) *was_aborted_invalid = 0;
            if (was_aborted_outofmemory) *was_aborted_outofmemory = 1;
            return NULL;
        }
    }
    int k = 0;
    int i = 0;
    while (i < ilen) {
        unicodechar c;
        int cbytes = 0;
        if (!get_utf8_codepoint(
                (const unsigned char*)(input + i), ilen - i, &c, &cbytes)) {
            if (!surrogatereplaceinvalid) {
                if (free_temp_buf)
                    free(temp_buf);
                if (was_aborted_invalid) *was_aborted_invalid = 1;
                if (was_aborted_outofmemory) *was_aborted_outofmemory = 0;
                return NULL;
            }
            unicodechar invalidbyte = 0xDC80ULL + (
                (unicodechar)(*(const unsigned char*)(input + i))
            );
            memcpy((char*)temp_buf + k * sizeof(invalidbyte),
                   &invalidbyte, sizeof(invalidbyte));
            k++;
            i++;
            continue;
        }
        i += cbytes;
        memcpy((char*)temp_buf + k * sizeof(c), &c, sizeof(c));
        k++;
    }
    temp_buf[k] = '\0';
    char *result = malloc(k * sizeof(unicodechar));
    assert(k * sizeof(unicodechar) <= ilen * sizeof(unicodechar));
    if (result) {
        memcpy(result, temp_buf, k * sizeof(unicodechar));
    }
    if (free_temp_buf)
        free(temp_buf);
    if (!result) {
        if (was_aborted_invalid) *was_aborted_invalid = 0;
        if (was_aborted_outofmemory) *was_aborted_outofmemory = 1;
        return NULL;
    }
    if (was_aborted_invalid) *was_aborted_invalid = 0;
    if (was_aborted_outofmemory) *was_aborted_outofmemory = 0;
    if (out_len) *out_len = k;
    return (unicodechar*)result;
}
