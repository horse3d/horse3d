// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/result.h"
#include "datetime.h"
#include "secrandom.h"

int result_Error(
        h64result *result,
        const char *message,
        const char *fileuri,
        int64_t line, int64_t column
        ) {
    if (!result_Success(result, fileuri)) {
        result->success = 0;
        return 0;
    }
    result->success = 0;
    if (!result_AddMessage(
            result, H64MSG_ERROR, message, fileuri, line, column
            ))
        return 0;
    return 1;
}

int result_ErrorNoLoc(
        h64result *result,
        const char *message,
        const char *fileuri
        ) {
    return result_Error(
        result, message, fileuri, -1, -1
    );
}

int result_Success(
        h64result *result,
        const char *fileuri
        ) {
    result->success = 1;
    if (fileuri && !result->fileuri) {
        result->fileuri = strdup(fileuri);
        if (!result->fileuri)
            return 0;
    }
    return 1;
}

int result_AddMessageEx(
        h64result *result,
        h64messagetype type,
        const char *message,
        const char *fileuri,
        int64_t line, int64_t column,
        const char id[32]
        ) {
    int newcount = result->message_count + 1;
    h64resultmessage *newmsgs = realloc(
        result->message, sizeof(*newmsgs) * newcount
    );
    if (!newmsgs)
        return 0;
    result->message = newmsgs;
    memset(&result->message[newcount - 1], 0, sizeof(*newmsgs));
    memcpy(result->message[newcount - 1].id, id,
        sizeof(*result->message[newcount - 1].id));
    result->message[newcount - 1].type = type;
    if (message) {
        result->message[newcount - 1].message = strdup(message);
        if (!result->message[newcount - 1].message) {
            return 0;
        }
    }
    result->message[newcount - 1].line = line;
    result->message[newcount - 1].column = column;
    if (fileuri) {
        result->message[newcount - 1].fileuri = strdup(fileuri);
        if (!result->message[newcount - 1].fileuri) {
            free(result->message[newcount - 1].message);
            return 0;
        }
    }
    result->message_count = newcount;
    return 1;
}

int result_AddMessage(
        h64result *result,
        h64messagetype type,
        const char *message,
        const char *fileuri,
        int64_t line, int64_t column
        ) {
    char id[32];
    while (1) {
        if (secrandom_GetBytes(id, sizeof(*id))) break;
        datetime_Sleep(10);
    }
    return result_AddMessageEx(
        result, type, message, fileuri,
        line, column, id
    );
}

int result_MessageIdToIndex(h64result *result, const char id[32]) {
    int i = 0;
    while (i < result->message_count) {
        if (memcmp(result->message[i].id, id,
                sizeof(*result->message[i].id)) == 0)
            return i;
        i++;
    }
    return -1;
}

int result_TransferMessages(
        h64result *resultfrom, h64result *resultto
        ) {
    int i = 0;
    while (i < resultfrom->message_count) {
        int targeti = result_MessageIdToIndex(
            resultto, resultfrom->message[i].id
        );
        if (targeti < 0) {
            if (!result_AddMessageEx(
                    resultto, resultfrom->message[i].type,
                    resultfrom->message[i].message,
                    resultfrom->message[i].fileuri,
                    resultfrom->message[i].line,
                    resultfrom->message[i].column,
                    resultfrom->message[i].id
                    ))
                return 0;
        }
        if (resultfrom->message[i].type == H64MSG_ERROR)
            resultto->success = 0;
        i++;
    }
    return 1;
}

int result_AddMessageNoLoc(
        h64result *result,
        h64messagetype type,
        const char *message,
        const char *fileuri
        ) {
    return result_AddMessage(
        result, type, message, fileuri, -1, -1
    );
}

void result_FreeContents(h64result *result) {
    int i = 0;
    while (i < result->message_count) {
        if (result->message[i].message)
            free(result->message[i].message);
        if (result->message[i].fileuri)
            free(result->message[i].fileuri);
        i++;
    }
    if (result->message)
        free(result->message);
    if (result->fileuri)
        free(result->fileuri);
    result->fileuri = NULL;
    result->message = NULL;
    result->message_count = 0;
}
