// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "compiler/result.h"
#include "datetime.h"
#include "secrandom.h"

int result_Error(
        h64result *result,
        const char *message,
        const h64wchar *fileuri, int64_t fileurilen,
        int64_t line, int64_t column
        ) {
    if (!result_Success(result, fileuri, fileurilen)) {
        result->success = 0;
        return 0;
    }
    result->success = 0;
    if (!result_AddMessage(
            result, H64MSG_ERROR, message, fileuri, fileurilen,
            line, column
            ))
        return 0;
    return 1;
}

void result_RemoveMessageDuplicates(
        h64result *result
        ) {
    int i = 0;
    while (i < result->message_count) {
        int k = i + 1;
        while (k < result->message_count) {
            if (result->message[i].type ==
                    result->message[k].type &&
                    result->message[i].line ==
                    result->message[k].line &&
                    result->message[i].column ==
                    result->message[k].column &&
                    (strcmp(result->message[i].message,
                            result->message[k].message) == 0)) {
                free(result->message[k].message);
                if (k + 1 < result->message_count)
                    memcpy(
                        &result->message[k],
                        &result->message[k + 1],
                        sizeof(*result->message) * (
                            result->message_count - k - 1
                        )
                    );
                result->message_count--;
                continue;
            }
            k++;
        }
        i++;
    }
}

int result_ErrorNoLoc(
        h64result *result,
        const char *message,
        const h64wchar *fileuri, int64_t fileurilen
        ) {
    return result_Error(
        result, message, fileuri, fileurilen, -1, -1
    );
}

int result_Success(
        h64result *result,
        const h64wchar *fileuri, int64_t fileurilen
        ) {
    result->success = 1;
    if (fileuri && !result->fileuri) {
        result->fileuri = strdupu32(
            fileuri, fileurilen, &result->fileurilen
        );
        if (!result->fileuri)
            return 0;
    }
    return 1;
}

int result_AddMessageEx(
        h64result *result,
        h64messagetype type,
        const char *message,
        const h64wchar *fileuri, int64_t fileurilen,
        int64_t line, int64_t column,
        const char id[32]
        ) {
    if (type == H64MSG_ERROR) {
        // Do this first in any case, even if we fail to actually
        // allocate the added error message later:
        result->success = 0;
    }
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
        result->message[newcount - 1].fileuri = (
            strdupu32(fileuri, fileurilen,
                &result->message[newcount - 1].fileurilen)
        );
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
        const h64wchar *fileuri, int64_t fileurilen,
        int64_t line, int64_t column
        ) {
    char id[32];
    while (1) {
        if (secrandom_GetBytes(id, sizeof(*id))) break;
        datetime_Sleep(10);
    }
    return result_AddMessageEx(
        result, type, message, fileuri, fileurilen,
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
                    resultfrom->message[i].fileurilen,
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
        const h64wchar *fileuri, int64_t fileurilen
        ) {
    return result_AddMessage(
        result, type, message, fileuri, fileurilen, -1, -1
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
