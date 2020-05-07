
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "compiler/lexer.h"
#include "compiler/warningconfig.h"

h64ast codemodule_GetASTUncached(
        h64compileproject *pr, const char *fileuri,
        h64compilewarnconfig *wconfig
        ) {
    // 1. Get tokens:
    h64tokenizedfile tfile = lexer_ParseFromFile(fileuri, wconfig, 0);
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
        h64ast tcode;
        memset(&tcode, 0, sizeof(tcode));
        memcpy(&tcode.resultmsg, &tfile.resultmsg,
               sizeof(tcode.resultmsg));
        return tcode;
    }

    // 2. Parse AST from tokens:
    h64ast tcode = ast_ParseFromTokens(
        pr, fileuri, tfile.token, tfile.token_count
    );
    lexer_FreeFileTokens(&tfile);
    haderrormessages = 0;
    i = 0;
    while (i < tcode.resultmsg.message_count) {
        if (tcode.resultmsg.message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        i++;
    }
    i = 0;
    while (i < tfile.resultmsg.message_count) {  // combine messages
        if (!result_AddMessage(
                &tcode.resultmsg, tfile.resultmsg.message[i].type,
                tfile.resultmsg.message[i].message,
                tfile.resultmsg.message[i].fileuri,
                tfile.resultmsg.message[i].line,
                tfile.resultmsg.message[i].column
                )) {
            result_FreeContents(&tcode.resultmsg);
            tcode.resultmsg.success = 0;
            return tcode;
        }
        if (tfile.resultmsg.message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        i++;
    }
    result_FreeContents(&tfile.resultmsg);
    if (haderrormessages || !tfile.resultmsg.success) {
        tcode.resultmsg.success = 0;
        return tcode;
    }

    return tcode;
}
