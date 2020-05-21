#include <assert.h>
#include <check.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "filesys.h"
#include "vfs.h"

#include "../testmain.h"

START_TEST (test_ast_simple)
{
    vfs_Init(NULL);

    h64compilewarnconfig wconfig;
    memset(&wconfig, 0, sizeof(wconfig));
    warningconfig_Init(&wconfig);

    char *cwd = filesys_GetCurrentDirectory();
    assert(cwd != NULL);
    h64compileproject *project = compileproject_New(
        cwd
    );
    free(cwd);
    assert(project != NULL);

    FILE *f = fopen(".testdata.txt", "wb");
    ck_assert(f != NULL);
    char s[] = "var v = 1.5 + 0xA + 0b10";
    ck_assert(fwrite(s, 1, strlen(s), f));
    fclose(f);

    char *error = NULL;
    h64ast *ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata.txt", &ast, &error
    ) != 0);
    ck_assert(error == NULL);

    compileproject_Free(project);  // This indirectly frees 'ast'!
}

TESTS_MAIN (test_ast_simple)
