// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "mainpreinit.h"
#include "widechar.h"

#include "testmain.h"

START_TEST (test_widechar)
{
    main_PreInit();

    h64wchar *s = NULL;
    int64_t out_len;
    int wasinvalid, wasoutofmem;

    wasinvalid = 0;
    wasoutofmem = 0;
    out_len = 0;
    ck_assert(utf8_to_utf32_ex(
        "\xFF\xc3\xb6", 3, NULL, 0, NULL, NULL, &out_len,
        0, 0, &wasinvalid, &wasoutofmem
    ) == NULL);
    ck_assert(wasinvalid == 1);
    ck_assert(wasoutofmem == 0);
    ck_assert(out_len == 0);

    s = NULL;
    wasinvalid = 0;
    wasoutofmem = 0;
    out_len = 0;
    ck_assert((s = utf8_to_utf32_ex(
        "\xFF\xc3\xb6", 3, NULL, 0, NULL, NULL, &out_len,
        1, 0, &wasinvalid, &wasoutofmem
    )) != NULL);
    ck_assert(wasinvalid == 0);
    ck_assert(wasoutofmem == 0);
    ck_assert(out_len == 2);
    ck_assert(s[0] == 0xDC80ULL + 0xFFULL);
    ck_assert(s[1] == 0xF6ULL);
    free(s);

    wasinvalid = 0;
    wasoutofmem = 0;
    out_len = 0;
    ck_assert(utf8_to_utf32_ex(
        "\xc3\xc3", 2, NULL, 0, NULL, NULL, &out_len,
        0, 0, &wasinvalid, &wasoutofmem
    ) == NULL);
    ck_assert(wasinvalid == 1);
    ck_assert(wasoutofmem == 0);
    ck_assert(out_len == 0);

    s = NULL;
    out_len = 0;
    ck_assert((s = utf8_to_utf32(
        "\xFF\xc3\xb6", 3, NULL, NULL, &out_len
    )) != NULL);
    ck_assert(out_len == 2);
    ck_assert(s[0] == 0xDC80ULL + 0xFFULL);
    ck_assert(s[1] == 0xF6ULL);
    free(s);

    ck_assert(!is_valid_utf8_char(
        (uint8_t*)"\xc3\xc3", 2
    ));
}
END_TEST

TESTS_MAIN (test_widechar)
