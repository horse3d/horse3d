// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bytecode.h"
#include "bytecodeserialize.h"
#include "compiler/astparser.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "compiler/disassembler.h"
#include "compiler/lexer.h"
#include "compiler/main.h"
#include "compiler/scoperesolver.h"
#include "filesys.h"
#include "filesys32.h"
#include "json.h"
#include "nonlocale.h"
#include "uri32.h"
#include "vmbinarywriter.h"
#include "vmexec.h"
#include "vmschedule.h"

static int _compileargparse(
        const char *cmd,
        const char **argv, int argc, int argoffset,
        h64wchar **fileuriorexec, int64_t *fileuriorexeclen,
        h64wchar **out_file, int64_t *out_file_len,
        h64compilewarnconfig *wconfig,
        h64misccompileroptions *miscoptions
        ) {
    if (wconfig) warningconfig_Init(wconfig);
    int doubledashed = 0;
    *out_file = NULL;
    *fileuriorexec = NULL;
    int i = argoffset;
    while (i < argc) {
        if ((strlen(argv[i]) == 0 || argv[i][0] != '-' ||
                doubledashed) && fileuriorexec && !*fileuriorexec) {
            // This is the file or exec line argument.
            if (miscoptions->from_stdin) {
                goto invalidbothfileargandfromstdin;
            }
            // For exec case, we want to combine all arguments into one:
            if (i + 1 < argc && strcmp(cmd, "exec") == 0) {
                int len = 0;
                int k = i;
                int kmax = i;
                while (k < argc) {
                    if (argv[k][0] == '-' && !doubledashed) {
                        break;
                    }
                    kmax = k;
                    len += (k > i ? 1 : 0) + strlen(argv[k]);
                    k++;
                }
                char *fileuriorexec_u8 = malloc(len + 1);
                if (fileuriorexec_u8) {
                    char *p = fileuriorexec_u8;
                    k = i;
                    while (k <= kmax) {
                        if (k > i) {
                            p[0] = ' ';
                            p++;
                        }
                        assert(p != NULL);
                        assert(argv[k] != NULL);
                        memcpy(p, argv[k], strlen(argv[k]));
                        p += strlen(argv[k]);
                        k++;
                    }
                    p[0] = '\0';
                } else {
                    h64fprintf(stderr, "horsec: error: "
                        "out of memory parsing arguments\n");
                    goto failquit;
                }
                *fileuriorexec = AS_U32(
                    fileuriorexec_u8, fileuriorexeclen
                );
                free(fileuriorexec_u8);
                i = kmax;
            } else {
                *fileuriorexec = AS_U32(
                    argv[i], fileuriorexeclen
                );
            }
            if (!*fileuriorexec) {
                h64fprintf(stderr, "horsec: error: "
                    "out of memory parsing arguments\n");
                goto failquit;
            }
        } else if (strcmp(argv[i], "--") == 0) {
            doubledashed = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            h64printf("horsec %s [options] %s\n", cmd,
                (strcmp(cmd, "exec") != 0 ? "file-path" : "code"));
            if (strcmp(cmd, "exec") != 0) {
                h64printf("   with file-path referring to a .h64 file.\n");
            } else {
                h64printf("   with code being literal horse64 code.\n");
            }
            h64printf("\n");
            h64printf("Available options:\n");
            if (strcmp(cmd, "get_tokens") != 0 &&
                    strcmp(cmd, "get_ast") != 0) {
                h64printf(
                    "  --import-debug:          Print details on import "
                    "resolution\n"
                );
            }
            h64printf(    "  --compiler-stage-debug:  "
                "Print compiler stages info\n");
            if (strcmp(cmd, "run") == 0 || strcmp(cmd, "exec") == 0) {
                h64printf(
                    "  --vmasyncjobs-debug:     Print async job "
                    "debug info\n"
                );
                h64printf(
                    "  --vmexec-debug:          Print instructions "
                    "as they run\n"
                );
                h64printf(
                    "  --vmsched-debug:         Print info about "
                    "horsevm scheduling\n"
                );
                h64printf(
                    "  --vmsched-verbose-debug: Extra detailed horsevm "
                    "scheduler info\n"
                );
                h64printf(
                    "  --vmsockets-debug:       Show debug info about "
                    "horsevm sockets\n"
                );
            }
            if (strcmp(cmd, "run") == 0 || strcmp(cmd, "exec") == 0 ||
                    strcmp(cmd, "compile") == 0 ||
                    strcmp(cmd, "get_asm") == 0 ||
                    strcmp(cmd, "codeinfo") == 0) {
                h64printf(
                    "  --from-stdin:            Take code input from stdin "
                    "instead\n"
                );
            }
            if (*fileuriorexec)
                free(*fileuriorexec);
            *fileuriorexec = NULL;
            if (*out_file)
                free(*out_file);
            *out_file = NULL;
            return 1;
        } else if ((strcmp(argv[i], "-o") == 0 ||
                strcmp(argv[i], "--output") == 0) &&
                strcmp(cmd, "compile") == 0) {
            if (*out_file) {
                h64fprintf(stderr, "horsec: error: %s: "
                    "-o/--output can only be specified "
                    "once\n", cmd);
                goto failquit;
            }
            if (i + 1 >= argc || argv[i + 1][0] == '-') {
                h64fprintf(stderr, "horsec: error: %s: "
                    "-o/--output need argument\n", cmd);
                failquit: ;
                if (*out_file)
                    free(*out_file);
                *out_file = NULL;
                if (*fileuriorexec)
                    free(*fileuriorexec);
                *fileuriorexec = NULL;
                return 0;
            }
            *out_file = utf8_to_utf32(
                argv[i + 1], strlen(argv[i + 1]),
                NULL, NULL, out_file_len
            );
            if (!*out_file) {
                h64fprintf(stderr, "horsec: error: "
                    "out of memory parsing arguments\n");
                goto failquit;
            }

            i += 2;
            continue;
        } else if (strcmp(argv[i], "--from-stdin") == 0 && (
                strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0 ||
                strcmp(cmd, "compile") == 0 ||
                strcmp(cmd, "get_asm") == 0 ||
                strcmp(cmd, "codeinfo") == 0
                )) {
            if (*fileuriorexec != NULL) {
                invalidbothfileargandfromstdin:
                h64fprintf(stderr, "horsec: error: %s: "
                    "cannot specify file argument but also "
                    "--from-stdin\n", cmd);
                goto failquit;
            }
            miscoptions->from_stdin = 1;
        } else if (strcmp(cmd, "get_tokens") != 0 &&
                   strcmp(cmd, "get_ast") != 0 &&
                   strcmp(argv[i], "--import-debug") == 0) {
            miscoptions->import_debug = 1;
        } else if ((strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0) &&
                strcmp(argv[i], "--vmsockets-debug") == 0) {
            miscoptions->vmsockets_debug = 1;
            #ifdef NDEBUG
            h64fprintf(
                stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmsockets-debug not compiled in\n", cmd
            );
            #endif
        } else if ((strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0) &&
                strcmp(argv[i], "--vmasyncjobs-debug") == 0) {
            miscoptions->vmasyncjobs_debug = 1;
            #ifdef NDEBUG
            h64fprintf(
                stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmasyncjobs-debug not compiled in\n", cmd
            );
            #endif
        } else if ((strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0) &&
                strcmp(argv[i], "--vmexec-debug") == 0) {
            miscoptions->vmexec_debug = 1;
            #ifdef NDEBUG
            h64fprintf(
                stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmexec-debug not compiled in\n", cmd
            );
            #endif
        } else if ((strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0) &&
                strcmp(argv[i], "--vmsched-debug") == 0) {
            miscoptions->vmscheduler_debug = 1;
            #ifdef NDEBUG
            h64fprintf(
                stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmsched-debug not compiled in\n", cmd
            );
            #endif
        } else if ((strcmp(cmd, "run") == 0 ||
                strcmp(cmd, "exec") == 0) &&
                strcmp(argv[i], "--vmsched-verbose-debug") == 0) {
            miscoptions->vmscheduler_debug = 1;
            miscoptions->vmscheduler_verbose_debug = 1;
            #ifdef NDEBUG
            h64fprintf(
                stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmsched-verbose-debug not compiled in\n", cmd
            );
            #endif
        } else if (strcmp(argv[i], "--compiler-stage-debug") == 0) {
            miscoptions->compiler_stage_debug = 1;
        } else if (wconfig && argv[i][0] == '-' &&
                argv[i][1] == 'W') {
            if (!warningconfig_CheckOption(
                    wconfig, argv[i])) {
                h64fprintf(
                    stderr, "horsec: warning: %s: unrecognized warning "
                    "option: %s\n", cmd, argv[i]
                );
            }
            i++;
            continue;
        } else {
            h64fprintf(
                stderr, "horsec: error: %s: unrecognized option: %s\n",
                cmd, argv[i]
            );
            goto failquit;
        }
        i++;
    }
    if (!*fileuriorexec && !miscoptions->from_stdin) {
        h64fprintf(stderr,
            "horsec: error: %s: need argument \"file\"\n", cmd
        );
        goto failquit;
    }
    if (!*out_file && strcmp(cmd, "compile") == 0) {
        h64fprintf(stderr, "horsec: error: "
            "missing -o/--output argument for compile action\n");
        goto failquit;
    }
    return 1;
}

static void printmsg(
        h64result *result, h64resultmessage *msg,
        const h64wchar *exectempfileuri, int64_t exectempfileurilen
        ) {
    const char *verb = "<unknown-msg-type>";
    if (msg->type == H64MSG_ERROR)
        verb = "error";
    if (msg->type == H64MSG_INFO)
        verb = "info";
    if (msg->type == H64MSG_WARNING)
        verb = "warning";
    FILE *output_fd = stderr;
    if (msg->type == H64MSG_INFO)
        output_fd = stdout;
    h64fprintf(output_fd,
        "horsec: %s: ", verb
    );
    // Get the file uri, both as utf32 and utf8:
    char _oomfileuri[] = "<oom converting file uri>";
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    if (msg->fileuri) {
        fileuri = msg->fileuri;
        fileurilen = msg->fileurilen;
    } else {
        fileuri = result->fileuri;
        fileurilen = result->fileurilen;
    }
    char *fileuri_u8 = NULL;
    if (fileuri && fileurilen > 0) {
        fileuri_u8 = AS_U8(
            fileuri, fileurilen
        );
        if (!fileuri_u8)
            fileuri_u8 = _oomfileuri;
    }
    // A few special values for the URI:
    if (!fileuri_u8 && msg->line >= 0) {
        fileuri_u8 = strdup("<unknown-file>");
        if (!fileuri_u8)
            fileuri_u8 = _oomfileuri;
    } else if (fileuri_u8 && exectempfileuri) {
        int isexecuri = 0;
        int result = uri32_CompareStrEx(
            fileuri, fileurilen, exectempfileuri,
            exectempfileurilen, 1,
            #if defined(_WIN32) || defined(_WIN64)
            1,
            #else
            0,
            #endif
            &isexecuri
        );
        if (!result) {
            fileuri_u8 = _oomfileuri;
        } else if (isexecuri) {
            fileuri_u8 = strdup("<exec>");
            if (!fileuri_u8)
                fileuri_u8 = _oomfileuri;
        }
    }
    if (fileuri_u8) {
        h64fprintf(output_fd, "%s", fileuri_u8);
        if (msg->line >= 0) {
            h64fprintf(output_fd, ":%" PRId64, msg->line);
            if (msg->column >= 0)
                h64fprintf(output_fd, ":%" PRId64, msg->column);
        }
        h64fprintf(output_fd, ": ");
    } else {
        h64fprintf(output_fd, "<URI error>: ");
    }
    h64fprintf(output_fd, "%s\n", msg->message);
    if (fileuri_u8 != _oomfileuri)
        free(fileuri_u8);
}


#define COMPILEEX_MODE_COMPILE 1
#define COMPILEEX_MODE_RUN 2
#define COMPILEEX_MODE_CODEINFO 3
#define COMPILEEX_MODE_TOASM 4
#define COMPILEEX_MODE_EXEC 5

int compiler_command_CompileEx(
        int mode, const char **argv, int argc, int argoffset,
        int *return_int
        ) {
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    h64wchar *execarg = NULL;
    int64_t execarglen = 0;
    const char *_name_mode_compile = "compile";
    const char *_name_mode_run = "run";
    const char *_name_mode_exec = "exec";
    const char *_name_mode_cinfo = "codeinfo";
    const char *_name_mode_getasm = "get_asm";
    const char *command = (
        mode == COMPILEEX_MODE_COMPILE ? _name_mode_compile : (
        mode == COMPILEEX_MODE_RUN ? _name_mode_run : (
        mode == COMPILEEX_MODE_EXEC ? _name_mode_exec : (
        mode == COMPILEEX_MODE_TOASM ? _name_mode_getasm :
        _name_mode_cinfo
        )))
    );
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    h64wchar *fileuriorexec = NULL;
    int64_t fileuriorexeclen = 0;

    h64wchar *outputfile = NULL;
    int64_t outputfilelen = 0;
    h64wchar *tempfilepath = NULL;
    int64_t tempfilepathlen = 0;
    h64wchar *tempfilefolder = NULL;
    int64_t tempfilefolderlen = 0;
    if (!_compileargparse(
            command, argv, argc, argoffset,
            &fileuriorexec, &fileuriorexeclen,
            &outputfile, &outputfilelen, &wconfig, &moptions
            ))
        return 0;
    assert(
        (outputfile == NULL && mode != COMPILEEX_MODE_COMPILE) ||
        (outputfile != NULL && mode == COMPILEEX_MODE_COMPILE)
    );
    assert(fileuriorexec != NULL || moptions.from_stdin);
    if (mode == COMPILEEX_MODE_EXEC || moptions.from_stdin) {
        execarg = fileuriorexec;
        execarglen = fileuriorexeclen;
        fileuriorexec = NULL;
        fileuriorexeclen = 0;
        if (moptions.from_stdin) {
            int bufsize = 4096;
            int buffill = 0;
            char *buf = malloc(bufsize);
            if (!buf) {
                free(fileuriorexec);
                free(outputfile);
                h64fprintf(
                    stderr, "horsec: error: OOM reading from stdin\n"
                );
                return 0;
            }
            while (!feof(stdin) && !ferror(stdin)) {
                if (buffill >= bufsize - 64) {
                    int newbufsize = bufsize * 2;
                    char *newbuf = realloc(buf, newbufsize);
                    if (!newbuf) {
                        free(fileuriorexec);
                        free(outputfile);
                        h64fprintf(stderr, "horsec: error: "
                            "OOM reading from stdin\n");
                        return 0;
                    }
                    buf = newbuf;
                    bufsize = newbufsize;
                }
                ssize_t rbytes = fread(
                    buf + buffill, 1, bufsize - buffill,
                    stdin
                );
                if (rbytes <= 0)
                    break;
                buffill += rbytes;
            }
            free(execarg);
            assert(buffill + 1 < bufsize);
            buf[buffill] = '\0';
            execarg = AS_U32(
                buf, &execarglen
            );
        }
        FILE *tempfile = NULL;
        {
            int64_t prefixlen = 0;
            h64wchar *prefix = AS_U32("code", &prefixlen);
            int64_t suffixlen = 0;
            h64wchar *suffix = AS_U32(".h64", &suffixlen);
            if (!prefix || !suffix) {
                free(prefix);  free(suffix);
                h64fprintf(stderr, "horsec: error: out of memory "
                    "creating temporary file\n");
                goto freeanderrorquit;
            }
            tempfile = filesys32_TempFile(
                1, 0, prefix, prefixlen, suffix, suffixlen,
                &tempfilefolder, &tempfilefolderlen,
                &tempfilepath, &tempfilepathlen
            );
            free(prefix);  free(suffix);
        }
        if (!tempfile) {
            assert(tempfilefolder == NULL);
            h64fprintf(stderr, "horsec: error: input/output error "
                "with temporary file\n");
            goto freeanderrorquit;
        }
        const char *execargu8 = AS_U8_TMP(execarg, execarglen);
        if (!execargu8) {
            fclose(tempfile);
            h64fprintf(stderr, "horsec: error: out of memory"
                "while writing temporary file\n");
            goto freeanderrorquit;
        }
        ssize_t written = fwrite(
            execargu8, 1, strlen(execargu8), tempfile
        );
        if (written < (ssize_t)strlen(execargu8)) {
            fclose(tempfile);
            h64fprintf(stderr, "horsec: error: input/output error "
                "with temporary file\n");
            goto freeanderrorquit;
        }
        fclose(tempfile);
        fileuri = uri32_Normalize(
            tempfilepath, tempfilepathlen, 1, &fileurilen
        );
        if (!fileuri) {
            h64fprintf(stderr, "horsec: error: out of memory or "
                "internal error with temporary file\n");
            freeanderrorquit:
            if (tempfilepath) {
                int _ignoreerr = 0;
                filesys32_RemoveFileOrEmptyDir(
                    tempfilepath, tempfilepathlen, &_ignoreerr);
                filesys32_RemoveFolderRecursively(
                    tempfilefolder, tempfilefolderlen, &_ignoreerr
                );
                free(tempfilepath);
                free(tempfilefolder);
            }
            free(fileuriorexec);
            free(outputfile);
            free(execarg);
            return 0;
        }
    } else {
        fileuri = fileuriorexec;
        fileurilen = fileuriorexeclen;
        fileuriorexec = NULL;
    }

    char *error = NULL;
    int64_t project_folder_uri_len = 0;
    h64wchar *project_folder_uri = NULL;
    if (mode != COMPILEEX_MODE_EXEC && !moptions.from_stdin) {
        project_folder_uri = compileproject_FolderGuess(
            fileuri, fileurilen, 1,
            &project_folder_uri_len, &error
        );
    } else {
        project_folder_uri = strdupu32(
            tempfilefolder, tempfilefolderlen, &project_folder_uri_len
        );
    }
    if (!project_folder_uri) {
        h64fprintf(stderr, "horsec: error: %s: %s\n",
                   command, (error ? error : "out of memory"));
        goto freeanderrorquit;
    }
    h64compileproject *project = compileproject_New(
        project_folder_uri, project_folder_uri_len
    );
    free(project_folder_uri);
    project_folder_uri = NULL;
    if (!project) {
        h64fprintf(stderr, "horsec: error: %s: alloc failure\n",
                   command);
        goto freeanderrorquit;
    }
    h64ast *ast = NULL;
    if (!compileproject_GetAST(
            project, fileuri, fileurilen, &ast, &error
            )) {
        h64fprintf(stderr, "horsec: error: %s: %s\n",
                   command, error);
        free(error);
        compileproject_Free(project);
        goto freeanderrorquit;
    }
    if (!compileproject_CompileAllToBytecode(
            project, &moptions, fileuri, fileurilen, &error
            )) {
        h64fprintf(stderr, "horsec: error: %s: %s\n",
                   command, error);
        free(error);
        compileproject_Free(project);
        goto freeanderrorquit;
    }

    // Examine & print message:
    int haderrormessages = 0;
    int i = 0;
    while (i < project->resultmsg->message_count) {
        if (project->resultmsg->message[i].type == H64MSG_ERROR)
            haderrormessages = 1;
        if (mode == COMPILEEX_MODE_RUN &&
                project->resultmsg->message[i].type != H64MSG_ERROR) {
            // When running directly, don't print if not a compile error
            i++;
            continue;
        }
        printmsg(
            project->resultmsg, &project->resultmsg->message[i],
            ((mode == COMPILEEX_MODE_EXEC ||
              moptions.from_stdin) ? tempfilepath : NULL),
            ((mode == COMPILEEX_MODE_EXEC ||
              moptions.from_stdin) ? tempfilepathlen : 0LL)
        );
        i++;
    }
    if (tempfilepath) {
        int _ignoreerr = 0;
        filesys32_RemoveFileOrEmptyDir(
            tempfilepath, tempfilepathlen, &_ignoreerr);
        filesys32_RemoveFolderRecursively(
            tempfilefolder, tempfilefolderlen, &_ignoreerr
        );
        free(tempfilepath);
        free(tempfilefolder);
    }
    free(fileuriorexec);
    fileuriorexec = NULL;
    free(fileuri);
    fileuri = NULL;
    free(execarg);
    execarg = NULL;
    int nosuccess = (
        haderrormessages || !ast->resultmsg.success ||
        !project->resultmsg->success
    );

    // Do final post-compile action depending on compile mode:
    if (mode == COMPILEEX_MODE_CODEINFO) {
        if (!nosuccess)
            h64program_PrintBytecodeStats(project->program);
    } else if (mode == COMPILEEX_MODE_TOASM) {
        int haveinstructions = 0;
        if (nosuccess) {
            int i = 0;
            while (i < project->program->func_count) {
                if (project->program->func[i].instructions_bytes > 0)
                    haveinstructions = 1;
                i++;
            }
        }
        if (!nosuccess || haveinstructions)
            disassembler_DumpToStdout(project->program);
    } else if (mode == COMPILEEX_MODE_RUN ||
            mode == COMPILEEX_MODE_EXEC) {
        if (!nosuccess) {
            int resultcode = vmschedule_ExecuteProgram(
                project->program, &moptions
            );
            compileproject_Free(project);
            if (return_int)
                *return_int = resultcode;
            free(outputfile);
            return 1;
        } else {
            h64fprintf(stderr, "horsec: error: "
                "not running program due to compile errors\n");
        }
    } else if (mode == COMPILEEX_MODE_COMPILE) {
        char *errormsg = NULL;
        int writeresult = vmbinarywriter_WriteProgram(
            outputfile, outputfilelen, project->program,
            &errormsg
        );
        if (!writeresult) {
            h64fprintf(stderr, "horsec: error: "
                "failed to write result: %s\n", errormsg);
            free(errormsg);
            compileproject_Free(project);  // This indirectly frees 'ast'!
            free(outputfile);
            return 0;
        }
        nosuccess = 0;
    } else {
        h64fprintf(stderr, "horsec: error: internal error: "
            "unhandled compile mode %d\n", mode);
    }
    compileproject_Free(project);  // This indirectly frees 'ast'!
    free(outputfile);
    return !nosuccess;
}

int compiler_command_Compile(
        const char **argv, int argc, int argoffset
        ) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_COMPILE, argv, argc, argoffset, NULL
    );
}

int compiler_AddResultMessageAsJson(
        jsonvalue *errorlist, jsonvalue *warninglist,
        jsonvalue *infolist, h64resultmessage *msg
        ) {
    jsonvalue *currmsg = json_Dict();
    if (!json_SetDictStr(
            currmsg, "message", msg->message)) {
        json_Free(currmsg);
        return 0;
    }
    if (msg->fileuri) {
        int64_t normalizedurilen = 0;
        h64wchar *normalizedurimsg = uri32_Normalize(
            msg->fileuri, msg->fileurilen, 1, &normalizedurilen
        );
        if (!normalizedurimsg) {
            json_Free(currmsg);
            return 0;
        }
        char *normalizeduriu8 = AS_U8(
            normalizedurimsg, normalizedurilen
        );
        if (!normalizeduriu8) {
            free(normalizedurimsg);
            json_Free(currmsg);
            return 0;
        }
        free(normalizedurimsg);
        if (!json_SetDictStr(
                currmsg, "file-uri", normalizeduriu8)) {
            free(normalizeduriu8);
            json_Free(currmsg);
            return 0;
        }
        free(normalizeduriu8);
    }
    if (msg->line >= 0) {
        if (!json_SetDictInt(
                currmsg, "line", msg->line)) {
            json_Free(currmsg);
            return 0;
        }
        if (msg->column >= 0) {
            if (!json_SetDictInt(
                    currmsg, "column", msg->column)) {
                json_Free(currmsg);
                return 0;
            }
        }
    }
    if (msg->type == H64MSG_ERROR) {
        if (!json_AddToList(errorlist, currmsg)) {
            json_Free(currmsg);
            return 0;
        }
    } else if (msg->type == H64MSG_WARNING) {
        if (!json_AddToList(warninglist, currmsg)) {
            json_Free(currmsg);
            return 0;
        }
    } else if (msg->type == H64MSG_INFO) {
        if (!json_AddToList(infolist, currmsg)) {
            json_Free(currmsg);
            return 0;
        }
    } else {
        json_Free(currmsg);
        return 0;
    }
    return 1;
}

jsonvalue *compiler_TokenizeToJSON(
        ATTR_UNUSED h64misccompileroptions *moptions,
        const h64wchar *fileuri, int64_t fileurilen,
        h64compilewarnconfig *wconfig
        ) {
    h64tokenizedfile tfile = {0};
    {
        uri32info *fileuri_info = uri32_ParseExU8Protocol(
            fileuri, fileurilen,
            "file", URI32_PARSEEX_FLAG_GUESSPORT
        );
        if (!fileuri_info)
            return NULL;
        tfile = lexer_ParseFromFile(
            fileuri_info, wconfig
        );
        uri32_Free(fileuri_info);
    }
    int64_t normalizedurilen = 0;
    h64wchar *normalizeduri = uri32_Normalize(
        fileuri, fileurilen, 1, &normalizedurilen
    );
    if (!normalizeduri) {
        result_FreeContents(&tfile.resultmsg);
        lexer_FreeFileTokens(&tfile);
        return NULL;
    }

    jsonvalue *v = json_Dict();
    jsonvalue *errorlist = json_List();
    jsonvalue *warninglist = json_List();
    jsonvalue *infolist = json_List();
    jsonvalue *tokenlist = json_List();
    int haderrormessages = 0;
    int failure = 0;
    int i = 0;
    while (i < tfile.token_count) {
        jsonvalue *token = lexer_TokenToJSON(
            &tfile.token[i], normalizeduri, normalizedurilen
        );
        if (!token) {
            failure = 1;
            break;
        } else {
            if (!json_AddToList(tokenlist, token)) {
                json_Free(token);
                failure = 1;
                break;
            }
        }
        i++;
    }
    lexer_FreeFileTokens(&tfile);

    i = 0;
    while (i < tfile.resultmsg.message_count) {
        if (!compiler_AddResultMessageAsJson(
                errorlist, warninglist, infolist,
                &tfile.resultmsg.message[i])) {
            failure = 1;
            break;
        }
        if (tfile.resultmsg.message[i].type == H64MSG_ERROR) {
            haderrormessages = 1;
            assert(!tfile.resultmsg.success);
        }
        i++;
    }
    result_FreeContents(&tfile.resultmsg);

    if (!json_SetDictBool(v, "success", !(haderrormessages ||
            !tfile.resultmsg.success))) {
        failure = 1;
    }
    if (!failure) {
        if (!json_SetDict(v, "errors", errorlist)) {
            failure = 1;
        } else {
            errorlist = NULL;
        }
        if (!json_SetDict(v, "warnings", warninglist)) {
            failure = 1;
        } else {
            warninglist = NULL;
        }
        if (!json_SetDict(v, "information", infolist)) {
            failure = 1;
        } else {
            infolist = NULL;
        }
        if (!json_SetDictStr(v, "file-uri",
                AS_U8_TMP(normalizeduri, normalizedurilen))) {
            failure = 1;
        }
        if (!json_SetDict(v, "tokens", tokenlist)) {
            failure = 1;
        } else {
            tokenlist = NULL;
        }
    }
    if (failure) {
        json_Free(errorlist);
        json_Free(warninglist);
        json_Free(infolist);
        json_Free(tokenlist);
        json_Free(v);
        free(normalizeduri);
        return NULL;
    }
    free(normalizeduri);
    return v;
}

int compiler_command_GetTokens(const char **argv, int argc, int argoffset) {
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    h64wchar *output_file = NULL;
    int64_t output_file_len = 0;
    if (!_compileargparse(
            "get_tokens", argv, argc, argoffset,
            &fileuri, &fileurilen, &output_file, &output_file_len,
            &wconfig, &moptions
            ))
        return 0;
    assert(fileuri != NULL && output_file == NULL);

    jsonvalue *v = compiler_TokenizeToJSON(
        &moptions, fileuri, fileurilen, &wconfig
    );
    free(fileuri);
    if (!v) {
        h64printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    h64printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

int compiler_command_GetAST(const char **argv, int argc, int argoffset) {
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    h64wchar *output_file = NULL;
    int64_t output_file_len = 0;
    if (!_compileargparse(
            "get_ast", argv, argc, argoffset,
            &fileuri, &fileurilen, &output_file, &output_file_len,
            &wconfig, &moptions
            ))
        return 0;
    assert(fileuri != NULL && output_file == NULL);

    jsonvalue *v = compiler_ParseASTToJSON(
        &moptions, fileuri, fileurilen, &wconfig, 0
    );
    free(fileuri);
    if (!v) {
        h64printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    h64printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

int compiler_command_GetResolvedAST(
        const char **argv, int argc, int argoffset
        ) {
    h64wchar *fileuri = NULL;
    int64_t fileurilen = 0;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    h64wchar *output_file = NULL;
    int64_t output_file_len = 0;
    if (!_compileargparse(
            "get_resolved_ast", argv, argc, argoffset,
            &fileuri, &fileurilen, &output_file, &output_file_len,
            &wconfig, &moptions
            ))
        return 0;
    assert(fileuri != NULL && output_file == NULL);

    jsonvalue *v = compiler_ParseASTToJSON(
        &moptions, fileuri, fileurilen, &wconfig, 1
    );
    free(fileuri);
    if (!v) {
        h64printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    h64printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

jsonvalue *compiler_ParseASTToJSON(
        h64misccompileroptions *moptions,
        const h64wchar *fileuri, int64_t fileurilen,
        h64compilewarnconfig *wconfig,
        int resolve_references
        ) {
    char *error = NULL;

    int64_t project_folder_uri_len = 0;
    h64wchar *project_folder_uri = compileproject_FolderGuess(
        fileuri, fileurilen, 1, &project_folder_uri_len, &error
    );
    if (!project_folder_uri) {
        failedproject: ;
        int fail = 1;
        jsonvalue *v = json_Dict();
        jsonvalue *infolist = json_List();
        jsonvalue *warninglist = json_List();
        jsonvalue *errorlist = json_List();
        jsonvalue *errobj = json_Dict();
        if (!json_SetDictStr(
                errobj, "message",
                (error ? error : "basic I/O or allocation failure")
                )) {
            fail = 1;
        }
        if (error) free(error);
        if (!json_AddToList(errorlist, errobj)) {
            fail = 1;
            json_Free(errobj);
        }
        if (!json_SetDict(v, "errors", errorlist)) {
            fail = 1;
            json_Free(errorlist);
        }
        if (!json_SetDict(v, "warnings", warninglist)) {
            fail = 1;
            json_Free(warninglist);
        }
        if (!json_SetDict(v, "information", infolist)) {
            fail = 1;
            json_Free(infolist);
        }
        return v;
    }
    h64compileproject *project = compileproject_New(
        project_folder_uri, project_folder_uri_len
    );
    free(project_folder_uri);
    project_folder_uri = NULL;
    if (!project)
        goto failedproject;
    if (wconfig)
        memcpy(&project->warnconfig, wconfig, sizeof(*wconfig));
    h64ast *tast = NULL;
    if (!compileproject_GetAST(
            project, fileuri, fileurilen, &tast, &error
            )) {
        compileproject_Free(project);
        project = NULL;
        goto failedproject;
    }
    if (resolve_references &&
            !scoperesolver_ResolveAST(project, moptions, tast, 0)) {
        compileproject_Free(project);
        project = NULL;
        goto failedproject;
    }
    int64_t normalizedurilen = 0;
    h64wchar *normalizeduri = uri32_Normalize(
        fileuri, fileurilen, 1, &normalizedurilen
    );
    if (!normalizeduri) {
        compileproject_Free(project);
        project = NULL;
        return 0;
    }

    jsonvalue *v = json_Dict();
    jsonvalue *errorlist = json_List();
    jsonvalue *warninglist = json_List();
    jsonvalue *infolist = json_List();
    jsonvalue *exprlist = json_List();
    int haderrormessages = 0;
    int failure = 0;
    int i = 0;
    while (i < tast->stmt_count) {
        jsonvalue *expr = ast_ExpressionToJSON(
            tast->stmt[i], normalizeduri, normalizedurilen
        );
        if (!expr) {
            failure = 1;
            break;
        } else {
            if (!json_AddToList(exprlist, expr)) {
                json_Free(expr);
                failure = 1;
                break;
            }
        }
        i++;
    }

    i = 0;
    while (i < project->resultmsg->message_count) {
        if (!compiler_AddResultMessageAsJson(
                errorlist, warninglist, infolist,
                &project->resultmsg->message[i])) {
            failure = 1;
            break;
        }
        if (project->resultmsg->message[i].type == H64MSG_ERROR) {
            haderrormessages = 1;
            assert(!project->resultmsg->success);
        }
        i++;
    }

    if (!json_SetDictBool(v, "success", !(haderrormessages ||
            !project->resultmsg->success))) {
        failure = 1;
    }
    if (!json_SetDict(v, "errors", errorlist)) {
        failure = 1;
        json_Free(errorlist);
    }
    if (!json_SetDict(v, "warnings", warninglist)) {
        failure = 1;
        json_Free(warninglist);
    }
    if (!json_SetDict(v, "information", infolist)) {
        failure = 1;
        json_Free(infolist);
    }
    if (!json_SetDictStr(v, "file-uri", AS_U8_TMP(
            normalizeduri, normalizedurilen))) {
        failure = 1;
    }
    jsonvalue *vscope = scope_ScopeToJSON(
        &tast->scope
    );
    if (!json_SetDict(v, "scope", vscope)) {
        failure = 1;
        json_Free(vscope);
    }
    if (!json_SetDict(v, "ast", exprlist)) {
        failure = 1;
        json_Free(exprlist);
    }
    compileproject_Free(project);  // indirectly frees 'tast'!
    project = NULL;
    if (failure) {
        json_Free(v);
        free(normalizeduri);
        return NULL;
    }
    free(normalizeduri);
    return v;
}

int compiler_command_Run(
        const char **argv, int argc, int argoffset, int *return_int
        ) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_RUN, argv, argc, argoffset, return_int
    );
}

int compiler_command_Exec(
        const char **argv, int argc, int argoffset, int *return_int
        ) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_EXEC, argv, argc, argoffset, return_int
    );
}

int compiler_command_CodeInfo(const char **argv, int argc, int argoffset) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_CODEINFO, argv, argc, argoffset, NULL
    );
}

int compiler_command_ToASM(const char **argv, int argc, int argoffset) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_TOASM, argv, argc, argoffset, NULL
    );
}
