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

    printf("test_vmexec.c: compiling \"%s\"\n", progname);
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
    moptions.vmexec_debug = 1;
    printf("test_vmexec.c: running \"%s\"\n", progname);
    fflush(stdout);
    int resultcode = vmschedule_ExecuteProgram(
        project->program, &moptions
    );
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

START_TEST (test_simpleclass)
{
    runprog(
        "test_simpleclass",
        "class bla {func bla{print('Hello')}} "
        "func main{var blaobj = new bla()  blaobj.bla()}",
        0
    );
}
END_TEST

START_TEST (test_attributeerrors)
{
    runprog(
        "test_attributeerrors",
        "class bla {func bla{self.x()  print('Hello')}} "
        "func main{var blaobj = new bla()  blaobj.blargh()}",
        -1
    );
}
END_TEST

START_TEST (test_hasattr)
{
    runprog(
        "test_hasattr",
        "class bla {\n"
        "    func bla{\n"
        "        if has_attr(self, 'x') {\n"
        "            self.x()\n"
        "        }\n"
        "        print('Hello')\n"
        "    }\n"
        "}\n"
        "\n"
        "func main{\n"
        "    var blaobj = new bla()\n"
        "    blaobj.bla()\n"
        "}\n",
        0
    );
}
END_TEST

START_TEST (test_callwithclass)
{
    runprog(
        "test_callwithclass",
        "func otherfunc(a1, a2) {return 5}\n"
        "func main{\n"
        "    var i = 5\n"
        "    var i2 = otherfunc(i, 'abc' + 'def')\n"
        "    return 5\n"
        "}\n",
        5
    );
}
END_TEST

START_TEST (test_hasattr2)
{
    runprog(
        "test_hasattr2",
        "class bla {\n"
        "    var varattr = 5\n"
        "    func funcattr{\n"
        "        return 6\n"
        "    }\n"
        "}\n"
        "func main{\n"
        "    var result = 0\n"
        "    var blaobj = new bla()\n"
        "    if has_attr(blaobj, 'varattr') {\n"
        "        result += blaobj.varattr\n"
        "    }\n"
        "    if has_attr(blaobj, 'funcattr') {\n"
        "        result += blaobj.funcattr()\n"
        "    }\n"
        "    if has_attr(blaobj, 'invalidattr') {\n"
        "        result = 0\n"
        "    }\n"
        "    return result\n"
        "}\n",
        11
    );
}
END_TEST

START_TEST (test_memberaccesschain)
{
    runprog(
        "test_memberaccesschain",
        "func main {\n"
        "    var s1 = 'a'  var s2 = 'bc'"
        "    print(s1.len.as_str + ', ' + s2.len.as_str)\n"
        "}\n",
        0
    );
}
END_TEST

START_TEST (test_unicodestrlen)
{
    runprog(
        "test_unicodestrlen",
        "func main {\n"
        "    var s1 = 'us flag: \\u1F1FA\\u1F1F8'\n"
        "    var s2 = 'english flag: \\u1F3F4\\uE0067\\uE0062"
        "\\uE0065\\uE006E\\uE0067\\uE007F'\n"
        "    return s1.len + s2.len\n"
        "}\n",
        10 + 15
    );
}
END_TEST

START_TEST (test_numberslist)
{
    runprog(
        "test_numberslist",
        "func main {\n"
        "    var l = [1, 2, 3]\n"
        "    var inlinel_len = [1, 2].len\n"
        "    l.add(4)\n"
        "    return l.len + inlinel_len\n"
        "}\n",
        6
    );
}
END_TEST

START_TEST (test_uri)
{
    runprog(
        "test_uri",
        "import uri from core.horse64.org\n"
        "func main {\n"
        "    var myuri = uri.parse('file://test.html')\n"
        "    return myuri.protocol.len * 2 + myuri.path.len"
        "}\n",
        strlen("file") * 2 + strlen("test.html")
    );
}
END_TEST


START_TEST (test_conditionals)
{
    runprog(
        "test_conditionals",
        "import uri from core.horse64.org\n"
        "var resultvalue = 0\n"
        "func sideeffecttrue(v) {\n"
        "    resultvalue += v\n"
        "    return true\n"
        "}\n"
        "func sideeffectfalse(v) {\n"
        "    resultvalue += v\n"
        "    return false\n"
        "}\n"
        "func main {\n"
        "    resultvalue = 0\n"
        "    if sideeffecttrue(5) or sideeffecttrue(7) {\n"
        "    }\n"
        "    # resultvalue should be 5 now.\n"
        "    if sideeffectfalse(5) or sideeffecttrue(7) {\n"
        "    }\n"
        "    # resultvalue should be 5+5+7=17 now.\n"
        "    if sideeffectfalse(2) and sideeffecttrue(3) {\n"
        "    }\n"
        "    # resultvalue should be 5+5+7+2=19 now.\n"
        "    if sideeffecttrue(2) and sideeffecttrue(3) {\n"
        "    }\n"
        "    # resultvalue should be 5+5+7+2+2+3=24 now.\n"
        "    return resultvalue\n"
        "}\n",
        24
    );
}
END_TEST


START_TEST (test_conditionals2)
{
    runprog(
        "test_conditionals2",
        "import uri from core.horse64.org\n"
        "var resultvalue = 0\n"
        "func sideeffecttrue(v) {\n"
        "    resultvalue += v\n"
        "    return true\n"
        "}\n"
        "func sideeffectfalse(v) {\n"
        "    resultvalue += v\n"
        "    return false\n"
        "}\n"
        "func main {\n"
        "    resultvalue = 0\n"
        "    if sideeffecttrue(5) or sideeffecttrue(7) or\n"
        "            sideeffectfalse(3) {\n"
        "        resultvalue += 1\n"
        "    }\n"
        "    # resultvalue should now be 5+1=6.\n"
        "    if sideeffectfalse(5) or (sideeffecttrue(7) and\n"
        "            sideeffectfalse(3)) {\n"
        "        resultvalue += 17\n"
        "    }\n"
        "    # resultvalue should now be 5+1+5+7+3=21.\n"
        "    return resultvalue\n"
        "}\n",
        21
    );
}
END_TEST

TESTS_MAIN(
    test_fibonacci, test_simpleclass, test_attributeerrors,
    test_hasattr, test_callwithclass, test_hasattr2,
    test_memberaccesschain,
    test_unicodestrlen, test_numberslist,
    test_uri, test_conditionals, test_conditionals2
)

