// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>

#include "mainpreinit.h"
#include "nonlocale.h"
#include "uri32.h"

#include "testmain.h"

static int64_t _uri32arglen = 0;
static h64wchar _uri32arg[4096];

h64wchar *_u32(const char *u8) {
    int64_t asu32len = 0;
    h64wchar *asu32 = AS_U32(
        u8, &asu32len
    );
    if (!asu32) {
        _uri32arglen = 0;
        return NULL;
    }
    if ((uint64_t)asu32len >
            sizeof(_uri32arg) / sizeof(_uri32arg[0])) {
        asu32len = (
            sizeof(_uri32arg) / sizeof(_uri32arg[0])
        );
    }
    memcpy(_uri32arg, asu32, sizeof(*asu32) * asu32len);
    free(asu32);
    return _uri32arg;
}

int64_t _u32len(const char *u8) {
    int64_t asu32len = 0;
    h64wchar *asu32 = AS_U32(
        u8, &asu32len
    );
    if (!asu32) {
        return 0;
    }
    free(asu32);
    if ((uint64_t)asu32len >
            sizeof(_uri32arg) / sizeof(_uri32arg[0])) {
        asu32len = (
            sizeof(_uri32arg) / sizeof(_uri32arg[0])
        );
    }
    _uri32arglen = asu32len;
    return asu32len;
}

START_TEST (test_uribasics)
{
    main_PreInit();

    // Test escaped space, which should be converted here:
    uri32info *uri = uri32_ParseExU8Protocol(
        _u32("file:///a%20b"), _u32len("file:///a%20b"), NULL, 0
    );
    assert(uri != NULL);
    assert(uri->protocol &&
           h64casecmp_u32u8(uri->protocol, uri->protocollen, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(uri->path && h64casecmp_u32u8(
        uri->path, uri->pathlen, "\\a b") == 0);
    #else
    assert(uri->path && h64casecmp_u32u8(uri->path,
        uri->pathlen, "/a b") == 0);
    #endif
    uri32_Free(uri);

    // Test escaped space, we expect it LEFT ALONE in a plain path:
    uri = uri32_ParseEx(
        _u32("/a%20b"), _u32len("/a%20b"),
        NULL, 0, 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "\\a%20b") == 0);
    #else
    assert(h64casecmp_u32u8(uri->path, uri->pathlen,
        "/a%20b") == 0);
    #endif
    uri32_Free(uri);

    // Utf-8 escaped o with umlaut dots (ö):
    uri = uri32_ParseEx(
        _u32("file:///%C3%B6"), _u32len("file:///%C3%B6"),
        NULL, 0, 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "\\\xC3\xB6") == 0);
    #else
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "/\xC3\xB6") == 0);
    #endif
    uri32_Free(uri);

    // Utf-8 o with umlaut dots (ö), but now via utf-32 input UNESCAPED:
    h64wchar testurl[64];
    testurl[0] = '/';
    testurl[1] = 246;  // umlaut o
    uri32info *uri32 = uri32_ParseEx(testurl, 2, NULL, 0, 0);
    assert(uri32->pathlen == 2);
    #if defined(_WIN32) || defined(_WIN64)
    assert(uri32->path[0] == '\\');
    #else
    assert(uri32->path[0] == '/');
    #endif
    assert(uri32->path[1] == 246);
    uri32_Free(uri32);

    // Utf-8 o with umlaut dots (ö), but now via utf-32 input ESCAPED:
    testurl[0] = 'f'; testurl[1] = 'i'; testurl[2] = 'l';
    testurl[3] = 'e'; testurl[4] = ':'; testurl[5] = '/';
    testurl[6] = '/'; testurl[7] = '/';
    testurl[8] = '%'; testurl[9] = 'C'; testurl[10] = '3';
    testurl[11] = '%'; testurl[12] = 'B'; testurl[13] = '6';
    uri32 = uri32_ParseEx(testurl, 14, NULL, 0, 0);
    assert(uri32->pathlen == 2);
    #if defined(_WIN32) || defined(_WIN64)
    assert(uri32->path[0] == '\\');
    #else
    assert(uri32->path[0] == '/');
    #endif
    assert(uri32->path[1] == 246);
    uri32_Free(uri32);

    // Test literal spaces:
    uri = uri32_ParseEx(
        _u32("/code blah.h64"), _u32len("/code blah.h64"),
        NULL, 0, 0
    );
    #if defined(_WIN32) || defined(_WIN64)
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "\\code blah.h64") == 0);
    #else
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "/code blah.h64") == 0);
    #endif
    {
        int64_t slen = 0;
        h64wchar *s = uri32_Dump(uri, &slen);
        assert(h64casecmp_u32u8(s, slen,
            "file:///code%20blah.h64") == 0);
        free(s);
    }
    uri32_Free(uri);

    // Test that with no default protocol, no protocol is added:
    uri = uri32_ParseEx(
        _u32("test.com:20/blubb"), _u32len("test.com:20/blubb"),
        NULL, 0, 0
    );
    assert(!uri->protocol);
    assert(h64casecmp_u32u8(uri->host,
        uri->hostlen, "test.com") == 0);
    assert(uri->port == 20);
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "/blubb") == 0);
    uri32_Free(uri);

    // Again no default protocol, but no path appended either:
    uri = uri32_ParseExU8Protocol(
        _u32("example.com:443"), _u32len("example.com:443"),
        "https", 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "https") == 0);
    assert(h64casecmp_u32u8(uri->host,
        uri->hostlen, "example.com") == 0);
    assert(uri->port == 443);
    uri32_Free(uri);

    // If we default to file even for remote-looking stuff, this
    // should return a file path:
    uri = uri32_ParseExU8Protocol(
        _u32("example.com:443"), _u32len("example.com:443"),
        "file", 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "file") == 0);
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "example.com:443") == 0);
    assert(uri->port < 0);
    uri32_Free(uri);

    // Test that remote protocol stays as-is even with different one set:
    uri = uri32_ParseExU8Protocol(
        _u32("http://blubb/"), _u32len("http://blubb/"),
        "https", 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "http") == 0);
    assert(uri->port < 0);
    assert(h64casecmp_u32u8(uri->host,
        uri->hostlen, "blubb") == 0);
    uri32_Free(uri);

    // Test port guessing:
    uri = uri32_ParseExU8Protocol(
        _u32("http://blubb/"),_u32len("http://blubb/"),
        "https", URI32_PARSEEX_FLAG_GUESSPORT
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "http") == 0);
    assert(uri->port == 80);
    assert(h64casecmp_u32u8(uri->host,
        uri->hostlen, "blubb") == 0);
    uri32_Free(uri);

    // Test a relative path file name that used to break:
    uri = uri32_ParseExU8Protocol(
        _u32("test-file.h64"), _u32len("test-file.h64"),
        "https", 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "file") == 0);
    assert(uri->port < 0);
    assert(h64casecmp_u32u8(uri->path,
        uri->pathlen, "test-file.h64") == 0);
    uri32_Free(uri);

    // Test that VFS paths are treated like file paths, and not like
    // a remote URI with a host:
    uri = uri32_ParseExU8Protocol(
        _u32("vfs://blubb:80/"), _u32len("vfs://blubb:80/"),
        "https", 0
    );
    assert(h64casecmp_u32u8(uri->protocol,
        uri->protocollen, "vfs") == 0);
    assert(uri->port < 0);
    assert(uri->host == NULL);
    assert(h64casecmp_u32u8(uri->path,
                uri->pathlen, "blubb:80/") == 0 ||
           h64casecmp_u32u8(uri->path,
                uri->pathlen, "blubb:80\\") == 0 ||
           h64casecmp_u32u8(uri->path,
                uri->pathlen, "blubb:80") == 0);
    uri32_Free(uri);
}
END_TEST

TESTS_MAIN(test_uribasics)
