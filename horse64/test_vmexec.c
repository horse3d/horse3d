// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>
#include <stdio.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/main.h"
#include "compiler/result.h"
#include "corelib/errors.h"
#include "debugsymbols.h"
#include "uri.h"
#include "vfs.h"

#include "testmain.h"

static int _vfsinitdone = 0;

void runprog(
        const char *progname,
        const char *prog, int expected_result
        ) {
    if (!_vfsinitdone) {
        _vfsinitdone = 1;
        vfs_Init(NULL);
    }

    char *error = NULL;
    FILE *tempfile = fopen("testdata.h64", "wb");
    assert(tempfile != NULL);
    ssize_t written = fwrite(
        prog, 1, strlen(prog), tempfile
    );
    assert(written == (ssize_t)strlen(prog));
    fclose(tempfile);
    char *fileuri = uri_Normalize("testdata.h64", 1);
    assert(fileuri != NULL);
    char *project_folder_uri = NULL;
    project_folder_uri = compileproject_FolderGuess(
        fileuri, 1, &error
    );
    assert(project_folder_uri);
    h64compileproject *project = compileproject_New(project_folder_uri);
    free(project_folder_uri);
    project_folder_uri = NULL;
    assert(project != NULL);
    h64ast *ast = NULL;
    if (!compileproject_GetAST(project, fileuri, &ast, &error)) {
        fprintf(stderr, "UNEXPECTED TEST FAIL: %s\n", error);
        free(error);
        free(fileuri);
        compileproject_Free(project);
        assert(0 && "require ast parse to work");
    }
    h64misccompileroptions moptions = {0};
    if (!compileproject_CompileAllToBytecode(
            project, &moptions, fileuri, &error
            )) {
        fprintf(stderr, "UNEXPECTED TEST FAIL: %s\n", error);
        free(error);
        free(fileuri);
        compileproject_Free(project);
        assert(0 && "require bytecode compile to file");
    }
    int i = 0;
    while (i < project->resultmsg->message_count) {
        if (project->resultmsg->message[i].type == H64MSG_ERROR) {
            fprintf(
                stderr, "UNEXPECTED TEST FAIL: %s\n",
                project->resultmsg->message[i].message
            );
        }
        assert(project->resultmsg->message[i].type != H64MSG_ERROR);
        i++;
    }
    free(fileuri);
    assert(ast->resultmsg.success && project->resultmsg->success);
    moptions.vmscheduler_debug = 1;
    moptions.vmscheduler_verbose_debug = 1;
    printf("test_vmexec.c: running \"%s\"\n", progname);
    fflush(stdout);
    int resultcode = vmschedule_ExecuteProgram(
        project->program, &moptions
    );\
    fflush(stdout); fflush(stderr);  // flush program output
    compileproject_Free(project);
    if (resultcode != expected_result)
        fprintf(
            stderr, "UNEXPECTED TEST FAIL: result mismatch: "
            "got %d, expected %d\n",
            resultcode, expected_result
        );
    assert(resultcode == expected_result);
}

START_TEST (test_fibonacci)
{
    runprog(
        "test_fibonacci",
        "func fib(n) {\n"
        "    var a = 0\n"
        "    var b = 1\n"
        "    while n > 0 {\n"
        "        var tmp = b\n"
        "        b += a\n"
        "        a = tmp\n"
        "        n -= 1\n"
        "    }\n"
        "    return a\n"
        "}\n"
        "func main {return fib(40)}\n",
        102334155
    );
}
END_TEST

TESTS_MAIN(test_fibonacci)

