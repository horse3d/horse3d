
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/codegen.h"
#include "compiler/compileproject.h"
#include "compiler/varstorage.h"


int newcalctemp(h64expression *func) {
    assert(func->funcdef._storageinfo != NULL);
    func->funcdef._storageinfo->_temp_calc_slots_used_right_now++;
    if (func->funcdef._storageinfo->_temp_calc_slots_used_right_now >
            func->funcdef._storageinfo->temp_calculation_slots)
        func->funcdef._storageinfo->temp_calculation_slots = (
            func->funcdef._storageinfo->_temp_calc_slots_used_right_now
        );
    return func->funcdef._storageinfo->_temp_calc_slots_used_right_now;
}

int _codegencallback_DoCodegen_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

    h64expression *func = surroundingfunc(expr);
    if (!func) {
        return 1;
    }

    if (IS_STMT(expr->type)) {
        func->funcdef._storageinfo->_temp_calc_slots_used_right_now = 0;
    }

    return 1;
}

int codegen_GenerateBytecodeForFile(
        h64compileproject *project, h64ast *resolved_ast
        ) {
    if (!project || !resolved_ast)
        return 0;

    // Do actual codegen step:
    int transformresult = asttransform_Apply(
        project, resolved_ast,
        NULL, &_codegencallback_DoCodegen_visit_out,
        NULL
    );
    if (!transformresult)
        return 0;

    return 1;
}
