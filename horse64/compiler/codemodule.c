// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "compiler/lexer.h"
#include "compiler/warningconfig.h"
#include "uri.h"

h64ast *codemodule_GetASTUncached(
        h64compileproject *pr, uriinfo *fileuri,
        h64compilewarnconfig *wconfig
        ) {
    char *fileuri_s = uri_Dump(fileuri);
    if (!fileuri_s) {
        return NULL;
    }

    // 1. Get tokens:
    h64tokenizedfile tfile = lexer_ParseFromFile(fileuri, wconfig);
    int haderrormessages = 0;
    int i = 0;
    while (i < tfile.resultmsg.message_count) {
        if (tfile.resultmsg.message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        i++;
    }
    if ((haderrormessages || !tfile.resultmsg.success) &&
            tfile.token_count == 0) {
        lexer_FreeFileTokens(&tfile);
        h64ast *tcode = malloc(sizeof(*tcode));
        if (!tcode) {
            lexer_FreeFileTokens(&tfile);
            result_FreeContents(&tfile.resultmsg);
            free(fileuri_s);
            return NULL;
        }
        memset(tcode, 0, sizeof(*tcode));
        tcode->basic_file_access_was_successful = 0;
        memcpy(&tcode->resultmsg, &tfile.resultmsg,
               sizeof(tcode->resultmsg));
        if (fileuri) {
            tcode->fileuri = strdup(fileuri_s);
            if (!tcode->fileuri) {
                free(tcode);
                lexer_FreeFileTokens(&tfile);
                result_FreeContents(&tfile.resultmsg);
                free(fileuri_s);
                return NULL;
            }
        }
        return tcode;
    }

    // 2. Parse AST from tokens:
    h64ast *tcode = ast_ParseFromTokens(
        pr, fileuri_s, tfile.token, tfile.token_count
    );
    if (!tcode) {
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
        free(fileuri_s);
        return NULL;
    }
    tcode->basic_file_access_was_successful = 1;
    lexer_FreeFileTokens(&tfile);
    haderrormessages = 0;
    i = 0;
    while (i < tcode->resultmsg.message_count) {
        if (tcode->resultmsg.message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        i++;
    }
    if (!result_TransferMessages(
            &tfile.resultmsg, &tcode->resultmsg
            )) {
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
        result_FreeContents(&tcode->resultmsg);
        ast_FreeContents(tcode);
        free(tcode);
        free(fileuri_s);
        return NULL;
    }
    while (i < tfile.resultmsg.message_count) {
        if (tfile.resultmsg.message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        i++;
    }
    result_FreeContents(&tfile.resultmsg);
    if (fileuri && !tcode->fileuri) {
        // Can happen with out of memory.
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
        result_FreeContents(&tcode->resultmsg);
        ast_FreeContents(tcode);
        free(tcode);
        free(fileuri_s);
        return NULL;
    }
    if (haderrormessages || !tfile.resultmsg.success) {
        tcode->resultmsg.success = 0;
        free(fileuri_s);
        return tcode;
    }

    free(fileuri_s);
    return tcode;
}
