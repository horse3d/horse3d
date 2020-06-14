
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/asttransform.h"
#include "compiler/codegen.h"
#include "compiler/compileproject.h"

int _codegencallback_DoCodegen_visit_out(
        h64expression *expr, h64expression *parent, void *ud
        ) {
    asttransforminfo *rinfo = (asttransforminfo *)ud;

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
