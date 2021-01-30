// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_RESULT_H_
#define HORSE64_COMPILER_RESULT_H_

#include "compileconfig.h"

#include <stdint.h>

#include "widechar.h"

typedef enum h64messagetype {
    H64MSG_ERROR = 0,
    H64MSG_INFO = 1,
    H64MSG_WARNING = 2
} h64messagetype;

typedef struct h64resultmessage {
    h64messagetype type;
    char id[32];
    char *message;
    h64wchar *fileuri;
    int64_t fileurilen;
    int64_t line, column;
} h64resultmessage;

typedef struct h64result {
    int success;
    h64wchar *fileuri;
    int64_t fileurilen;

    int message_count;
    h64resultmessage *message;
} h64result;


int result_TransferMessages(
    h64result *resultfrom, h64result *resultto
);

int result_Error(
    h64result *result,
    const char *message,
    const h64wchar *fileuri, int64_t fileurilen,
    int64_t line, int64_t column
);

int result_ErrorNoLoc(
    h64result *result,
    const char *message,
    const h64wchar *fileuri, int64_t fileurilen
);

int result_Success(
    h64result *result,
    const h64wchar *fileuri, int64_t fileurilen
);

int result_AddMessage(
    h64result *result,
    h64messagetype type,
    const char *message,
    const h64wchar *fileuri, int64_t fileurilen,
    int64_t line, int64_t column
);

int result_AddMessageNoLoc(
    h64result *result,
    h64messagetype type,
    const char *message,
    const h64wchar *fileuri, int64_t fileurilen
);

void result_FreeContents(h64result *result);

void result_RemoveMessageDuplicates(
    h64result *result
);

#endif  // HORSE64_COMPILER_RESULT_H_
