// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
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
#include "filesys.h"
#include "compiler/scoperesolver.h"
#include "vfs.h"

#include "../testmain.h"

START_TEST (test_scope_import_complex)
{
    vfs_Init(NULL);

    h64misccompileroptions moptions = {0};

    char *cwd = filesys_GetCurrentDirectory();
    assert(cwd != NULL);
    char *testfolder_path = filesys_Join(cwd, ".testdata-prj");
    assert(testfolder_path != NULL);
    free(cwd);
    cwd = NULL;

    if (filesys_FileExists(testfolder_path)) {
        ck_assert(filesys_IsDirectory(testfolder_path));
        int result = filesys_RemoveFolder(testfolder_path, 1);
        assert(result != 0);
    }
    assert(filesys_FileExists(testfolder_path) == 0);
    int createresult = filesys_CreateDirectory(testfolder_path, 1);
    ck_assert(createresult);
    createresult = filesys_CreateDirectory(
        ".testdata-prj/horse_modules", 1
    );
    ck_assert(createresult);
    createresult = filesys_CreateDirectory(
        ".testdata-prj/horse_modules/my.lib", 1
    );
    ck_assert(createresult);
    createresult = filesys_CreateDirectory(
        ".testdata-prj/horse_modules/my.lib/mymodule", 1
    );
    ck_assert(createresult);

    h64compileproject *project = compileproject_New(
        testfolder_path
    );
    assert(project != NULL);

    {
        FILE *f = fopen(".testdata-prj/mainfile.h64", "wb");
        ck_assert(f != NULL);
        char s[] = (
            "# PERMITTED almost-duplicate import that diverges:\n"
            "import mymodule.test1 from my.lib\n"
            "import mymodule.test2 from my.lib\n"
            "class TestClass {"
            "    var v = 1.5 + 0xA + 0b10"
            "}"
            "func main {"
            "    var obj = new TestClass()"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    {
        FILE *f = fopen(
            ".testdata-prj/horse_modules/my.lib/mymodule/test1.h64",
            "wb"
        );
        ck_assert(f != NULL);
        char s[] = (
            "func test1_blobb {"
            "    print(\"test\")"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    {
        FILE *f = fopen(
            ".testdata-prj/horse_modules/my.lib/mymodule/test2.h64",
            "wb"
        );
        ck_assert(f != NULL);
        char s[] = (
            "func test2_thing {"
            "    print(\"test\")"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }

    char *error = NULL;
    h64ast *ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata-prj/mainfile.h64", &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    ck_assert(scoperesolver_ResolveAST(
        project, &moptions, ast, 0
    ) != 0);
    if (project->resultmsg->message_count > 0) {
        int i = 0;
        while (i < project->resultmsg->message_count) {
            if (project->resultmsg->message[i].type == H64MSG_ERROR) {
                fprintf(
                    stderr, "TEST UNEXPECTED FAIL: %s "
                    "(file:%s,line:%" PRId64 ",column:%" PRId64 ")\n",
                    project->resultmsg->message[i].message,
                    project->resultmsg->message[i].fileuri,
                    project->resultmsg->message[i].line,
                    project->resultmsg->message[i].column
                );
            }
            ck_assert(project->resultmsg->message[i].type != H64MSG_ERROR);
            i++;
        }
    }
    ck_assert(ast->resultmsg.success && project->resultmsg->success);
    compileproject_Free(project);  // This indirectly frees 'ast'!

    {
        FILE *f = fopen(".testdata-prj/mainfile.h64", "wb");
        ck_assert(f != NULL);
        char s[] = (
            "# INVALID duplicate import that should fail:\n"
            "import mymodule.test1 from my.lib\n"
            "import mymodule.test1 from my.lib\n"
            "class TestClass {"
            "    var v = 1.5 + 0xA + 0b10"
            "}"
            "func main {"
            "    var obj = new TestClass()"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    project = compileproject_New(
        testfolder_path
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata-prj/mainfile.h64", &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    ck_assert(scoperesolver_ResolveAST(
        project, &moptions, ast, 0
    ) != 0);
    int founderror = 0;
    ck_assert(project->resultmsg->message_count > 0);
    {
        int i = 0;
        while (i < project->resultmsg->message_count) {
            if (project->resultmsg->message[i].type == H64MSG_ERROR) {
                founderror = 1;
                ck_assert(strstr(
                    project->resultmsg->message[i].message, "duplicate"
                ));
                ck_assert(!strstr(
                    project->resultmsg->message[i].message,
                    "randomnonsenseword"
                ));
            }
            i++;
        }
        ck_assert(founderror);
    }
    ck_assert(!ast->resultmsg.success || !project->resultmsg->success);
    compileproject_Free(project);  // This indirectly frees 'ast'

    {
        FILE *f = fopen(".testdata-prj/mainfile.h64", "wb");
        ck_assert(f != NULL);
        char s[] = (
            "# VALID use of imported element:\n"
            "import mymodule.test1 from my.lib\n"
            "func main {"
            "    var obj = mymodule.test1.test1_blobb()"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    project = compileproject_New(
        testfolder_path
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata-prj/mainfile.h64", &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    ck_assert(scoperesolver_ResolveAST(
        project, &moptions, ast, 0
    ) != 0);
    if (project->resultmsg->message_count > 0) {
        int i = 0;
        while (i < project->resultmsg->message_count) {
            if (project->resultmsg->message[i].type == H64MSG_ERROR) {
                fprintf(stderr, "TEST UNEXPECTED FAIL: %s\n",
                        project->resultmsg->message[i].message);
            }
            ck_assert(project->resultmsg->message[i].type != H64MSG_ERROR);
            i++;
        }
    }
    ck_assert(ast->resultmsg.success && project->resultmsg->success);
    compileproject_Free(project);  // This indirectly frees 'ast'

    {
        FILE *f = fopen(".testdata-prj/mainfile.h64", "wb");
        ck_assert(f != NULL);
        char s[] = (
            "# INVALID use of function that is not in imported module:\n"
            "import mymodule.test1 from my.lib\n"
            "func main {"
            "    var obj = mymodule.test1.no_such_function_invalid()"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    project = compileproject_New(
        testfolder_path
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata-prj/mainfile.h64", &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    ck_assert(scoperesolver_ResolveAST(
        project, &moptions, ast, 0
    ) != 0);
    founderror = 0;
    ck_assert(ast->resultmsg.message_count > 0);
    {
        int i = 0;
        while (i < ast->resultmsg.message_count) {
            if (ast->resultmsg.message[i].type == H64MSG_ERROR) {
                founderror = 1;
                ck_assert(strstr(
                    ast->resultmsg.message[i].message, "unknown"
                ));
                ck_assert(!strstr(
                    ast->resultmsg.message[i].message, "randomnonsenseword"
                ));
            }
            i++;
        }
        ck_assert(founderror);
    }
    ck_assert(!ast->resultmsg.success || !project->resultmsg->success);
    compileproject_Free(project);  // This indirectly frees 'ast'

    {
        FILE *f = fopen(".testdata-prj/mainfile.h64", "wb");
        ck_assert(f != NULL);
        char s[] = (
            "# VALID x parameter reference that broke in the past,\n"
            "# due to scope being wrong:\n"
            "func main {"
            "    var v = x => (x + 1)"
            "}"
        );
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
    }
    project = compileproject_New(
        testfolder_path
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, ".testdata-prj/mainfile.h64", &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    ck_assert(ast->fileuri != NULL);
    ck_assert(scoperesolver_ResolveAST(
        project, &moptions, ast, 0
    ) != 0);
    if (project->resultmsg->message_count > 0) {
        int i = 0;
        while (i < project->resultmsg->message_count) {
            if (project->resultmsg->message[i].type == H64MSG_ERROR) {
                fprintf(stderr, "TEST UNEXPECTED FAIL: %s\n",
                        project->resultmsg->message[i].message);
            }
            ck_assert(project->resultmsg->message[i].type != H64MSG_ERROR);
            i++;
        }
    }
    ck_assert(ast->resultmsg.success && project->resultmsg->success);
    compileproject_Free(project);  // This indirectly frees 'ast'

    free(testfolder_path);
    testfolder_path = NULL;
}
END_TEST

TESTS_MAIN (test_scope_import_complex)
