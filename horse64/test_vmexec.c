// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <check.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/main.h"
#include "compiler/result.h"
#include "corelib/errors.h"
#include "debugsymbols.h"
#include "filesys.h"
#include "filesys32.h"
#include "mainpreinit.h"
#include "nonlocale.h"
#include "uri32.h"
#include "vfs.h"

#include "testmain.h"

void runprog(
        const char *progname,
        const char *prog, int expected_result
        ) {
    main_PreInit();

    printf("test_vmexec.c: compiling \"%s\"\n", progname);
    // Write code into test file:
    char *error = NULL;
    FILE *tempfile = fopen("testdata.h64", "wb");
    assert(tempfile != NULL);
    ssize_t written = fwrite(
        prog, 1, strlen(prog), tempfile
    );
    assert(written == (ssize_t)strlen(prog));
    fclose(tempfile);

    h64misccompileroptions moptions = {0};

    // Parse file uri:
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    {
        h64wchar _testdata_h64_const[] = {
            't', 'e', 's', 't', 'd', 'a', 't', 'a', '.', 'h', '6', '4'
        };
        int64_t _testdata_h64_const_len = strlen("testdata.h64");
        fileuri = uri32_Normalize(
            _testdata_h64_const, _testdata_h64_const_len, 1, &fileurilen
        );
    }
    assert(fileuri != NULL);
    int64_t project_folder_uri_len = 0;
    h64wchar *project_folder_uri = NULL;
    project_folder_uri = compileproject_FolderGuess(
        fileuri, fileurilen, 1, &moptions,
        &project_folder_uri_len, &error
    );
    assert(project_folder_uri);
    h64compileproject *project = compileproject_New(
        project_folder_uri, project_folder_uri_len, &moptions
    );
    free(project_folder_uri);
    project_folder_uri = NULL;
    assert(project != NULL);
    h64ast *ast = NULL;
    if (!compileproject_GetAST(
            project, fileuri, fileurilen, &moptions,
            &ast, &error
            )) {
        fprintf(stderr, "UNEXPECTED TEST FAIL: %s\n", error);
        free(error);
        free(fileuri);
        compileproject_Free(project);
        assert(0 && "require ast parse to work");
    }
    if (!compileproject_CompileAllToBytecode(
            project, &moptions, fileuri, fileurilen, &error
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
        project->program, &moptions, NULL, NULL, 0
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

static char *extract_expected_result_str(const char *filecontents) {
    const int len = strlen(filecontents);
    int lastlinestart = 0;
    int lineisnoncomment = 0;
    int lineiscomment = 0;
    char linebuf[4096] = {0};
    int i = 0;
    while (i <= len) {
        if (i >= len || filecontents[i] == '\n' ||
                filecontents[i] == '\r') {
            if (lineiscomment) {
                int linelen = i - lastlinestart;
                if (linelen > (int)sizeof(linebuf) - 1)
                    linelen = sizeof(linebuf) - 1;
                memcpy(
                    linebuf,
                    &filecontents[lastlinestart],
                    linelen
                );
                linebuf[linelen] = '\0';
                while (linebuf[0] == ' ' || linebuf[0] == '\t')
                    memmove(linebuf, linebuf + 1, strlen(linebuf));
                if (linebuf[0] == '#')
                    memmove(linebuf, linebuf + 1, strlen(linebuf));
                while (linebuf[0] == ' ' || linebuf[0] == '\t')
                    memmove(linebuf, linebuf + 1, strlen(linebuf));
                int k = 0;
                while (k < (int)strlen(linebuf)) {
                    if (linebuf[k] >= 'A' && linebuf[k] <= 'Z')
                        linebuf[k] = tolower(linebuf[k]);
                    k++;
                }
                if (memcmp(linebuf, "expected result value:",
                        strlen("expected result value:")) == 0) {
                    return strdup(linebuf);
                } else if (memcmp(linebuf, "expected return value:",
                        strlen("expected return value:")) == 0) {
                    return strdup(linebuf);
                }
            }
            if (i >= len)
                break;
            lastlinestart = i + 1;
            lineisnoncomment = 0;
            lineiscomment = 0;
            i++;
            continue;
        }
        if (!lineisnoncomment && filecontents[i] == '#') {
            lineiscomment = 1;
        } else if (!lineiscomment &&
                (filecontents[i] != ' ' || filecontents[i] != '\r' ||
                 filecontents[i] != '\n' || filecontents[i] != '\t')) {
            lineisnoncomment = 1;
        }
        i++;
    }
    return NULL;
}

static int _is_expected_value_op(char c) {
    return (c == '.' || c == '*' || c == '+');
}

static int _expected_value_op_priority(char c) {
    if (c == '.')
        return 1;
    else if (c == '*')
        return 2;
    else if (c == '+')
        return 3;
    return 0;
}

int parse_expected_value_opstring(const char *orig_s, int64_t *result) {
    char *s = strdup(orig_s);
    if (!s)
        return 0;
    while (s[0] == ' ' || s[0] == '\t')
        memmove(s, s + 1, strlen(s));
    while (strlen(s) > 0 && (s[strlen(s) - 1] == ' ' ||
            s[strlen(s) - 1] == '\t'))
        s[strlen(s) - 1] = '\0';
    int most_outer_opindex = -1;
    char most_outer_opchar = 0;
    char inquote = 0;
    int i = 0;
    while (i < (int)strlen(s)) {
        if (inquote != 0 && s[i] == '\\') {
            i += 2;
            continue;
        } else if (inquote == 0 && (s[i] == '\'' ||
                s[i] == '"')) {
            inquote = s[i];
            i++;
            continue;
        } else if (inquote == s[i]) {
            inquote = 0;
            i++;
            continue;
        } else if (inquote == 0 && _is_expected_value_op(s[i])) {
            if (most_outer_opchar == 0 ||
                    (_expected_value_op_priority(
                        most_outer_opchar
                     ) < _expected_value_op_priority(s[i]))) {
                most_outer_opchar = s[i];
                most_outer_opindex = i;
            }
        }
        i++;
    }
    if (most_outer_opindex < 0) {
        // Ok, this must be a single value. Let's see what it is:
        while (s[0] == ' ' || s[0] == '\t')
            memmove(s, s + 1, strlen(s));
        while (strlen(s) > 0 && (s[strlen(s) - 1] == ' ' ||
                s[strlen(s) - 1] == ' '))
            s[strlen(s) - 1] = '\0';
        if ((s[0] >= '0' && s[0] <= '9') ||
                (s[0] == '-' &&
                 s[1] >= '0' && s[1] <= '9')) {
            int nondigit = 0;
            int k = 0;
            while (k < (int)strlen(s)) {
                if (k == 0 && s[0] == '-') {
                    k++;
                    continue;
                }
                if (s[k] < '0' || s[k] > '9') {
                    nondigit = 1;
                    break;
                }
                k++;
            }
            if (nondigit) {
                free(s);
                return 0;
            }
            *result = h64atoll(s);
            free(s);
            return 1;
        }
        free(s);
        return 0;
    }
    char *lefthalf = strdup(s);
    if (!lefthalf) {
        free(s);
        return 0;
    }
    lefthalf[most_outer_opindex] = '\0';
    char *righthalf = strdup(s);
    if (!righthalf) {
        free(s);
        free(lefthalf);
        return 0;
    }
    memmove(
        righthalf, righthalf + most_outer_opindex + 1,
        strlen(righthalf) - most_outer_opindex
    );
    if (most_outer_opchar == '.') {
        if (strcmp(righthalf, "len") != 0) {
            free(s); free(lefthalf); free(righthalf);
            return 0;
        }
        if (strlen(lefthalf) < 2 ||
                ((lefthalf[0] != '\'' ||
                  lefthalf[strlen(lefthalf) - 1] != '\'') &&
                 (lefthalf[0] != '"' ||
                  lefthalf[strlen(lefthalf) - 1] != '"'))) {
            free(s); free(lefthalf); free(righthalf);
            return 0;
        }
        *result = strlen(lefthalf) - 2;
        // ^ wrong when escaped, we don't care for now.
        free(s); free(lefthalf); free(righthalf);
        return 1;
    } else if (most_outer_opchar == '*') {
        int64_t lresult, rresult;
        if (!parse_expected_value_opstring(
                lefthalf, &lresult) ||
                !parse_expected_value_opstring(
                righthalf, &rresult)) {
            free(s); free(lefthalf); free(righthalf);
            return 0;
        }
        free(s); free(lefthalf); free(righthalf);
        *result = lresult * rresult;
        return 1;
    } else if (most_outer_opchar == '+') {
        int64_t lresult, rresult;
        if (!parse_expected_value_opstring(
                lefthalf, &lresult) ||
                !parse_expected_value_opstring(
                righthalf, &rresult)) {
            free(s); free(lefthalf); free(righthalf);
            return 0;
        }
        free(s); free(lefthalf); free(righthalf);
        *result = lresult + rresult;
        return 1;
    } else {
        assert(0);
        return 0;
    }
}

int parse_expected_value(const char *filecontents, int64_t *result) {
    char *orig_s = extract_expected_result_str(filecontents);
    if (!orig_s)
        return 0;
    char s[4096];
    memcpy(s, orig_s,
        (strlen(orig_s) + 1) < sizeof(s) ?
         (strlen(orig_s) + 1) : sizeof(s));
    s[sizeof(s) - 1] = '\0';
    free(orig_s);
    int i = 0;
    while (i <= (int)strlen(s)) {
        if (s[i] == ':') {
            memmove(s, s + i + 1, strlen(s) - i);
            break;
        }
        i++;
    }
    while (s[0] == ' '|| s[0] == '\t')
        memmove(s, s + 1, strlen(s));
    while (strlen(s) > 0 && (s[strlen(s) - 1] == ' ' ||
            s[strlen(s) - 1] == '\t'))
        s[strlen(s) - 1] = '\0';
    if (strlen(s) == 0)
        return 0;
    return parse_expected_value_opstring(s, result);
}

START_TEST (test_runchecks_files)
{
    // Quick parse tests for our own helper thing:
    int64_t resultv;
    assert(
        parse_expected_value_opstring("'hello'.len", &resultv) &&
        resultv == 5
    );
    assert(
        parse_expected_value_opstring("'hello'.len * 2", &resultv) &&
        resultv == 10
    );
    assert(
        parse_expected_value_opstring("'hello'.len + 2 * 3", &resultv) &&
        resultv == 11
    );
    assert(
        parse_expected_value_opstring("'hello'.len * 2 + 3", &resultv) &&
        resultv == 13
    );

    // Scan subfolder for tests:
    int64_t subfolderpath_len = 0;
    h64wchar *subfolderpath = AS_U32(
        "tests/run-check/", &subfolderpath_len
    );
    assert(subfolderpath != NULL);
    h64wchar **contents = NULL;
    int64_t *contentslen = NULL;
    int error = 0;
    int result = filesys32_ListFolder(
        subfolderpath, subfolderpath_len,
        &contents, &contentslen, 1, &error
    );
    assert(result != 0);
    int i = 0;
    while (contents[i]) {  // Cycle through scan results.
        h64printf(
            "test_vmexec.c: reading test file: %s\n",
            AS_U8_TMP(contents[i], contentslen[i])
        );

        // Extract test file contents:
        int error = 0;
        char *test_contents = filesys23_ContentsAsStr(
            contents[i], contentslen[i], &error
        );
        assert(test_contents != NULL);

        // Parse expected test return value from inline comment:
        int64_t expected_value = -1;
        int parseresult = parse_expected_value(
            test_contents, &expected_value
        );
        assert(parseresult != 0);
        h64printf(
            "test_vmexec.c: expected value: %" PRId64 "\n",
            expected_value
        );

        // Run the test:
        runprog(
            AS_U8_TMP(contents[i], contentslen[i]),
            test_contents,
            expected_value
        );
        free(test_contents);
        i++;
    }
    filesys32_FreeFolderList(contents, contentslen);
}
END_TEST

TESTS_MAIN(
    test_runchecks_files
)

