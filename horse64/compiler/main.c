
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compiler/astparser.h"
#include "compiler/codemodule.h"
#include "compiler/compileproject.h"
#include "compiler/disassembler.h"
#include "compiler/lexer.h"
#include "compiler/main.h"
#include "compiler/scoperesolver.h"
#include "json.h"
#include "uri.h"
#include "vmexec.h"

static int _compileargparse(
        const char *cmd,
        const char **argv, int argc, int argoffset,
        const char **fileuri, h64compilewarnconfig *wconfig,
        h64misccompileroptions *miscoptions
        ) {
    if (wconfig) warningconfig_Init(wconfig);
    int doubledashed = 0;
    int i = argoffset;
    while (i < argc) {
        if ((strlen(argv[i]) == 0 || argv[i][0] != '-' ||
                doubledashed) && fileuri && !*fileuri) {
            *fileuri = argv[i];
        } else if (strcmp(argv[i], "--") == 0) {
            doubledashed = 1;
        } else if (strcmp(cmd, "run") == 0 &&
                strcmp(argv[i], "--vmexec-debug") == 0) {
            miscoptions->vmexec_debug = 1;
            #ifdef NDEBUG
            fprintf(stderr, "horsec: warning: %s: compiled with NDEBUG, "
                "output for --vmexec-debug not compiled in\n");
            #endif
        } else if (wconfig && argv[i][0] == '-' &&
                argv[i][1] == 'W') {
            if (!warningconfig_CheckOption(
                    wconfig, argv[i])) {
                fprintf(stderr, "horsec: warning: %s: unrecognized warning "
                    "option: %s\n", cmd, argv[i]);
            }
            i++;
            continue;
        } else {
            fprintf(stderr, "horsec: error: %s: unrecognized option: %s\n",
                    cmd, argv[i]);
            return 0;
        }
        i++;
    }
    if (fileuri && !*fileuri) {
        fprintf(stderr,
            "horsec: error: %s: need argument \"file\"\n", cmd);
        return 0;
    }
    return 1;
}

static void printmsg(h64result *result, h64resultmessage *msg) {
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
    fprintf(output_fd,
        "horsec: %s: ", verb
    );
    const char *fileuri = (msg->fileuri ? msg->fileuri : result->fileuri);
    if (!fileuri && msg->line >= 0) {
        fileuri = "<unknown-file>";
    }
    if (fileuri) {
        fprintf(output_fd, "%s", fileuri);
        if (msg->line >= 0) {
            fprintf(output_fd, ":%" PRId64, msg->line);
            if (msg->column >= 0)
                fprintf(output_fd, ":%" PRId64, msg->column);
        }
        fprintf(output_fd, ": ");
    }
    fprintf(output_fd, "%s\n", msg->message);
}

#define COMPILEEX_MODE_COMPILE 1
#define COMPILEEX_MODE_RUN 2
#define COMPILEEX_MODE_CODEINFO 3
#define COMPILEEX_MODE_TOASM 4

int compiler_command_CompileEx(
        int mode, const char **argv, int argc, int argoffset
        ) {
    const char *fileuri = NULL;
    const char *_name_mode_compile = "mode";
    const char *_name_mode_run = "run";
    const char *_name_mode_cinfo = "codeinfo";
    const char *command = (
        mode == COMPILEEX_MODE_COMPILE ? _name_mode_compile : (
        mode == COMPILEEX_MODE_RUN ? _name_mode_run : _name_mode_cinfo
        ));
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    if (!_compileargparse(
            command, argv, argc,
            argoffset, &fileuri, &wconfig, &moptions
            ))
        return 0;

    char *error = NULL;
    char *project_folder_uri = compileproject_FolderGuess(
        fileuri, 1, &error
    );
    if (!project_folder_uri) {
        fprintf(stderr, "horsec: error: %s: %s\n",
                command, error);
        free(error);
        return 0;
    }
    h64compileproject *project = compileproject_New(project_folder_uri);
    free(project_folder_uri);
    project_folder_uri = NULL;
    if (!project) {
        fprintf(stderr, "horsec: error: %s: alloc failure\n",
                command);
        return 0;
    }
    h64ast *ast = NULL;
    if (!compileproject_GetAST(project, fileuri, &ast, &error)) {
        fprintf(stderr, "horsec: error: %s: %s\n",
                command, error);
        free(error);
        compileproject_Free(project);
        return 0;
    }
    if (!compileproject_CompileAllToBytecode(
            project, fileuri, &error
            )) {
        fprintf(stderr, "horsec: error: %s: %s\n",
                command, error);
        free(error);
        compileproject_Free(project);
        return 0;
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
        printmsg(project->resultmsg, &project->resultmsg->message[i]);
        i++;
    }
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
    } else if (mode == COMPILEEX_MODE_RUN) {
        if (!nosuccess) {
            int resultcode = vmexec_ExecuteProgram(
                project->program, &moptions
            );
            compileproject_Free(project);
            _exit(resultcode);
            return 1;
        } else {
            fprintf(stderr, "horsec: error: "
                "not running program due to compile errors\n");
        }
    } else {
        fprintf(stderr, "horsec: error: internal error: "
            "unhandled compile mode %d\n", mode);
    }
    compileproject_Free(project);  // This indirectly frees 'ast'!
    return !nosuccess;
}

int compiler_command_Compile(
        const char **argv, int argc, int argoffset
        ) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_COMPILE, argv, argc, argoffset
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
        char *normalizedurimsg = uri_Normalize(msg->fileuri, 1);
        if (!normalizedurimsg) {
            json_Free(currmsg);
            return 0;
        }
        if (!json_SetDictStr(
                currmsg, "file-uri", normalizedurimsg)) {
            free(normalizedurimsg);
            json_Free(currmsg);
            return 0;
        }
        free(normalizedurimsg);
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
        const char *fileuri, h64compilewarnconfig *wconfig
        ) {
    h64tokenizedfile tfile = lexer_ParseFromFile(
        fileuri, wconfig, 0
    );

    char *normalizeduri = uri_Normalize(fileuri, 1);
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
            &tfile.token[i], normalizeduri
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
        if (!json_SetDictStr(v, "file-uri", normalizeduri)) {
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
    const char *fileuri = NULL;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    if (!_compileargparse(
            "get_tokens", argv, argc, argoffset,
            &fileuri, &wconfig, &moptions
            ))
        return 0;

    jsonvalue *v = compiler_TokenizeToJSON(fileuri, &wconfig);
    if (!v) {
        printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

int compiler_command_GetAST(const char **argv, int argc, int argoffset) {
    const char *fileuri = NULL;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    if (!_compileargparse(
            "get_ast", argv, argc, argoffset,
            &fileuri, &wconfig, &moptions
            ))
        return 0;
    assert(fileuri != NULL);

    jsonvalue *v = compiler_ParseASTToJSON(fileuri, &wconfig, 0);
    if (!v) {
        printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

int compiler_command_GetResolvedAST(
        const char **argv, int argc, int argoffset
        ) {
    const char *fileuri = NULL;
    h64compilewarnconfig wconfig = {0};
    h64misccompileroptions moptions = {0};
    if (!_compileargparse(
            "get_resolved_ast", argv, argc, argoffset,
            &fileuri, &wconfig, &moptions
            ))
        return 0;
    assert(fileuri != NULL);

    jsonvalue *v = compiler_ParseASTToJSON(fileuri, &wconfig, 1);
    if (!v) {
        printf("{\"errors\":[{\"message\":\"internal error, "
               "JSON construction failed\"}]}\n");
        return 0;
    }
    char *s = json_Dump(v);
    printf("%s\n", s);
    free(s);
    json_Free(v);
    return 1;
}

jsonvalue *compiler_ParseASTToJSON(
        const char *fileuri, h64compilewarnconfig *wconfig,
        int resolve_references
        ) {
    char *error = NULL;

    char *project_folder_uri = compileproject_FolderGuess(
        fileuri, 1, &error
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
    h64compileproject *project = compileproject_New(project_folder_uri);
    free(project_folder_uri);
    project_folder_uri = NULL;
    if (!project)
        goto failedproject;
    if (wconfig)
        memcpy(&project->warnconfig, wconfig, sizeof(*wconfig));
    h64ast *tast = NULL;
    if (!compileproject_GetAST(
            project, fileuri, &tast, &error
            )) {
        compileproject_Free(project);
        project = NULL;
        goto failedproject;
    }
    if (resolve_references &&
            !scoperesolver_ResolveAST(project, tast, 0)) {
        compileproject_Free(project);
        project = NULL;
        goto failedproject;
    }

    char *normalizeduri = uri_Normalize(fileuri, 1);
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
            tast->stmt[i], normalizeduri
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
    if (!json_SetDictStr(v, "file-uri", normalizeduri)) {
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

int compiler_command_Run(const char **argv, int argc, int argoffset) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_RUN, argv, argc, argoffset
    );
}

int compiler_command_CodeInfo(const char **argv, int argc, int argoffset) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_CODEINFO, argv, argc, argoffset
    );
}

int compiler_command_ToASM(const char **argv, int argc, int argoffset) {
    return compiler_command_CompileEx(
        COMPILEEX_MODE_TOASM, argv, argc, argoffset
    );
}
