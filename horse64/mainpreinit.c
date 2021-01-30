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
#include "mainpreinit.h"
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