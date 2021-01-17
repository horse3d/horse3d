// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>

#include "uri.h"
#include "uri32.h"

#include "testmain.h"

START_TEST (test_uribasics)
{
    // Test escaped space, which should be converted here:
    uriinfo *uri = uri_ParseEx("file:///a%20b", NULL, 0);
    assert(uri->protocol &&
           strcmp(uri->protocol, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(uri->path && strcmp(uri->path, "\\a b") == 0);
    #else
    assert(uri->path && strcmp(uri->path, "/a b") == 0);
    #endif
    uri_Free(uri);

    // Test escaped space, we expect it LEFT ALONE in a plain path:
    uri = uri_ParseEx("/a%20b", NULL, 0);
    assert(strcmp(uri->protocol, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(strcmp(uri->path, "\\a%20b") == 0);
    #else
    assert(strcmp(uri->path, "/a%20b") == 0);
    #endif
    uri_Free(uri);

    // Utf-8 escaped o with umlaut dots (ö):
    uri = uri_ParseEx("file:///%C3%B6", NULL, 0);
    assert(strcmp(uri->protocol, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(strcmp(uri->path, "\\\xC3\xB6") == 0);
    #else
    assert(strcmp(uri->path, "/\xC3\xB6") == 0);
    #endif
    uri_Free(uri);

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
    uri = uri_ParseEx("/code blah.h64", NULL, 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(strcmp(uri->path, "\\code blah.h64") == 0);
    #else
    assert(strcmp(uri->path, "/code blah.h64") == 0);
    #endif
    {
        char *s = uri_Dump(uri);
        assert(strcmp(s, "file:///code%20blah.h64") == 0);
        free(s);
    }
    uri_Free(uri);

    // Test that with no default protocol, no protocol is added:
    uri = uri_ParseEx("test.com:20/blubb", NULL, 0);
    assert(!uri->protocol);
    assert(strcmp(uri->host, "test.com") == 0);
    assert(uri->port == 20);
    assert(strcmp(uri->path, "/blubb") == 0);
    uri_Free(uri);

    // Again no default protocol, but no path appended either:
    uri = uri_ParseEx("example.com:443", "https", 0);
    assert(strcmp(uri->protocol, "https") == 0);
    assert(strcmp(uri->host, "example.com") == 0);
    assert(uri->port == 443);
    uri_Free(uri);

    // If we default to file even for remote-looking stuff, this
    // should return a file path:
    uri = uri_ParseEx("example.com:443", "file", 0);
    assert(strcmp(uri->protocol, "file") == 0);
    assert(strcmp(uri->path, "example.com:443") == 0);
    assert(uri->port < 0);
    uri_Free(uri);

    // Test that remote protocol stays as-is even with different one set:
    uri = uri_ParseEx("http://blubb/", "https", 0);
    assert(strcmp(uri->protocol, "http") == 0);
    assert(uri->port < 0);
    assert(strcmp(uri->host, "blubb") == 0);
    uri_Free(uri);

    // Test port guessing:
    uri = uri_ParseEx(
        "http://blubb/", "https", URI_PARSEEX_FLAG_GUESSPORT
    );
    assert(strcmp(uri->protocol, "http") == 0);
    assert(uri->port == 80);
    assert(strcmp(uri->host, "blubb") == 0);
    uri_Free(uri);

    // Test a relative path file name that used to break:
    uri = uri_ParseEx("test-file.h64", "https", 0);
    assert(strcmp(uri->protocol, "file") == 0);
    assert(uri->port < 0);
    assert(strcmp(uri->path, "test-file.h64") == 0);
    uri_Free(uri);

    // Test that VFS paths are treated like file paths, and not like
    // a remote URI with a host:
    uri = uri_ParseEx("vfs://blubb:80/", "https", 0);
    assert(strcmp(uri->protocol, "vfs") == 0);
    assert(uri->port < 0);
    assert(uri->host == NULL);
    assert(strcmp(uri->path, "blubb:80/") == 0 ||
           strcmp(uri->path, "blubb:80\\") == 0 ||
           strcmp(uri->path, "blubb:80") == 0);
    uri_Free(uri);
}
END_TEST

TESTS_MAIN(test_uribasics)
