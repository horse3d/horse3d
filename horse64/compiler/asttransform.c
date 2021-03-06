// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/compileproject.h"

int _asttransform_cancel_visit_descend_callback(
        ATTR_UNUSED h64expression *expr, void *ud
        ) {
    asttransforminfo *atinfo = (asttransforminfo *)ud;
    if (atinfo->dont_descend_visitation) {
        atinfo->dont_descend_visitation = 0;
        return 1;
    }
    return 0;
}

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
    atinfo.dont_descend_visitation = 0;
    int k = 0;
    while (k < ast->stmt_count) {
        assert(ast->stmt != NULL &&
               ast->stmt[k] != NULL);
        int result = ast_VisitExpression(
            ast->stmt[k], NULL, visit_in, visit_out,
            _asttransform_cancel_visit_descend_callback,
            &atinfo
        );
        if (!result || atinfo.hadoutofmemory) {
            pr->resultmsg->success = 0;
            ast->resultmsg.success = 0;
            if (atinfo.hadoutofmemory) {
                result_AddMessage(
                    &ast->resultmsg,
                    H64MSG_ERROR, "out of memory during ast transform",
                    ast->fileuri, ast->fileurilen,
                    -1, -1
                );
                // At least try to transfer messages:
                result_TransferMessages(
                    &ast->resultmsg, pr->resultmsg
                );   // return value ignored, if we're oom nothing we can do
                return 0;
            } else if (!result_TransferMessages(
                    &ast->resultmsg, pr->resultmsg
                    )) {
                return 0;
            }
        }
        k++;
    }
    {   // Copy over new messages resulting from resolution stage:
        if (!result_TransferMessages(
                &ast->resultmsg, pr->resultmsg
                )) {
            pr->resultmsg->success = 0;
            ast->resultmsg.success = 0;
            return 0;
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
                ast->fileuri, ast->fileurilen,
                -1, -1
            );
            pr->resultmsg->success = 0;
        }
    }
    return 1;
}
