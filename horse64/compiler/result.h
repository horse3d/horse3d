// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_RESULT_H_
#define HORSE64_COMPILER_RESULT_H_

#include <stdint.h>

typedef enum h64messagetype {
    H64MSG_ERROR = 0,
    H64MSG_INFO = 1,
    H64MSG_WARNING = 2
} h64messagetype;

typedef struct h64resultmessage {
    h64messagetype type;
    char id[32];
    char *message;
    char *fileuri;
    int64_t line, column;
} h64resultmessage;

typedef struct h64result {
    int success;
    char *fileuri;

    int message_count;
    h64resultmessage *message;
} h64result;


int result_TransferMessages(
    h64result *resultfrom, h64result *resultto
);

int result_Error(
    h64result *result,
    const char *message,
    const char *fileuri,
    int64_t line, int64_t column
);

int result_ErrorNoLoc(
    h64result *result,
    const char *message,
    const char *fileuri
);

int result_Success(
    h64result *result,
    const char *fileuri
);

int result_AddMessage(
    h64result *result,
    h64messagetype type,
    const char *message,
    const char *fileuri,
    int64_t line, int64_t column
);

int result_AddMessageNoLoc(
    h64result *result,
    h64messagetype type,
    const char *message,
    const char *fileuri
);

void result_FreeContents(h64result *result);

#endif  // HORSE64_COMPILER_RESULT_H_
