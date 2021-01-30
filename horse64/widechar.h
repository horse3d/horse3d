// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_WIDECHAR_H_
#define HORSE64_WIDECHAR_H_

#include "compileconfig.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
    int invalidquestionmarkescape,
    char *out, int outbuflen, int *outlen
);

int utf8_char_len(const unsigned char *p);

h64wchar *utf8_to_utf32_ex(
    const char *input,
    int64_t input_len,
    char *short_out_buf, int short_out_buf_bytes,
    void *(*out_alloc)(uint64_t len, void *userdata),
    void *out_alloc_ud,
    int64_t *out_len,
    int surrogatereplaceinvalid,
    int questionmarkinvalid,
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
    int64_t *out_len, int surrogateunescape,
    int invalidquestionmarkescape
);

int utf32_to_utf16(
    const h64wchar *input, int64_t input_len,
    char *outbuf, int64_t outbufbyteslen,
    int64_t *out_len, int surrogateunescape
);

h64wchar *utf16_to_utf32(
    const uint16_t *input, int64_t input_len,
    int64_t *out_len, int surrogateescape,
    int *wasoom
);

int64_t utf32_letter_len(
    const h64wchar *sdata, int64_t sdata_len
);

int64_t utf32_letters_count(
    h64wchar *sdata, int64_t sdata_len
);

int utf8_to_utf16(
    const uint8_t *input, int64_t input_len,
    uint16_t *outbuf, int64_t outbuflen,
    int64_t *out_len, int surrogateunescape,
    int surrogateescape
);

int utf16_to_utf8(
    const uint16_t *input, int64_t input_len,
    char *outbuf, int64_t outbuflen,
    int64_t *out_len, int surrogateescape
);

void utf32_tolower(h64wchar *s, int64_t slen);

void utf32_toupper(h64wchar *s, int64_t slen);

h64wchar *AS_U32(const char *s, int64_t *out_len);

char *AS_U8(const h64wchar *s, int64_t slen);

const char *AS_U8_TMP(const h64wchar *s, int64_t slen);

h64wchar *strdupu32(
    const h64wchar *s, int64_t slen, int64_t *out_len
);

const h64wchar *strstr_u32u8(
    const h64wchar *s, int64_t slen, const char *find
);

h64wchar *strdup_u32u8(const char *u8, int64_t *out_len);

#endif  // HORSE64_WIDECHAR_H_