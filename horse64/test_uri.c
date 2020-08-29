// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>

#include "uri.h"

#include "testmain.h"

START_TEST (test_uribasics)
{
    uriinfo *uri = uri_ParseEx("file:///a%20b", NULL);
    assert(strcmp(uri->protocol, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(strcmp(uri->path, "\\a b") == 0);
    #else
    assert(strcmp(uri->path, "/a b") == 0);
    #endif
    uri_Free(uri);

    uri = uri_ParseEx("/a%20b", NULL);
    assert(strcmp(uri->protocol, "file") == 0);
    #if defined(_WIN32) || defined(_WIN64)
    assert(strcmp(uri->path, "\\a%20b") == 0);
    #else
    assert(strcmp(uri->path, "/a%20b") == 0);
    #endif
    uri_Free(uri);

    uri = uri_ParseEx("/code blah.h64", NULL);
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

    uri = uri_ParseEx("test.com:20/blubb", NULL);
    assert(!uri->protocol);
    assert(strcmp(uri->host, "test.com") == 0);
    assert(uri->port == 20);
    assert(strcmp(uri->path, "/blubb") == 0);
    uri_Free(uri);

    uri = uri_ParseEx("example.com:443", "https");
    assert(strcmp(uri->protocol, "https") == 0);
    assert(strcmp(uri->host, "example.com") == 0);
    assert(uri->port == 443);
    uri_Free(uri);

    uri = uri_ParseEx("http://blubb/", "https");
    assert(strcmp(uri->protocol, "http") == 0);
    assert(uri->port < 0);
    assert(strcmp(uri->host, "blubb") == 0);
    uri_Free(uri);
}
END_TEST

TESTS_MAIN(test_uribasics)
