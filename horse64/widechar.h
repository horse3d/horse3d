// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_WIDECHAR_H_
#define HORSE64_WIDECHAR_H_

#include <stdint.h>

typedef uint32_t __attribute__((__may_alias__)) h64wchar;

int is_valid_utf8_char(
    const unsigned char *p, int size
);

int get_utf8_codepoint(
    const unsigned char *p, int size,
    h64wchar *out, int *outlen
);

int write_codepoint_as_utf8(
    uint64_t codepoint, int surrogateunescape,
    char *out, int outbuflen, int *outlen
);

int utf8_char_len(const unsigned char *p);

h64wchar *utf8_to_utf32_ex(
    const char *input,
    int64_t input_len,
    void *(*out_alloc)(uint64_t len, void *userdata),
    void *out_alloc_ud,
    int64_t *out_len,
    int surrogatereplaceinvalid,
    int *was_aborted_invalid,
    int *was_aborted_outofmemory
);

h64wchar *utf8_to_utf32(
    const char *input, int64_t input_len,
    void *(*out_alloc)(uint64_t len, void *userdata),
    void *out_alloc_ud,
    int64_t *out_len
);

int utf32_to_utf8(
    const h64wchar *input, int64_t input_len,
    char *outbuf, int64_t outbuflen,
    int64_t *out_len, int surrogateunescape
);

#endif  // HORSE64_WIDECHAR_H_
