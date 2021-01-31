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
#include "compiler/scoperesolver.h"
#include "filesys.h"
#include "filesys32.h"
#include "mainpreinit.h"
#include "vfs.h"

#include "../testmain.h"

START_TEST (test_scope_import_complex)
{
    main_PreInit();

    h64misccompileroptions moptions = {0};

    int64_t cwdlen = 0;
    h64wchar *cwd = filesys32_GetCurrentDirectory(&cwdlen);
    assert(cwd != NULL);

    // Prepare paths:
    int64_t testproj_namelen = 0;
    h64wchar *testproj_name = AS_U32(
        ".testdata-prj", &testproj_namelen
    );
    assert(testproj_name != NULL);
    int64_t testproj_withsubdirs_len = 0;
    h64wchar *testproj_withsubdirs = AS_U32(
        ".testdata-prj/horse_modules/my.lib/mymodule",
        &testproj_withsubdirs_len
    );
    assert(testproj_withsubdirs != NULL);
    int64_t testfolder_pathlen = 0;
    h64wchar *testfolder_path = filesys32_Join(
        cwd, cwdlen, testproj_name, testproj_namelen,
        &testfolder_pathlen
    );
    assert(testfolder_path != NULL);
    int64_t testfolder2_pathlen = 0;
    h64wchar *testfolder2_path = filesys32_Join(
        cwd, cwdlen, testproj_withsubdirs, testproj_withsubdirs_len,
        &testfolder2_pathlen
    );
    assert(testfolder2_path != NULL);
    free(cwd);
    cwd = NULL;

    int _exists = 0;
    int result = filesys32_TargetExists(
        testfolder_path, testfolder_pathlen, &_exists
    );
    assert(result != 0);
    if (_exists) {
        int _isdir = 0;
        ck_assert(
            filesys32_IsDirectory(
                testfolder_path, testfolder_pathlen, &_isdir
            ) &&
            _isdir
        );
        int _err = 0;
        int result = filesys32_RemoveFolderRecursively(
            testfolder_path, testfolder_pathlen, &_err
        );
        assert(result != 0);
    }
    assert(filesys32_TargetExists(
        testfolder_path, testfolder_pathlen, &_exists
    ) && !_exists);
    int createresult = filesys32_CreateDirectoryRecursively(
        testfolder2_path, testfolder2_pathlen, 1
    );
    ck_assert(createresult == FS32_MKDIRERR_SUCCESS);
    free(testfolder2_path);
    testfolder2_path = NULL;

    h64compileproject *project = compileproject_New(
        testfolder_path, testfolder_pathlen
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

    int64_t codefile_u32len = 0;
    h64wchar *codefile_u32 = AS_U32(
        ".testdata-prj/mainfile.h64", &codefile_u32len
    );
    assert(codefile_u32 != NULL);
    char *error = NULL;
    h64ast *ast = NULL;
    ck_assert(compileproject_GetAST(
        project, codefile_u32, codefile_u32len, &ast, &error
    ) != 0);
    ck_assert(error == NULL);
    assert(scoperesolver_ResolveAST(
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
                    AS_U8_TMP(
                        project->resultmsg->message[i].fileuri,
                        project->resultmsg->message[i].fileurilen),
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
        testfolder_path, testfolder_pathlen
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, codefile_u32, codefile_u32len, &ast, &error
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
        testfolder_path, testfolder_pathlen
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, codefile_u32, codefile_u32len, &ast, &error
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
        testfolder_path, testfolder_pathlen
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, codefile_u32, codefile_u32len, &ast, &error
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
        testfolder_path, testfolder_pathlen
    );
    error = NULL;
    ast = NULL;
    ck_assert(compileproject_GetAST(
        project, codefile_u32, codefile_u32len, &ast, &error
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

    int _err = 0;
    result = filesys32_RemoveFolderRecursively(
        testfolder_path, testfolder_pathlen, &_err
    );
    assert(result == 0);

    free(codefile_u32);
    codefile_u32 = NULL;
    free(testfolder_path);
    testfolder_path = NULL;
}
END_TEST

TESTS_MAIN (test_scope_import_complex)
