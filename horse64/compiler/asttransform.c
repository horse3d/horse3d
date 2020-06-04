
#include <assert.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"

int asttransform_Apply(
        h64compileproject *pr, h64ast *ast,
        int (*visit_in)(
            h64expression *expr, h64expression *parent, void *ud
        ),
        int (*visit_out)(
            h64expression *expr, h64expression *parent, void *ud
        ),
        void *ud 
        ) {
    // Do transform:
    asttransforminfo atinfo;
    memset(&atinfo, 0, sizeof(atinfo));
    atinfo.pr = pr;
    atinfo.ast = ast;
    atinfo.userdata = ud;
    int msgcount = ast->resultmsg.message_count;
    int k = 0;
    while (k < ast->stmt_count) {
        assert(ast->stmt != NULL &&
               ast->stmt[k] != NULL);
        int result = ast_VisitExpression(
            ast->stmt[k], NULL, visit_in, visit_out,
            &atinfo
        );
        if (!result || atinfo.hadoutofmemory) {
            result_AddMessage(
                &ast->resultmsg,
                H64MSG_ERROR, "out of memory during ast transform",
                ast->fileuri,
                -1, -1
            );
            return 0;
        }
        k++;
    }
    {   // Copy over new messages resulting from resolution stage:
        k = msgcount;
        while (k < ast->resultmsg.message_count) {
            if (!result_AddMessage(
                    pr->resultmsg,
                    ast->resultmsg.message[k].type,
                    ast->resultmsg.message[k].message,
                    ast->resultmsg.message[k].fileuri,
                    ast->resultmsg.message[k].line,
                    ast->resultmsg.message[k].column
                    )) {
                pr->resultmsg->success = 0;
                ast->resultmsg.success = 0;
                return 0;
            }
            if (ast->resultmsg.message[k].type == H64MSG_ERROR) {
                pr->resultmsg->success = 0;
                ast->resultmsg.success = 0;
            }
            k++;
        }
    }
    if (atinfo.hadunexpectederror) {
        // Make sure an error is returned:
        int haderrormsg = 0;
        k = 0;
        while (k < pr->resultmsg->message_count) {
            if (pr->resultmsg->message[k].type == H64MSG_ERROR)
                haderrormsg = 1;
            k++;
        }
        if (!haderrormsg) {
            result_AddMessage(
                pr->resultmsg,
                H64MSG_ERROR, "internal error: failed to apply "
                "ast transform with unknown error",
                ast->fileuri,
                -1, -1
            );
            pr->resultmsg->success = 0;
        }
    }
    return 1;
}
