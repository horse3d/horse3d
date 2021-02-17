// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/main.h"
#include "mainpreinit.h"
#include "filesys32.h"
#include "vfs.h"

#include "../testmain.h"

#define PARSETEST_EXPECTOK 1
#define PARSETEST_EXPECTFAIL 0

void _parsetest_do(const char *testcode, int expectOK) {
    main_PreInit();

    h64misccompileroptions moptions = {0};
    h64compilewarnconfig wconfig;
    memset(&wconfig, 0, sizeof(wconfig));
    warningconfig_Init(&wconfig);

    int64_t cwdlen = 0;
    h64wchar *cwd = filesys32_GetCurrentDirectory(&cwdlen);
    assert(cwd != NULL);
    h64compileproject *project = compileproject_New(
        cwd, cwdlen, &moptions
    );
    free(cwd);
    assert(project != NULL);

    int64_t _testdata_txt_name_len = 0;
    h64wchar *_testdata_txt_name = AS_U32(
        ".testdata.txt", &_testdata_txt_name_len
    );
    int openerr = 0;
    FILE *f = filesys32_OpenFromPath(
        _testdata_txt_name, _testdata_txt_name_len, "wb", &openerr
    );
    ck_assert(f != NULL);
    ck_assert(fwrite(testcode, 1, strlen(testcode), f) == strlen(testcode));
    fclose(f);

    char *error = NULL;
    h64ast *ast = NULL;
    ck_assert(compileproject_GetAST(
        project, _testdata_txt_name, _testdata_txt_name_len,
        &moptions, &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    if (expectOK) {
        int i = 0;
        while (i < ast->resultmsg.message_count) {
            if (ast->resultmsg.message[i].type == H64MSG_ERROR) {
                printf(
                    "UNEXPECTED FAIL MSG: %" PRId64 ":%" PRId64
                    ": \"%s\"\n",
                    ast->resultmsg.message[i].line,
                    ast->resultmsg.message[i].column,
                    ast->resultmsg.message[i].message
                );
            }
            i++;
        }
        ck_assert(project->resultmsg->success && "expected parse success");
        ck_assert(ast->resultmsg.success && "expected parse success");
        i = 0;
        while (i < ast->resultmsg.message_count) {
            ck_assert(ast->resultmsg.message[i].type != H64MSG_ERROR);
            i++;
        }
    } else {
        ck_assert(!ast->resultmsg.success);
        int founderror;
        int i = 0;
        while (i < ast->resultmsg.message_count) {
            if (ast->resultmsg.message[i].type == H64MSG_ERROR)
                founderror = 1;
            i++;
        }
        assert(founderror);
    }

    compileproject_Free(project);  // This indirectly frees 'ast'!
    free(_testdata_txt_name);
}


START_TEST (test_ast_simple)
{
    char s[] = "var v = 1.5 + 0xA + 0b10";
    _parsetest_do(s, PARSETEST_EXPECTOK);
}
END_TEST

START_TEST (test_ast_complex)
{
    char s[] = (
        "class TestClass {\n"
        "    var v = 1.5 + 0xA + 0b10\n"
        "}\n"
        "func main {\n"
        "    var obj = TestClass()\n"
        "    var b = 1\n"
        "    b += obj.v\n"
        "}"
    );
    _parsetest_do(s, PARSETEST_EXPECTOK);
}
END_TEST

START_TEST (test_ast_invalidprotect)
{
    char s[] = (
        "class TestClass {\n"
        "    var protect v = 1.5 + 0xA + 0b10\n"
        "    # ^ invalid pos for a protect keyword\n"
        "}\n"
    );
    _parsetest_do(s, PARSETEST_EXPECTFAIL);
}
END_TEST

START_TEST (test_ast_twoprints)
{
    char s[] = (
        "func main {"
        "    print(4)"
        "    print(5)"
        "}"
    );
    _parsetest_do(s, PARSETEST_EXPECTOK);
}
END_TEST

START_TEST (test_ast_bracketnesting)
{
    char s[] = (
        "func testfunc {"
        "    return (5)"
        "}"
        "func main {"
        "    var v = (testfunc())"
        "}"
    );
    _parsetest_do(s, PARSETEST_EXPECTOK);
}
END_TEST


TESTS_MAIN (test_ast_simple, test_ast_complex, test_ast_twoprints,
            test_ast_bracketnesting, test_ast_invalidprotect)
