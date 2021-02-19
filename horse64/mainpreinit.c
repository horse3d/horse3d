// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause


#include "compileconfig.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <fcntl.h>
#include <windows.h>
#else
#include <signal.h>
#endif

#include "filesys.h"
#include "nonlocale.h"
#include "mainpreinit.h"
#include "packageversion.h"
#include "vfs.h"

void _load_unicode_data();  // widechar.c

static int _didpreinit = 0;

void main_PreInit() {
    if (_didpreinit)
        return;
    _didpreinit = 1;
    #if defined(_WIN32) || defined(_WIN64)
    _setmode(_fileno(stdin), O_BINARY);
    #else
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    #endif

    vfs_Init();

    // Load up unicode tables for widechar.c:
    _load_unicode_data();
}

void main_OutputVersionLong() {
    h64printf(
        "org.horse64.core horsec/horsevm v%s.\n"
        "- Corelib version:   %s\n"
        "- Build time:        %s\n"
        "- Compiler version:  %s%s\n",
        CORELIB_VERSION,
        CORELIB_VERSION, BUILD_TIME,
        #if defined(__clang__)
        "clang-", __clang_version__
        #elif defined(__GNUC__) && !defined(__clang__)
        "gcc-", __VERSION__
        #else
        "unknown-", "unknown"
        #endif
    );
}

void main_OutputVersionShort() {
    int isdev = (
        (strstr(CORELIB_VERSION, "dev") != 0) ||
        (strstr(CORELIB_VERSION, "alpha") != 0) ||
        (strstr(CORELIB_VERSION, "beta") != 0) ||
        (strstr(CORELIB_VERSION, "DEV") != 0)
    );
    h64printf("%s%s%s\n",
        CORELIB_VERSION,
        (isdev ? "-" : ""),
        (isdev ? BUILD_TIME : "")
    );
}