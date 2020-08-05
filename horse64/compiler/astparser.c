// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// #define H64AST_DEBUG

#include "bytecode.h"
#include "compiler/ast.h"
#include "compiler/asthelpers.h"
#include "compiler/astparser.h"
#include "compiler/compileproject.h"
#include "compiler/globallimits.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"
#include "poolalloc.h"
#include "uri.h"


static int64_t _refline(tsinfo *tokenstreaminfo, h64token *token, int i) {
    ptrdiff_t offset = (token - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int)offset) / (int)sizeof(*token);
    if (i > tokenstreaminfo->token_count - starti - 1)
        i = tokenstreaminfo->token_count - starti - 1;
    if (i < 0)
        return 0;
    return token[i].line;
}

static int64_t _refcol(tsinfo *tokenstreaminfo, h64token *token, int i) {
    ptrdiff_t offset = (token - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int64_t)offset) / (int64_t)sizeof(*token);
    if (i > tokenstreaminfo->token_count - starti - 1)
        i = tokenstreaminfo->token_count - starti - 1;
    if (i < 0)
        return 0;
    return token[i].column;
}

static char _reftokname_none[] = "end of file";

static const char *_reftokname(tsinfo *tokenstreaminfo, h64token *token, int i) {
    ptrdiff_t offset = (token - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int64_t)offset) / (int64_t)sizeof(*token);
    if (i > tokenstreaminfo->token_count - starti - 1 || i < 0) {
        return _reftokname_none;
    }
    return lexer_TokenTypeToStr(token[i].type);
}

const char *_shortenedname(
        char *buf, const char *name
        ) {
    int copylen = strlen(name) + 1;
    if (copylen > 32) {
        memcpy(buf, name, 32);
        memcpy(buf + 32, "...", 4);
        return buf;
    }
    memcpy(buf, name, copylen);
    return buf;
}

static int IdentifierIsReserved(const char *identifier) {
    if (strcmp(identifier, "self") == 0 ||
            strcmp(identifier, "base") == 0) {
        return 1;
    }
    return 0;
}

static const char *_describetoken(
        char *buf, tsinfo *tokenstreaminfo, h64token *token, int i) {
    ptrdiff_t offset = (token - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int64_t)offset) / (int64_t)sizeof(*token);
    if (!token || i > tokenstreaminfo->token_count - starti - 1 || i < 0)
        return _reftokname_none;
    int maxlen = 64;
    snprintf(buf, maxlen - 1, "%s", _reftokname(
        tokenstreaminfo, token, i));
    if (token[i].type == H64TK_BRACKET) {
        snprintf(
            buf, maxlen - 1,
            "\"%c\"", token[i].char_value
        );
    } else if (token[i].type == H64TK_BINOPSYMBOL ||
            token[i].type == H64TK_UNOPSYMBOL) {
        snprintf(buf, maxlen - 1, "\"%s\"", operator_OpPrintedAsStr(
            token[i].int_value));
    } else if (token[i].type == H64TK_KEYWORD) {
        snprintf(buf, maxlen - 1, "keyword \"%s\"", token[i].str_value);
    } else if (token[i].type == H64TK_IDENTIFIER) {
        snprintf(buf, maxlen - 1, "identifier \"%s\"", token[i].str_value);
        if (strlen(buf) > 35)
            memcpy(buf + 32, "...\"", strlen("...\"") + 1);
    } else if (token[i].type == H64TK_CONSTANT_INT) {
        snprintf(
            buf, maxlen - 1,
            "%" PRId64, token[i].int_value
        );
    }
    buf[maxlen - 1] = '\0';
    return buf;
}

static h64scopedef *_getSameScopeShadowedDefinition(
        h64parsethis *parsethis, const char *identifier) {
    h64scopedef *duplicateuse = scope_QueryItem(
        parsethis->scope, identifier, 0
    );
    if (duplicateuse) {
        h64expression *expr = duplicateuse->declarationexpr;
        if (expr->type == H64EXPRTYPE_INLINEFUNCDEF)
            return NULL;
        if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
            if (expr->funcdef.name &&
                    strcmp(expr->funcdef.name, identifier) == 0)
                return duplicateuse;
            return NULL;
        }
        return duplicateuse;
    }
    return NULL;
}

#define RECOVERFLAGS_MUSTFORWARD 1
#define RECOVERFLAGS_NORMAL 0

h64expression *ast_AllocExpr(h64ast *ast) {
    if (!ast)
        return NULL;
    if (!ast->ast_expr_alloc) {
        ast->ast_expr_alloc = poolalloc_New(sizeof(h64expression));
        if (!ast->ast_expr_alloc)
            return NULL;
    }
    return poolalloc_malloc(ast->ast_expr_alloc, 0);
}
void ast_ParseRecover_FindNextStatement(
        tsinfo *tokenstreaminfo, h64token *tokens,
        int max_tokens_touse, int *k, int flags
        ) {
    ptrdiff_t offset = (tokens - tokenstreaminfo->token);
    assert(offset >= 0);
    int offseti = ((int64_t)offset) / (int64_t)sizeof(*tokens);
    int brackets_depth = 0;
    int i = *k;
    int initiali = i;
    while (i < max_tokens_touse &&
            i < tokenstreaminfo->token_count - offseti) {
        if (tokens[i].type == H64TK_BRACKET) {
            char c = tokens[i].char_value;
            if (c == '{' || c == '[' || c == '(') {
                brackets_depth++;
            } else {
                brackets_depth--;
                if (brackets_depth < 0) brackets_depth = 0;
                if (brackets_depth == 0 && (c == '}' || c == ')') &&
                        i + 1 < max_tokens_touse &&
                        (tokens[i + 1].type == H64TK_IDENTIFIER ||
                         (tokens[i + 1].type == H64TK_BRACKET &&
                          tokens[i + 1].char_value == '}'))) {
                    if (i + 1 < max_tokens_touse &&
                            tokens[i + 1].type == H64TK_BRACKET &&
                            tokens[i + 1].char_value != '}') {
                        // Next char can't be end of statement yet
                        i++;
                        continue;
                    }
                    *k = i + 1;  // start statement past bracket
                    return;
                }
            }
        } else if (tokens[i].type == H64TK_KEYWORD &&
                (i > initiali ||
                 (flags & RECOVERFLAGS_MUSTFORWARD) == 0)) {
            char *s = tokens[i].str_value;
            if (strcmp(s, "if") == 0 ||
                    strcmp(s, "var") == 0 ||
                    strcmp(s, "const") == 0 ||
                    strcmp(s, "for") == 0 ||
                    strcmp(s, "while") == 0 ||
                    strcmp(s, "func") == 0 ||
                    strcmp(s, "do") == 0 ||
                    strcmp(s, "class") == 0 ||
                    strcmp(s, "with") == 0) {
                *k = i;
                return;
            }
        } else if ((tokens[i].type == H64TK_CONSTANT_INT ||
                tokens[i].type == H64TK_CONSTANT_STRING ||
                tokens[i].type == H64TK_CONSTANT_FLOAT ||
                tokens[i].type == H64TK_CONSTANT_BOOL ||
                tokens[i].type == H64TK_CONSTANT_NONE ||
                tokens[i].type == H64TK_IDENTIFIER) && brackets_depth == 0) {
            int i2 = i + 1;
            if (i2 < tokenstreaminfo->token_count - offseti - 1 &&
                    i2 < max_tokens_touse &&
                    tokens[i2].type == H64TK_IDENTIFIER) {
                *k = i2;
                return;
            }
        }
        i++;
    }
    *k = i;
}

void ast_ParseRecover_FindEndOfBlock(
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse, int *k
        ) {
    ptrdiff_t offset = (tokens - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int)offset) / (int)sizeof(*tokens);
    int brackets_depth = 0;
    int i = *k;
    while (i < max_tokens_touse &&
            i < tokenstreaminfo->token_count - starti - 1) {
        if (tokens[i].type == H64TK_BRACKET) {
            char c = tokens[i].char_value;
            if (c == '{' || c == '[' || c == '(') {
                brackets_depth++;
            } else {
                brackets_depth--;
                if (brackets_depth < -1) brackets_depth = -1;
                if (brackets_depth == -1 && c == '}') {  // -1 = leave scope
                    *k = i;
                    return;
                } else if (brackets_depth < 0) {
                    brackets_depth = 0;  // probably not truly left scope
                }
            }
        } else if (tokens[i].type == H64TK_BINOPSYMBOL &&
                (tokens[i].int_value == H64OP_CALL ||
                 tokens[i].int_value == H64OP_INDEXBYEXPR)) {
            brackets_depth++;
        } else if (tokens[i].type == H64TK_IDENTIFIER) {
            char *s = tokens[i].str_value;
            if (strcmp(s, "class") == 0 ||
                    strcmp(s, "import") == 0) {
                *k = i;
                return;
            }
        }
        i++;
    }
    *k = i;
}

#define INLINEMODE_NONGREEDY 0
#define INLINEMODE_GREEDY 1

int _ast_ParseFunctionArgList_Ex(
        h64parsecontext *context,
        h64parsethis *parsethis,
        h64expression *funcdefexpr,
        int is_call,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    assert(is_call == (funcdefexpr == NULL));
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }
    memset(out_funcargs, 0, sizeof(*out_funcargs));

    int i = 0;
    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[128];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            context->resultmsg, buf, fileuri,
            _refline(context->tokenstreaminfo, tokens, i),
            _refcol(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    assert(
        (tokens[0].type == H64TK_BRACKET &&
         tokens[0].char_value == '(') ||
        (tokens[0].type == H64TK_BINOPSYMBOL &&
         tokens[0].int_value == H64OP_CALL));
    i++;
    while (1) {
        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_BRACKET &&
                tokens[i].char_value == ')') {
            i++;
            break;
        }
        char **new_arg_names = realloc(
            out_funcargs->arg_name,
            sizeof(*new_arg_names) * (out_funcargs->arg_count + 1)
        );
        if (!new_arg_names) {
            oom:
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            ast_ClearFunctionArgsWithoutFunc(
                out_funcargs, parsethis->scope, 0
            );
            return 0;
        }
        out_funcargs->arg_name = new_arg_names;
        h64expression **new_arg_values = realloc(
            out_funcargs->arg_value,
            sizeof(*new_arg_values) * (out_funcargs->arg_count + 1)
        );
        if (!new_arg_values)
            goto oom;
        out_funcargs->arg_value = new_arg_values;

        char *arg_name = NULL;
        char *kwarg_name = NULL;

        if (i + 1 < max_tokens_touse &&
                tokens[i].type == H64TK_IDENTIFIER &&
                tokens[i + 1].type == H64TK_BINOPSYMBOL &&
                tokens[i + 1].int_value == H64OP_ASSIGN) {
            kwarg_name = strdup(tokens[i].str_value);
            if (!kwarg_name)
                goto oom;
            i += 2;
        } else if (!is_call &&
                    i + 1 < max_tokens_touse &&
                    tokens[i].type == H64TK_IDENTIFIER &&
                    (tokens[i + 1].type == H64TK_COMMA || (
                     tokens[i + 1].type == H64TK_BRACKET &&
                     tokens[i + 1].char_value == ')'))) {
            arg_name = strdup(tokens[i].str_value);
            if (!arg_name)
                goto oom;
            out_funcargs->arg_name[out_funcargs->arg_count] = arg_name;
            out_funcargs->arg_value[out_funcargs->arg_count] = NULL;
            out_funcargs->arg_count++;
            i++;
            if (tokens[i].type == H64TK_COMMA) i++;
            int scopeoom = 0;
            if (!is_call && !scope_AddItem(
                    parsethis->scope, arg_name, funcdefexpr, &scopeoom
                    )) {
                if (scopeoom) {
                    goto oom;
                } else {
                    additemfail:
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, "INTERNAL ERROR, failed to "
                            "scope-add function param", fileuri,
                            -1, -1
                            ))
                        goto oom;
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 1;
                    ast_ClearFunctionArgsWithoutFunc(
                        out_funcargs, parsethis->scope, 0
                    );
                    return 0;
                }
            }
            continue;
        }
        if (!is_call && !kwarg_name) {
            free(arg_name);
            char buf[512]; char describebuf[64];
            int bugindex = i;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier for function argument name",
                    _describetoken(describebuf,
                        context->tokenstreaminfo, tokens, i)
                );
            } else {
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected ',' or ')' to resume argument list",
                    _describetoken(describebuf,
                        context->tokenstreaminfo, tokens, i + 1)
                );
                bugindex++;
            }
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo,
                             tokens, bugindex),
                    _refcol(context->tokenstreaminfo,
                            tokens, bugindex)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_ClearFunctionArgsWithoutFunc(
                out_funcargs, parsethis->scope, 0
            );
            return 0;
        }
        int scopeoom = 0;
        if (!is_call && !scope_AddItem(
                parsethis->scope, kwarg_name, funcdefexpr,
                &scopeoom
                )) {
            free(arg_name);
            free(kwarg_name);
            if (scopeoom) {
                goto oom;
            } else {
                goto additemfail;
            }
        }

        int inneroom = 0;
        int innerparsefail = 0;
        h64expression *expr = NULL;
        int tlen = 0;
        h64parsethis _buf;
        assert(i > 0);
        if (i >= max_tokens_touse ||
                !ast_ParseExprInline(
                        context, newparsethis(
                            &_buf, parsethis,
                            tokens + i, max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &innerparsefail, &inneroom,
                        &expr, &tlen,
                        nestingdepth
                        )) {
            free(arg_name);
            free(kwarg_name);
            char buf[512]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected valid inline value for argument list",
                _describetoken(describebuf,
                    context->tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_ClearFunctionArgsWithoutFunc(
                out_funcargs, parsethis->scope, 0
            );
            return 0;
        }
        assert(tlen > 0 && expr != NULL);
        out_funcargs->arg_name[out_funcargs->arg_count] = kwarg_name;
        out_funcargs->arg_value[out_funcargs->arg_count] = expr;
        out_funcargs->arg_count++;
        i += tlen;
        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_COMMA)
            i++;
    }
    assert(i > 0);
    *out_tokenlen = i;
    return 1;
}

int ast_ParseFuncCallArgs(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return _ast_ParseFunctionArgList_Ex(
        context, parsethis, NULL,
        1, parsefail, outofmemory,
        out_funcargs, out_tokenlen, nestingdepth
    );
}

int ast_ParseFuncDefArgs(
        h64parsecontext *context,
        h64parsethis *parsethis,
        h64expression *funcdefexpr,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return _ast_ParseFunctionArgList_Ex(
        context, parsethis, funcdefexpr,
        0, parsefail, outofmemory,
        out_funcargs, out_tokenlen, nestingdepth
    );
}

int ast_ParseExprInlineOperator_Recurse(
        h64parsecontext *context,
        h64parsethis *parsethis,
        h64expression *lefthandside,
        int lefthandsidetokenlen,
        int precedencelevel,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        // XXX: do NOT free original lefthandside if no parse error.
        return 0;
    }
    assert(lefthandside || lefthandsidetokenlen == 0);

    #ifdef H64AST_DEBUG
    char describebuf[64];
    printf("horsec: debug: OP PARSE FROM %d, token: %s\n", 0,
         _describetoken(describebuf, tokenstreaminfo, tokens, 0));
    #endif

    int i = 0;
    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[128];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            context->resultmsg, buf, fileuri,
            _refline(context->tokenstreaminfo, tokens, i),
            _refcol(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *original_lefthand = lefthandside;
    i += lefthandsidetokenlen;
    int operatorsprocessed = 0;

    // Parse left-hand side if we don't have any yet:
    if (i == 0 && !lefthandside &&
            tokens[i].type != H64TK_UNOPSYMBOL) {
        int inneroom = 0;
        int innerparsefail = 0;
        int tlen = 0;
        h64expression *innerexpr = NULL;
        h64parsethis _buf;
        if (!ast_ParseExprInline(
                context, newparsethis(
                    &_buf, parsethis,
                    tokens, max_tokens_touse
                ),
                INLINEMODE_NONGREEDY,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                if (original_lefthand &&
                        original_lefthand != lefthandside)
                    ast_MarkExprDestroyed(original_lefthand);
                return 0;
            } else if (innerparsefail) {
                if (outofmemory) *outofmemory = 0;
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                if (original_lefthand &&
                        original_lefthand != lefthandside)
                    ast_MarkExprDestroyed(original_lefthand);
                return 0;
            }
            if (parsefail) *parsefail = 0;
            if (lefthandside &&
                    original_lefthand != lefthandside)
                ast_MarkExprDestroyed(lefthandside);
            // XXX: do NOT free original_lefthandside if no parse error.
            return 0;
        }
        if (lefthandside && original_lefthand != lefthandside)
            ast_MarkExprDestroyed(lefthandside);
        lefthandside = innerexpr;
        lefthandsidetokenlen = tlen;
        i += tlen;
    } else if (!lefthandside && tokens[i].type == H64TK_BINOPSYMBOL &&
            !IS_ASSIGN_OP(tokens[i].int_value)) {
        char buf[512]; char describebuf[64];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected left hand value before binary operator",
            _describetoken(describebuf,
                           context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, i),
                _refcol(context->tokenstreaminfo, tokens, i)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        if (original_lefthand && original_lefthand != lefthandside)
            ast_MarkExprDestroyed(original_lefthand);
        if (lefthandside) ast_MarkExprDestroyed(lefthandside);
        return 0;
    }

    #ifdef H64AST_DEBUG
    char describebufy[64];
    char *lhandside = ast_ExpressionToJSONStr(lefthandside, NULL); 
    printf("horsec: debug: "
         "GOT LEFT HAND SIDE %s AND NOW AT %d %s - "
         "current handling level: %d\n", lhandside, i,
         _describetoken(describebufy, tokenstreaminfo, tokens, i),
         precedencelevel);
    if (lhandside) free(lhandside);
    #endif

    // Deal with operators we encounter:
    while (i < max_tokens_touse) {
        if ((tokens[i].type != H64TK_BINOPSYMBOL ||
                IS_ASSIGN_OP(tokens[i].int_value)) &&
                tokens[i].type != H64TK_UNOPSYMBOL)
            break;

        int precedence = operator_PrecedenceByType(
            tokens[i].int_value
        );

        // Make sure we don't have an unary op unless at the start:
        if (tokens[i].type == H64TK_UNOPSYMBOL && i > 0) {
            char buf[512]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected binary operator or end of inline "
                "expression starting in line %"
                PRId64 ", column %" PRId64 " instead",
                _describetoken(describebuf,
                    context->tokenstreaminfo, tokens, i),
                _refline(context->tokenstreaminfo, tokens, 0),
                _refline(context->tokenstreaminfo, tokens, 0)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            if (lefthandside) ast_MarkExprDestroyed(lefthandside);
            if (original_lefthand && original_lefthand != lefthandside)
                ast_MarkExprDestroyed(original_lefthand);
            return 0;
        }

        // Hand off different precedence levels:
        if (precedence < precedencelevel) {
            #ifdef H64AST_DEBUG
            printf("horsec: debug: "
                "RECURSE down to %d AT %d FOR op %s\n",
                precedencelevel - 1, i,
                operator_OpPrintedAsStr(tokens[i].int_value));
            #endif
            assert(precedence >= 0);
            int inneroom = 0;
            int innerparsefail = 0;
            int tlen = 0;

            h64expression *innerrighthand = NULL;
            int innerrighthandlen = 0;
            if (lefthandside && (
                    lefthandside->type == H64EXPRTYPE_BINARYOP ||
                    lefthandside->type == H64EXPRTYPE_UNARYOP)) {
                innerrighthandlen = (
                    lefthandside->op.totaltokenlen -
                    lefthandside->op.optokenoffset - 1
                );
                innerrighthand = (
                    lefthandside->type == H64EXPRTYPE_BINARYOP ?
                    lefthandside->op.value2 : lefthandside->op.value1
                );
                assert(innerrighthandlen > 0);
                assert(innerrighthand != NULL);
            }

            int skipback = innerrighthandlen;
            if (!innerrighthand) skipback = i;
            h64expression *innerexpr = NULL;
            h64parsethis _buf;
            if (!ast_ParseExprInlineOperator_Recurse(
                    context,
                    newparsethis(&_buf, parsethis, tokens + i - skipback,
                              max_tokens_touse - (i - skipback)),
                    innerrighthand,
                    innerrighthandlen,
                    precedencelevel - 1,
                    &innerparsefail, &inneroom,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (original_lefthand &&
                            original_lefthand != lefthandside)
                        ast_MarkExprDestroyed(original_lefthand);
                    ast_MarkExprDestroyed(lefthandside);
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                } else if (innerparsefail) {
                    if (original_lefthand &&
                            original_lefthand != lefthandside)
                        ast_MarkExprDestroyed(original_lefthand);
                    ast_MarkExprDestroyed(lefthandside);
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    return 0;
                }
                break;
            }
            if (innerexpr == original_lefthand)
                original_lefthand = NULL;
            if (innerexpr == lefthandside)
                lefthandside = NULL;
            assert(innerexpr != NULL);
            assert(innerexpr->type == H64EXPRTYPE_BINARYOP ||
                innerexpr->type == H64EXPRTYPE_UNARYOP ||
                innerexpr->type == H64EXPRTYPE_CALL);
            if (lefthandside && i - skipback > 0) {
                if (lefthandside->type == H64EXPRTYPE_BINARYOP) {
                    if (lefthandside->op.value2 &&
                            lefthandside->op.value2 != original_lefthand &&
                            lefthandside->op.value2 != innerrighthand)
                        ast_MarkExprDestroyed(lefthandside->op.value2);
                    lefthandside->op.value2 = innerexpr;
                } else {
                    assert(lefthandside->type == H64EXPRTYPE_UNARYOP);
                    if (lefthandside->op.value1 &&
                            lefthandside->op.value1 != original_lefthand &&
                            lefthandside->op.value1 != innerrighthand)
                        ast_MarkExprDestroyed(lefthandside->op.value1);
                    lefthandside->op.value1 = innerexpr;
                }
                assert(tlen > innerrighthandlen &&
                    i - skipback + tlen > i);
            } else {
                if (lefthandside && original_lefthand != lefthandside &&
                        lefthandside != innerrighthand)
                    ast_MarkExprDestroyed(lefthandside);
                lefthandside = innerexpr;
            }
            lefthandsidetokenlen = (i - skipback) + tlen;
            #ifdef H64AST_DEBUG
            char *lhandside = ast_ExpressionToJSONStr(lefthandside, NULL);
            printf("horsec: debug: FROM RECURSIVE INNER, "
                "GOT NEW lefthand: %s\n", lhandside);
            if (lhandside) free(lhandside);
            #endif
            i = lefthandsidetokenlen;
            operatorsprocessed++;
            continue;
        } else if (precedence > precedencelevel) {
            // Needs to be handled by parent call, we're done here.
            break;
        } else {
            #ifdef H64AST_DEBUG
            printf("horsec: debug: handling op %s at level %d\n",
                operator_OpPrintedAsStr(tokens[i].int_value),
               precedencelevel);
            #endif
        }
        i++; // go past operator

        int optokenoffset = i - 1;
        operatorsprocessed++;
        assert(
            tokens[i - 1].type == H64TK_UNOPSYMBOL ||
            i - 1 > 0
        );

        // Special handling of call right-hand side:
        if (tokens[i - 1].type == H64TK_BINOPSYMBOL &&
                tokens[i - 1].int_value == H64OP_CALL) {
            h64expression *callexpr = ast_AllocExpr(context->ast);
            if (!callexpr) {
                if (outofmemory) *outofmemory = 1;
                if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_MarkExprDestroyed(original_lefthand);
                return 0;
            }
            memset(callexpr, 0, sizeof(*callexpr));
            callexpr->storage.eval_temp_id = -1;
            callexpr->line = tokens[i - 1].line;
            callexpr->column = tokens[i - 1].column;
            callexpr->tokenindex = (i - 1) + (
                ((char*)tokens -
                 (char*)context->tokenstreaminfo->token) / sizeof(*tokens)
            );
            callexpr->type = H64EXPRTYPE_CALL;
            callexpr->inlinecall.value = lefthandside;
            if (lefthandside == original_lefthand)
                original_lefthand = NULL;
            lefthandside = NULL;
            i--;
            int tlen = 0;
            int inneroom = 0;
            int innerparsefail = 0;
            h64parsethis _buf;
            if (!ast_ParseFuncCallArgs(
                    context, newparsethis(
                        &_buf, parsethis, tokens + i, max_tokens_touse - i
                    ),
                    &innerparsefail, &inneroom,
                    &callexpr->inlinecall.arguments,
                    &tlen, nestingdepth
                    )) {
                ast_MarkExprDestroyed(callexpr);
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                    if (original_lefthand &&
                            original_lefthand != lefthandside)
                        ast_MarkExprDestroyed(original_lefthand);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail) {
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, "internal error? "
                            "got no function args "
                            "but no error", fileuri,
                            _refline(context->tokenstreaminfo, tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                }
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_MarkExprDestroyed(original_lefthand);
                return 0;
            }
            i += tlen;
            lefthandside = callexpr;
            lefthandsidetokenlen = i;
            #ifdef H64AST_DEBUG
            char describebufy2[64];
            char *lhandside2 = ast_ExpressionToJSONStr(lefthandside, NULL);
            printf("horsec: debug: "
                "GOT CALL %s AND NOW AT %d %s (tlen was: %d) - "
                "current handling level: %d\n", lhandside2, i,
                _describetoken(describebufy2,
                    tokenstreaminfo, tokens, i), tlen,
                precedencelevel);
            if (lhandside2) free(lhandside2);
            #endif
            continue;
        }

        // Parse right-hand side:
        h64expression *righthandside = NULL;
        int righthandsidelen = 0;
        {
            int inneroom = 0;
            int innerparsefail = 0;
            int isindexbyexpr = (
                tokens[i - 1].type == H64TK_BINOPSYMBOL &&
                tokens[i - 1].int_value == H64OP_INDEXBYEXPR
            );
            h64parsethis _buf;
            if (i >= max_tokens_touse ||
                    (!isindexbyexpr && !ast_ParseExprInline(
                        context, newparsethis(
                            &_buf, parsethis, tokens + i, max_tokens_touse - i
                        ),
                        INLINEMODE_NONGREEDY,
                        &innerparsefail, &inneroom,
                        &righthandside, &righthandsidelen,
                        nestingdepth
                        )
                    ) || (isindexbyexpr && !ast_ParseExprInline(
                        context, newparsethis(
                            &_buf, parsethis, tokens + i, max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &innerparsefail, &inneroom,
                        &righthandside, &righthandsidelen, nestingdepth
                        )
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_MarkExprDestroyed(original_lefthand);
                    return 0;
                } else if (innerparsefail) {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_MarkExprDestroyed(original_lefthand);
                    return 0;
                }
                righthandsideparsefail: ;
                char buf[512]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected %s",
                    _describetoken(describebuf,
                        context->tokenstreaminfo, tokens, i),
                    (isindexbyexpr ? "expression for indexing" :
                     "right-hand side to binary operator")
                );
                if (outofmemory) *outofmemory = 0;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_MarkExprDestroyed(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_MarkExprDestroyed(original_lefthand);
                return 0;
            }
            if (isindexbyexpr && (
                    i + righthandsidelen >= max_tokens_touse ||
                    tokens[i + righthandsidelen].type != H64TK_BRACKET ||
                    tokens[i + righthandsidelen].char_value != ']')) {
                ast_MarkExprDestroyed(righthandside);
                righthandside = NULL;
                goto righthandsideparsefail;
            } else if (isindexbyexpr) {
                i++;  // go past closing ']'
            }
        }
        assert(righthandside != NULL && righthandsidelen > 0);
        i += righthandsidelen;

        h64expression *opexpr = ast_AllocExpr(context->ast);
        if (!opexpr) {
            if (outofmemory) *outofmemory = 1;
            if (lefthandside) ast_MarkExprDestroyed(lefthandside);
            if (original_lefthand && original_lefthand != lefthandside)
                ast_MarkExprDestroyed(original_lefthand);
            return 0;
        }
        memset(opexpr, 0, sizeof(*opexpr));
        opexpr->storage.eval_temp_id = -1;
        opexpr->tokenindex = -1;
        opexpr->op.optype = tokens[optokenoffset].int_value;
        if (tokens[optokenoffset].type == H64TK_UNOPSYMBOL) {
            opexpr->type = H64EXPRTYPE_UNARYOP;
            assert(lefthandside == NULL);
            opexpr->op.value1 = righthandside;
            if (lefthandside && lefthandside != original_lefthand &&
                    lefthandside != righthandside)
                ast_MarkExprDestroyed(lefthandside);
            lefthandside = NULL;
        } else {
            opexpr->type = H64EXPRTYPE_BINARYOP;
            opexpr->op.value1 = lefthandside;
            if (original_lefthand == lefthandside)
                original_lefthand = NULL;
            opexpr->op.value2 = righthandside;
            lefthandside = NULL;
        }
        assert((optokenoffset > 0 ||
            tokens[optokenoffset].type == H64TK_UNOPSYMBOL) &&
            optokenoffset < i);
        opexpr->op.optokenoffset = optokenoffset;
        lefthandside = opexpr;
        lefthandsidetokenlen = i;
        opexpr->op.totaltokenlen = i;
        if (opexpr->op.value1) {
            opexpr->tokenindex = opexpr->op.value1->tokenindex;
            opexpr->line = opexpr->op.value1->line;
            opexpr->column = opexpr->op.value1->column;
        }

        #ifdef H64AST_DEBUG
        char describebufy2[64];
        char *lhandside2 = ast_ExpressionToJSONStr(lefthandside, NULL);
        printf("horsec: debug: "
            "GOT NEW LEFT HAND SIDE %s AND NOW AT %d %s - "
             "current handling level: %d\n", lhandside2, i,
             _describetoken(describebufy2,
                 tokenstreaminfo, tokens, i),
             precedencelevel);
        if (lhandside2) free(lhandside2);
        #endif
    }
    if (lefthandside && operatorsprocessed > 0) {
        if (original_lefthand && original_lefthand != lefthandside)
            ast_MarkExprDestroyed(original_lefthand);
        *out_expr = lefthandside;
        *out_tokenlen = lefthandsidetokenlen;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        return 1;
    } else {
        if (lefthandside &&
                original_lefthand != lefthandside)
            ast_MarkExprDestroyed(lefthandside);
        // XXX: do NOT free original lefthandside if no parse error.
    }
    *out_expr = NULL;
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 0;
    return 0;
}

int ast_ParseExprInlineOperator(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return ast_ParseExprInlineOperator_Recurse(
        context, parsethis,
        NULL, 0,
        operator_precedences_total_count - 1,
        parsefail, outofmemory,
        out_expr, out_tokenlen, nestingdepth
    ); 
}

int ast_ParseInlineFunc(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if ( max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    int i = 0;
    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[128];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            context->resultmsg, buf, fileuri,
            _refline(context->tokenstreaminfo, tokens, i),
            _refcol(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }
    h64expression *expr = ast_AllocExpr(context->ast);
    if (!expr) {
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));
    expr->storage.eval_temp_id = -1;
    expr->type = H64EXPRTYPE_INLINEFUNCDEF;
    expr->funcdef.bytecode_func_id = -1;
    expr->line = _refline(context->tokenstreaminfo, tokens, 0);
    expr->column = _refcol(context->tokenstreaminfo, tokens, 0);
    assert(!parsethis->scope ||
           parsethis->scope->magicinitnum == SCOPEMAGICINITNUM);
    expr->funcdef.scope.parentscope = parsethis->scope;
    if (!scope_Init(&expr->funcdef.scope)) {
        if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 0;
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    expr->funcdef.scope.classandfuncnestinglevel =
        expr->funcdef.scope.parentscope->classandfuncnestinglevel + 1;
    expr->tokenindex = 0 + (
        ((char*)tokens -
         (char*)context->tokenstreaminfo->token) / sizeof(*tokens)
    );
    if (tokens[0].type == H64TK_BRACKET &&
            tokens[0].char_value == '(') {
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        h64parsethis _buf;
        if (!ast_ParseFuncDefArgs(
                context, newparsethis_newscope(
                    &_buf, parsethis, &expr->funcdef.scope,
                    tokens, max_tokens_touse
                ), expr,
                &innerparsefail, &inneroom,
                &expr->funcdef.arguments, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            } else if (innerparsefail) {
                if (outofmemory) *outofmemory = 0;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected function argument list for inline "
                "function definition",
                _reftokname(context->tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, 0),
                    _refcol(context->tokenstreaminfo, tokens, 0)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i += tlen; 
    } else if (tokens[0].type == H64TK_IDENTIFIER) {
        expr->funcdef.arguments.arg_name = malloc(
            sizeof(char*) * 1
        );
        if (expr->funcdef.arguments.arg_name)
            expr->funcdef.arguments.arg_name[0] = strdup(
                tokens[0].str_value
            );
        expr->funcdef.arguments.arg_value = malloc(
            sizeof(char*) * 1
        );
        if (expr->funcdef.arguments.arg_value)
            expr->funcdef.arguments.arg_value[0] = NULL;
        if (!expr->funcdef.arguments.arg_name ||
                !expr->funcdef.arguments.arg_value ||
                !expr->funcdef.arguments.arg_name[0]) {
            if (expr->funcdef.arguments.arg_value &&
                    expr->funcdef.arguments.arg_value[0])
                free(expr->funcdef.arguments.arg_value[0]);
            if (expr->funcdef.arguments.arg_name &&
                    expr->funcdef.arguments.arg_name[0])
                free(expr->funcdef.arguments.arg_name[0]);
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->funcdef.arguments.arg_count = 1;
        int scopeoom = 0;
        if (!scope_AddItem(
                &expr->funcdef.scope,
                expr->funcdef.arguments.arg_name[0], expr,
                &scopeoom
                )) {
            if (scopeoom) {
                scopeaddoom:
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            } else {
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "INTERNAL ERROR, failed to "
                        "scope-add function param", fileuri,
                        -1, -1
                        ))
                    goto scopeaddoom;
                if (outofmemory) *outofmemory = 0;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        }
        i++;
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected function argument list for inline function",
            _reftokname(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    if (i >= max_tokens_touse ||
            tokens[i].type != H64TK_INLINEFUNC) {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected \"=>\" for inline function",
            _reftokname(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    i++;
    if (i >= max_tokens_touse ||
            tokens[i].type != H64TK_BRACKET ||
            tokens[i].char_value != '(') {
        char buf[256]; char dbuf[64];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected \"(\" to begin inline function's returned value",
            _describetoken(dbuf, context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    i++;
    int tlen = 0;
    int innerparsefail = 0;
    int inneroom = 0;
    h64expression *returnedexpr = NULL;
    h64parsethis _buf;
    int inlinevaluetokenid = i;
    if (!ast_ParseExprInline(
            context, newparsethis_newscope(
                &_buf, parsethis, &expr->funcdef.scope,
                tokens + i, max_tokens_touse - i
            ),
            INLINEMODE_GREEDY,
            &innerparsefail, &inneroom,
            &returnedexpr, &tlen, nestingdepth
            )) {
        if (inneroom) {
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        if (outofmemory) *outofmemory = 0;
        if (!innerparsefail) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected valid inline expression as inline function "
                "return value",
                _reftokname(context->tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
        }
        if (parsefail) *parsefail = 1;
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    i += tlen;
    if (i >= max_tokens_touse ||
            tokens[i].type != H64TK_BRACKET ||
            tokens[i].char_value != ')') {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected \")\" to end inline function's returned value",
            _reftokname(context->tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_MarkExprDestroyed(returnedexpr);
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    i++;
    h64expression *returnstmt = ast_AllocExpr(context->ast);
    if (!returnstmt) {
        if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 0;
        ast_MarkExprDestroyed(returnedexpr);
        ast_MarkExprDestroyed(expr);
        return 0;
    }
    memset(returnstmt, 0, sizeof(*returnstmt));
    expr->storage.eval_temp_id = -1;
    assert(!expr->funcdef.stmt);
    expr->funcdef.stmt = malloc(sizeof(
        *expr->funcdef.stmt
    ) * 1);
    if (!expr->funcdef.stmt) {
        if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 0;
        ast_MarkExprDestroyed(returnedexpr);
        ast_MarkExprDestroyed(expr);
        free(returnstmt);
        return 0;
    }
    expr->funcdef.stmt_count = 1;
    returnstmt->line = tokens[inlinevaluetokenid].line;
    returnstmt->column = tokens[inlinevaluetokenid].column;
    returnstmt->type = H64EXPRTYPE_RETURN_STMT;
    returnstmt->returnstmt.returned_expression = returnedexpr;
    expr->funcdef.stmt[0] = returnstmt;
    *out_expr = expr;
    if (out_tokenlen) *out_tokenlen = i;
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 0;
    return 1;
}

int ast_ParseExprInline(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int inlinemode,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse < 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[128];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            context->resultmsg, buf, fileuri,
            _refline(context->tokenstreaminfo, tokens, 0),
            _refcol(context->tokenstreaminfo, tokens, 0)
        );
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *expr = ast_AllocExpr(context->ast);
    if (!expr) {
        result_ErrorNoLoc(
            context->resultmsg,
            "failed to allocate expression, "
            "out of memory?",
            fileuri
        );
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));
    expr->storage.eval_temp_id = -1;

    expr->line = tokens[0].line;
    expr->column = tokens[0].column;
    expr->tokenindex = 0 + (
        ((char*)tokens -
         (char*)context->tokenstreaminfo->token) / sizeof(*tokens)
    );

    if (inlinemode == INLINEMODE_NONGREEDY) {
        if (tokens[0].type == H64TK_IDENTIFIER &&
                max_tokens_touse >= 2 &&
                tokens[1].type == H64TK_INLINEFUNC) {
            h64expression *innerexpr = NULL;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroutofmemory = 0;
            if (!ast_ParseInlineFunc(
                    context, parsethis,
                    &innerparsefail, &inneroutofmemory,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                } else {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    if (!innerparsefail) {
                        result_ErrorNoLoc(
                            context->resultmsg,
                            "internal error, unexpectedly failed "
                            "to parse inline func. this should never "
                            "happen, not even when out of memory...",
                            fileuri
                        );
                    }
                }
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            assert(innerexpr != NULL);
            ast_MarkExprDestroyed(expr);
            expr = innerexpr;
            if (outofmemory) *outofmemory = 0;
            if (parsefail) *parsefail = 0;
            if (out_expr) *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = tlen;
            return 1;
        } else if (tokens[0].type == H64TK_UNOPSYMBOL) {
            h64expression *innerexpr = NULL;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroutofmemory = 0;
            if (!ast_ParseExprInlineOperator_Recurse(
                    context, parsethis,
                    NULL, 0, operator_PrecedenceByType(tokens[0].int_value),
                    &innerparsefail, &inneroutofmemory,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                } else {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    if (!innerparsefail) {
                        result_ErrorNoLoc(
                            context->resultmsg,
                            "internal error, unexpectedly failed "
                            "to parse inline unaryop. this should never "
                            "happen, not even when out of memory...",
                            fileuri
                        );
                    }
                }
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            ast_MarkExprDestroyed(expr);
            *out_expr = innerexpr;
            if (out_tokenlen) *out_tokenlen = tlen;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        } else if (tokens[0].type == H64TK_IDENTIFIER) {
            expr->type = H64EXPRTYPE_IDENTIFIERREF;
            assert(tokens[0].str_value != NULL);
            expr->identifierref.value = strdup(tokens[0].str_value);
            if (!expr->identifierref.value) {
                expr->type = H64EXPRTYPE_INVALID;
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = 1;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        } else if (tokens[0].type == H64TK_CONSTANT_INT ||
                tokens[0].type == H64TK_CONSTANT_FLOAT ||
                tokens[0].type == H64TK_CONSTANT_BOOL ||
                tokens[0].type == H64TK_CONSTANT_NONE ||
                tokens[0].type == H64TK_CONSTANT_STRING) {
            expr->type = H64EXPRTYPE_LITERAL;
            expr->literal.type = tokens[0].type;
            if (tokens[0].type == H64TK_CONSTANT_INT) {
                expr->literal.int_value = tokens[0].int_value;
            } else if (tokens[0].type == H64TK_CONSTANT_FLOAT) {
                expr->literal.float_value = tokens[0].float_value;
            } else if (tokens[0].type == H64TK_CONSTANT_BOOL) {
                expr->literal.int_value = tokens[0].int_value;
            } else if (tokens[0].type == H64TK_CONSTANT_STRING) {
                expr->literal.str_value = strdup(tokens[0].str_value);
                if (!expr->literal.str_value) {
                    ast_MarkExprDestroyed(expr);
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
            } else if (tokens[0].type == H64TK_CONSTANT_NONE) {
                // Nothing to copy over
            } else {
                // Should be impossible to reach!
                fprintf(stderr, "horsec: error: UNHANDLED LITERAL TYPE\n");
                ast_MarkExprDestroyed(expr);
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = 1;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        } else if (tokens[0].type == H64TK_BRACKET &&
                tokens[0].char_value == '(') {
            // Check if this is an inline function.
            {
                int bracket_depth = 0;
                int i = 1;
                while (1) {
                    if (i >= max_tokens_touse)
                        break;
                    if (tokens[i].type == H64TK_BRACKET) {
                        char c = tokens[i].char_value;
                        if (c == '{' || c == '(' || c == '[') {
                            bracket_depth++;
                        } else if (c == '}' || c == ')' || c == ']') {
                            bracket_depth--;
                            if (bracket_depth < 0)
                                break;
                        }
                    }
                    i++;
                }
                if (i + 1 < max_tokens_touse &&
                        tokens[i].type == H64TK_BRACKET &&
                        tokens[i].char_value == ')' &&
                        tokens[i + 1].type == H64TK_INLINEFUNC) {
                    i = 0;
                    h64expression *innerexpr = NULL;
                    int tlen = 0;
                    int innerparsefail = 0;
                    int inneroutofmemory = 0;
                    if (!ast_ParseInlineFunc(
                            context, parsethis,
                            &innerparsefail, &inneroutofmemory,
                            &innerexpr, &tlen, nestingdepth
                            )) {
                        if (inneroutofmemory) {
                            if (outofmemory) *outofmemory = 1;
                            if (parsefail) *parsefail = 0;
                        } else {
                            if (outofmemory) *outofmemory = 0;
                            if (parsefail) *parsefail = 1;
                            if (!innerparsefail) {
                                result_ErrorNoLoc(
                                    context->resultmsg,
                                    "internal error, unexpectedly failed "
                                    "to parse inline func. this should never "
                                    "happen, not even when out of memory...",
                                    fileuri
                                );
                            }
                        }
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    assert(innerexpr != NULL);
                    ast_MarkExprDestroyed(expr);
                    expr = innerexpr;
                    i += tlen;
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 0;
                    if (out_expr) *out_expr = expr;
                    if (out_tokenlen) *out_tokenlen = i;
                    return 1;
                }
            }
            // Ok, not an inline func. So this must be a normal bracket:

            int tlen = 0;
            h64expression *innerexpr = NULL;
            int inneroom = 0;
            int innerparsefail = 0;
            int i = 1;
            h64parsethis _buf;
            if (!ast_ParseExprInline(
                    context, newparsethis(
                        &_buf, parsethis, tokens + i, max_tokens_touse - i
                    ),  // get value inside brackets
                    INLINEMODE_GREEDY,
                    &innerparsefail, &inneroom,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                } else if (innerparsefail) {
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                // Nothing was inside the brackets, this is invalid:
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected '(' followed immediately by ')',"
                    "expected '(' <inlinevalue> ')' or some other inline "
                    "value instead"
                );
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i - 1),
                        _refcol(context->tokenstreaminfo, tokens, i - 1)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            assert(innerexpr != NULL);
            ast_MarkExprDestroyed(expr);
            expr = innerexpr;
            i += tlen;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_BRACKET ||
                    tokens[i].char_value != ')') {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected ')' corresponding to opening '(' "
                    "in line %" PRId64 ", column %" PRId64 " instead",
                    _describetoken(describebuf,
                        context->tokenstreaminfo, tokens, i),
                    _refline(context->tokenstreaminfo, tokens, 0),
                    _refcol(context->tokenstreaminfo, tokens, 0)
                );
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i++;  // past closing bracket

            *out_expr = expr;
            *out_tokenlen = i;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        } else if (tokens[0].type == H64TK_BRACKET &&
                (tokens[0].char_value == '[' ||
                 tokens[0].char_value == '{')) {
            // List, vector, set, or map
            const char *_nm_vec = "vector";
            const char *_nm_set = "set";
            const char *_nm_map = "map";
            const char *itemname = "list";
            int islist = 1;
            int ismap = 0;
            int isset = 0;
            int isvector = 0;
            int vectorusesletters = 0;
            if (tokens[0].char_value == '[' &&
                    2 < max_tokens_touse && (
                    (tokens[1].type == H64TK_IDENTIFIER &&
                     strcmp(tokens[1].str_value, "x") == 0) ||
                    (tokens[1].type == H64TK_CONSTANT_INT &&
                     tokens[1].int_value == 0))) {
                if (tokens[1].type == H64TK_IDENTIFIER)
                    vectorusesletters = 1;
                itemname = _nm_vec;
                isvector = 1;
                islist = 0;
            }
            if (tokens[0].char_value == '{') {
                itemname = _nm_set;
                isset = 1;
                islist = 0;
            }
            if (tokens[0].char_value != '{' && !isvector) {
                // Figure out if this is a map.
                // For that, skip past first item:
                int i = 1;
                int bracket_depth = 0;
                while (i < max_tokens_touse &&
                        ((tokens[i].type != H64TK_COMMA &&
                          tokens[i].type != H64TK_MAPARROW) ||
                          bracket_depth > 0)) {
                    if (tokens[i].type == H64TK_BRACKET) {
                        if (tokens[i].char_value == '(' ||
                                tokens[i].char_value == '[' ||
                                tokens[i].char_value == '{') {
                            bracket_depth++;
                        } else if (tokens[i].char_value == ')' ||
                                tokens[i].char_value == '[' ||
                                tokens[i].char_value == '}') {
                            bracket_depth--;
                            if (bracket_depth < 0) bracket_depth = 0;
                        }
                    }
                    i++;
                }
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_MAPARROW) {
                    itemname = _nm_map;
                    ismap = 1;
                    islist = 0;
                }
                i = 0;
            }
            expr->type = (
                ismap ? H64EXPRTYPE_MAP : (
                isset ? H64EXPRTYPE_SET : (
                isvector ? H64EXPRTYPE_VECTOR : H64EXPRTYPE_LIST
                ))
            );

            int hadanyitems = 0;
            int i = 1;
            while (1) {
                int hadcomma = 0;
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_COMMA) {
                    hadcomma = 1;
                    i++;
                }
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_BRACKET &&
                        ((tokens[i].char_value == ']' && !isset) ||
                         (tokens[i].char_value == '}' && isset))) {
                    i++;
                    break;
                }
                if (hadanyitems && !hadcomma) {
                    char buf[512]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected '%c' or ',' resuming or ending %s "
                        "starting in line %"
                        PRId64 ", column %" PRId64 " instead",
                        _describetoken(describebuf,
                            context->tokenstreaminfo, tokens, i),
                        (isset ? '}' : ']'),
                        itemname,
                        expr->line, expr->column
                    );
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo, tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }

                // Special handling of empty map [->]
                if (ismap &&
                        i + 1 < max_tokens_touse &&
                        tokens[i].type == H64TK_MAPARROW &&
                        tokens[i + 1].type == H64TK_BRACKET &&
                        tokens[i + 1].char_value == ']' &&
                        !hadanyitems) {
                    i += 2;
                    break;
                }

                // Allocate space for next item:
                if (ismap) {
                    h64expression **new_keys = realloc(
                        expr->constructormap.key,
                        sizeof(*new_keys) *
                        (expr->constructormap.entry_count + 1)
                    );
                    if (!new_keys) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    expr->constructormap.key = new_keys;
                    h64expression **new_values = realloc(
                        expr->constructormap.value,
                        sizeof(*new_values) *
                        (expr->constructormap.entry_count + 1)
                    );
                    if (!new_values) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    expr->constructormap.value = new_values;
                } else if (islist) {
                    h64expression **new_entries = realloc(
                        expr->constructorlist.entry,
                        sizeof(*new_entries) *
                        (expr->constructorlist.entry_count + 1)
                    );
                    if (!new_entries) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    expr->constructorlist.entry = new_entries;
                } else if (isset) {
                    h64expression **new_entries = realloc(
                        expr->constructorset.entry,
                        sizeof(*new_entries) *
                        (expr->constructorset.entry_count + 1)
                    );
                    if (!new_entries) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    expr->constructorset.entry = new_entries;
                } else if (isvector) {
                    h64expression **new_entries = realloc(
                        expr->constructorvector.entry,
                        sizeof(*new_entries) *
                        (expr->constructorvector.entry_count + 1)
                    );
                    if (!new_entries) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    expr->constructorvector.entry = new_entries;
                } else {
                    // Should never be reached
                    fprintf(stderr, "horsec: error: unreachable "
                        "code path reached (map/vector/list/set parser)");
                    _exit(1);
                }

                if (isvector) {
                    // Get prefix of next entry:
                    if (vectorusesletters &&
                            expr->constructorvector.entry_count >= 4) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected ']' to end %s "
                            "starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(describebuf,
                                context->tokenstreaminfo, tokens, i),
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(
                                    context->tokenstreaminfo, tokens, i),
                                _refcol(
                                    context->tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    int foundidx = -1;
                    if (i < max_tokens_touse &&
                            tokens[i].type == H64TK_IDENTIFIER &&
                            strlen(tokens[i].str_value) == 1 &&
                            vectorusesletters) {
                        foundidx = tokens[i].str_value[0] - 'x';
                        if (strcmp(tokens[i].str_value, "w") == 0)
                            foundidx = 3;
                    }
                    if (i < max_tokens_touse &&
                            tokens[i].type == H64TK_CONSTANT_INT &&
                            !vectorusesletters &&
                            tokens[i].int_value >= 0 &&
                            tokens[i].int_value < INT32_MAX) {
                        foundidx = tokens[i].int_value;
                    }
                    if (foundidx < 0 ||
                            foundidx != expr->constructorvector.entry_count
                            ) {
                        char buf[512]; char describebuf[64];
                        char expect1dec[32];
                        snprintf(expect1dec, sizeof(expect1dec) - 1,
                            "\"%d\"", expr->constructorvector.entry_count);
                        char expect2dec[32] = {0};
                        if (vectorusesletters) {
                            if (expr->constructorvector.entry_count < 3)
                                snprintf(expect2dec, sizeof(expect2dec) - 1,
                                    ", or \"%c\"",
                                    'x' + expr->constructorvector.
                                    entry_count);
                            else
                                memcpy(expect2dec, ", or \"w\"",
                                       strlen(", or \"w\""));
                        }
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected %s%s for next entry, or "
                            "']' to end %s "
                            "starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(describebuf,
                                context->tokenstreaminfo, tokens, i),
                            expect1dec, expect2dec,
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(
                                    context->tokenstreaminfo, tokens, i),
                                _refcol(
                                    context->tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    i++;
                    if (i >= max_tokens_touse ||
                            tokens[i].type != H64TK_COLON) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected ':' after vector entry label "
                            "in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(describebuf,
                                context->tokenstreaminfo, tokens, i),
                            _refline(context->tokenstreaminfo,
                                     tokens, i - 1),
                            _refcol(context->tokenstreaminfo,
                                    tokens, i - 1)
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(context->tokenstreaminfo,
                                         tokens, i),
                                _refcol(context->tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    i++;
                }

                // Get next item:
                assert(i > 0);
                h64expression *innerexpr = NULL;
                int tlen = 0;
                int innerparsefail = 0;
                int inneroutofmemory = 0;
                h64parsethis _buf;
                if (!ast_ParseExprInline(
                        context, newparsethis(
                            &_buf, parsethis, tokens + i,
                            max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &innerparsefail, &inneroutofmemory,
                        &innerexpr, &tlen, nestingdepth
                        )) {
                    if (inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        if (parsefail) *parsefail = 0;
                    } else {
                        if (outofmemory) *outofmemory = 0;
                        if (parsefail) *parsefail = 1;
                    }
                    if (!innerparsefail && !inneroutofmemory) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected inline value as next %s "
                            "in %s starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(describebuf,
                                context->tokenstreaminfo, tokens, i),
                            (ismap ? "key" : "entry"),
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(context->tokenstreaminfo,
                                         tokens, i),
                                _refcol(context->tokenstreaminfo,
                                        tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                assert(tlen > 0);
                i += tlen;
                hadanyitems = 1;
                if (isvector) {
                    expr->constructorvector.entry[
                        expr->constructorvector.entry_count
                    ] = innerexpr;
                    expr->constructorvector.entry_count++;
                    continue;
                } else if (isset) {
                    expr->constructorset.entry[
                        expr->constructorset.entry_count
                    ] = innerexpr;
                    expr->constructorset.entry_count++;
                    continue;
                } else if (islist) {
                    expr->constructorlist.entry[
                        expr->constructorlist.entry_count
                    ] = innerexpr;
                    expr->constructorlist.entry_count++;
                    continue;
                }
                assert(ismap);
                if (i >= max_tokens_touse ||
                        tokens[i].type != H64TK_MAPARROW) {
                    char buf[512]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected \"=>\" after key entry "
                        "for map starting in line %"
                        PRId64 ", column %" PRId64 " instead",
                        _describetoken(describebuf,
                            context->tokenstreaminfo, tokens, i),
                        _refline(context->tokenstreaminfo, tokens, 0),
                        _refcol(context->tokenstreaminfo, tokens, 0)
                    );
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo, tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(innerexpr);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i++;
                h64expression *innerexpr2 = NULL;
                int tlen2 = 0;
                innerparsefail = 0;
                inneroutofmemory = 0;
                h64parsethis _buf2;
                if (!ast_ParseExprInline(
                        context, newparsethis(
                            &_buf2, parsethis,
                            tokens + i, max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &innerparsefail, &inneroutofmemory,
                        &innerexpr2, &tlen2, nestingdepth
                        )) {
                    if (inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        if (parsefail) *parsefail = 0;
                    } else {
                        if (outofmemory) *outofmemory = 0;
                        if (parsefail) *parsefail = 1;
                    }

                    if (!innerparsefail && !inneroutofmemory) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected inline value following \"=>\" "
                            "for map starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(describebuf,
                                context->tokenstreaminfo, tokens, i),
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(context->tokenstreaminfo,
                                         tokens, i),
                                _refcol(context->tokenstreaminfo,
                                        tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_MarkExprDestroyed(innerexpr);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i += tlen2;
                expr->constructormap.key[
                    expr->constructorlist.entry_count
                ] = innerexpr;
                expr->constructormap.value[
                    expr->constructorlist.entry_count
                ] = innerexpr2;
                expr->constructormap.entry_count++;
            }
            if (outofmemory) *outofmemory = 0;
            if (parsefail) *parsefail = 0;
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = i;
            return 1;
        }

        if (parsefail) *parsefail = 0;
        ast_MarkExprDestroyed(expr);
        return 0;
    }

    #if defined(H64AST_DEBUG)
    char describebuf[64];
    printf("horsec: debug: GREEDY PARSE FROM %d %s\n", 0,
         _describetoken(describebuf,
             context->tokenstreaminfo, tokens, 0));
    #endif

    // Try to greedily parse as full operator expression:
    {
        int tlen = 0;
        h64expression *innerexpr = NULL;
        int inneroom = 0;
        int innerparsefail = 0;
        if (!ast_ParseExprInlineOperator(
                context, parsethis,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            } else if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        } else {
            ast_MarkExprDestroyed(expr);
            *out_expr = innerexpr;
            *out_tokenlen = tlen;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        }
    }

    // If we can't parse as an operator, retry as non-greedy:
    {
        int tlen = 0;
        h64expression *innerexpr = NULL;
        int inneroom = 0;
        int innerparsefail = 0;
        if (!ast_ParseExprInline(
                context, parsethis,
                INLINEMODE_NONGREEDY,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            } else if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        } else {
            ast_MarkExprDestroyed(expr);
            *out_expr = innerexpr;
            *out_tokenlen = tlen;
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 0;
            return 1;
        }
    }

    // Nothing found in either mode, so there is nothing here:
    if (parsefail) *parsefail = 0;
    if (outofmemory) *outofmemory = 0;
    ast_MarkExprDestroyed(expr);
    return 0;
}

int ast_CanBeLValue(h64expression *e) {
    if (e->type == H64EXPRTYPE_IDENTIFIERREF) {
        return 1;
    } else if (e->type == H64EXPRTYPE_BINARYOP) {
        if (e->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER &&
                e->op.optype != H64OP_CALL &&
                e->op.optype != H64OP_INDEXBYEXPR)
            return 0;
        if (!ast_CanBeLValue(e->op.value1))
            return 0;
        return 1;
    } else {
        return 0;
    }
}

int ast_CanBeClassRef(h64expression *e) {
    if (e->type == H64EXPRTYPE_IDENTIFIERREF) {
        return 1;
    } else if (e->type == H64EXPRTYPE_BINARYOP) {
        if (e->op.optype != H64OP_ATTRIBUTEBYIDENTIFIER)
            return 0;
        if (!ast_CanBeClassRef(e->op.value1))
            return 0;
        if (e->op.value2->type != H64EXPRTYPE_IDENTIFIERREF)
            return 0;
        return 1;
    } else {
        return 0;
    }
}

int ast_ParseCodeBlock(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int statementmode,
        h64expression ***stmt_ptr,
        int *stmt_count_ptr,
        int *parsefail,
        int *outofmemory,
        int *out_tokenlen,
        int nestingdepth
        ) {
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 1;
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, "unexpected missing code block, "
                "expected '{' for code block", fileuri,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        return 0;
    }
    int i = 0;
    if (i >= max_tokens_touse ||
            tokens[i].type != H64TK_BRACKET ||
            tokens[i].char_value != '{') {
        char buf[256]; char describebuf[64];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected \"{\" for "
            "code block",
            _describetoken(describebuf,
                context->tokenstreaminfo, tokens, i)
        );
        if (parsefail) *parsefail = 1;
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(context->tokenstreaminfo, tokens, i),
                _refcol(context->tokenstreaminfo, tokens, i)
                ))
            if (outofmemory) *outofmemory = 1;
        return 0;
    }
    int64_t codeblock_line = tokens[i].line;
    int64_t codeblock_column = tokens[i].column;

    i++;
    while (1) {
        if (i < max_tokens_touse && (
                tokens[i].type != H64TK_BRACKET ||
                tokens[i].char_value != '}')) {
            int tlen = 0;
            int _innerparsefail = 0;
            int _inneroutofmemory = 0;
            h64parsethis _buf;
            h64expression *innerexpr = NULL;
            if (!ast_ParseExprStmt(
                    context, newparsethis(
                        &_buf, parsethis,
                        &tokens[i], max_tokens_touse - i
                    ),
                    (statementmode != STATEMENTMODE_INCLASS &&
                     statementmode != STATEMENTMODE_INCLASSFUNC ?
                     STATEMENTMODE_INFUNC :
                     STATEMENTMODE_INCLASSFUNC),
                    &_innerparsefail,
                    &_inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    )) {
                if (_inneroutofmemory) {
                    if (parsefail) *parsefail = 0;
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
                if (_innerparsefail) {
                    // Try to recover by finding next obvious
                    // statement, or possible function end:
                    int previ = i;
                    ast_ParseRecover_FindNextStatement(
                        context->tokenstreaminfo,
                        tokens, max_tokens_touse, &i,
                        RECOVERFLAGS_MUSTFORWARD
                    );
                    assert(i > previ || i >= max_tokens_touse);
                    continue;
                }
            } else {
                assert(innerexpr != NULL);
                assert(tlen > 0);
                h64expression **new_stmt = realloc(
                    *stmt_ptr,
                    sizeof(*new_stmt) *
                    ((*stmt_count_ptr) + 1)
                );
                if (!new_stmt) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(innerexpr);
                    return 0;
                }
                *stmt_ptr = new_stmt;
                (*stmt_ptr)[
                    *stmt_count_ptr
                ] = innerexpr;
                (*stmt_count_ptr)++;
                i += tlen;
                continue;
            }
        }
        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_BRACKET ||
                tokens[i].char_value != '}') {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected valid statement or \"}\" to end "
                "code block opened with \"{\" in line %"
                PRId64 ", column %" PRId64 " instead",
                _describetoken(describebuf,
                    context->tokenstreaminfo, tokens, i),
                codeblock_line, codeblock_column
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            } else {
                // If this is a clear indication the block ended, exit:
                if (tokens[i].type == H64TK_IDENTIFIER && (
                        strcmp(tokens[i].str_value, "class") == 0 ||
                        strcmp(tokens[i].str_value, "import") == 0))
                    break;

                // Skip to next possible statement:
                int previ = i;
                ast_ParseRecover_FindNextStatement(
                    context->tokenstreaminfo, tokens,
                    max_tokens_touse, &i,
                    RECOVERFLAGS_NORMAL
                );
                assert(i >= previ || i >= max_tokens_touse);
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_BRACKET &&
                        tokens[i].char_value == '}') {
                    i++;
                    break;
                }
                if (i >= max_tokens_touse)
                    break;
                continue;
            }
        }
        i++;
        break;
    }
    if (out_tokenlen) *out_tokenlen = i;
    return 1;
}

static const char _defnameimport[] = "import";
static const char _defnamevar[] = "variable";
static const char _defnamefunc[] = "function";
static const char _defnamefuncparam[] = "function parameter";
static const char _defnameclass[] = "class";
static const char _defnameforloop[] = "for loop iterator";
static const char _defnamecatch[] = "caught error";

const char *_identifierdeclarationname(
        h64expression *expr, const char *identifier
        ) {
    const char *deftype = NULL;
    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        if (expr->funcdef.name &&
                strcmp(expr->funcdef.name, identifier) == 0) {
            deftype = _defnamefunc;
        } else {
            assert(funcdef_has_parameter_with_name(
                expr, identifier
            ));
            deftype = _defnamefuncparam;
        }
    } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
        deftype = _defnameclass;
    } else if (expr->type == H64EXPRTYPE_FOR_STMT) {
        deftype = _defnameforloop;
    } else if (expr->type == H64EXPRTYPE_IMPORT_STMT) {
        deftype = _defnameimport;
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        deftype = _defnamevar;
    } else if (expr->type == H64EXPRTYPE_DO_STMT) {
        deftype = _defnamecatch;
    } else {
        fprintf(stderr, "horsec: error: internal error: "
            "what is this type: %d\n", expr->type);
        assert(0 && "unrecognized scope definition type");
    }
    return deftype;
}

static int _importstmts_have_duplicate_path(
        h64expression *expr1, h64expression *expr2
        ) {
    if (expr1->importstmt.import_elements_count !=
            expr2->importstmt.import_elements_count)
        return 0;
    int i = 0;
    while (i < expr1->importstmt.import_elements_count) {
        if (strcasecmp(
                expr1->importstmt.import_elements[i],
                expr2->importstmt.import_elements[i]) != 0) {
            return 0;
        }
        i++;
    }
    return 1;
}

int ast_CanAddNameToScopeCheck(
        h64parsecontext *context,
        h64parsethis *parsethis,
        h64expression *expr,
        int identifiertokenindex,
        h64scopedef **appends_to_sdef,
        int *outofmemory
        ) {
    h64scope *scope = parsethis->scope;
    int i = identifiertokenindex;
    const char *exprname = NULL;
    if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        exprname = expr->funcdef.name;
    } else if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        exprname = expr->vardef.identifier;
    } else if (expr->type == H64EXPRTYPE_CLASSDEF_STMT) {
        exprname = expr->classdef.name;
    } else if (expr->type == H64EXPRTYPE_FOR_STMT) {
        exprname = expr->forstmt.iterator_identifier;
    } else if (expr->type == H64EXPRTYPE_DO_STMT) {
        exprname = expr->dostmt.error_name;
    } else if (expr->type == H64EXPRTYPE_IMPORT_STMT) {
        if (expr->importstmt.import_as != NULL) {
            exprname = expr->importstmt.import_as;
        } else {
            assert(expr->importstmt.import_elements_count > 0);
            exprname = expr->importstmt.import_elements[0];
        }
    } else {
        assert(0 && "unexpected definition type, what is this?");
    }
    const char *deftype = _identifierdeclarationname(
        expr, exprname
    );

    h64scopedef *duplicateuse = NULL;
    if ((duplicateuse = _getSameScopeShadowedDefinition(
            parsethis, exprname
            )) != NULL) {
        h64expression *conflictingexpr = duplicateuse->declarationexpr;
        int validimportstacking = 0;
        if (duplicateuse->declarationexpr->type ==
                H64EXPRTYPE_IMPORT_STMT &&
                expr->type == H64EXPRTYPE_IMPORT_STMT) {
            validimportstacking = 1;
            if (_importstmts_have_duplicate_path(
                    duplicateuse->declarationexpr, expr)) {
                validimportstacking = 0;
            }
            int i = 0;
            while (i < duplicateuse->additionaldecl_count) {
                assert(duplicateuse->additionaldecl[i]->type ==
                       H64EXPRTYPE_IMPORT_STMT);
                if (_importstmts_have_duplicate_path(
                        duplicateuse->additionaldecl[i], expr)) {
                    validimportstacking = 0;
                    break;
                }
                i++;
            }
        }
        if (!validimportstacking) {
            char buf[256];
            char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected duplicate %s \"%s\", "
                "already defined as %s in same scope "
                "in line %" PRId64 ", column %" PRId64
                ", this is not allowed",
                deftype, _shortenedname(describebuf, exprname),
                _identifierdeclarationname(
                    duplicateuse->declarationexpr, exprname
                ),
                duplicateuse->declarationexpr->line,
                duplicateuse->declarationexpr->column
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, context->fileuri,
                    _refline(
                        context->tokenstreaminfo, parsethis->tokens, i),
                    _refcol(
                        context->tokenstreaminfo, parsethis->tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            if (outofmemory) *outofmemory = 0;
            return 0;
        }
        if (outofmemory) *outofmemory = 0;
        if (appends_to_sdef)
            *appends_to_sdef = duplicateuse;
        return 1;
    } else {
        h64scopedef *shadoweduse = scope_QueryItem(
            parsethis->scope, exprname, 1
        );
        assert(!shadoweduse || shadoweduse->scope != NULL);
        assert(context != NULL && context->project != NULL);
        int forbidden = 0;
        if (shadoweduse &&
                (shadoweduse->declarationexpr->type ==
                H64EXPRTYPE_FUNCDEF_STMT ||
                shadoweduse->declarationexpr->type ==
                H64EXPRTYPE_INLINEFUNCDEF) &&
                shadoweduse->scope->classandfuncnestinglevel ==
                parsethis->scope->classandfuncnestinglevel &&
                funcdef_has_parameter_with_name(
                shadoweduse->declarationexpr,
                expr->funcdef.name)) {
            forbidden = 1;
            char buf[256];
            char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s \"%s\" "
                "shadowing function "
                "parameter seen "
                "in line %" PRId64 ", column %" PRId64
                ", this is not allowed",
                deftype, _shortenedname(describebuf, exprname),
                shadoweduse->declarationexpr->line,
                shadoweduse->declarationexpr->column
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, context->fileuri,
                    _refline(
                        context->tokenstreaminfo,
                        parsethis->tokens, i),
                    _refcol(
                        context->tokenstreaminfo,
                        parsethis->tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
        } else if (shadoweduse && ((
                   !shadoweduse->scope->is_global &&
                   shadoweduse->scope->classandfuncnestinglevel ==
                   parsethis->scope->classandfuncnestinglevel &&
                   context->project->warnconfig.
                   warn_shadowing_direct_locals
                   ) || (
                   !shadoweduse->scope->is_global &&
                   shadoweduse->scope->classandfuncnestinglevel !=
                   parsethis->scope->classandfuncnestinglevel &&
                   context->project->warnconfig.
                   warn_shadowing_parent_func_locals
                   ) || (
                   shadoweduse->scope->is_global &&
                   context->project->warnconfig.
                   warn_shadowing_globals))) {
            char warningtypetext[64];
            if (!shadoweduse->scope->is_global) {
                if (shadoweduse->scope->classandfuncnestinglevel ==
                        parsethis->scope->classandfuncnestinglevel) {
                    snprintf(warningtypetext,
                        sizeof(warningtypetext) - 1,
                        ", this is not recommended "
                        "[-Wshadowing-direct-locals]"
                    );
                } else {
                    snprintf(warningtypetext,
                        sizeof(warningtypetext) - 1,
                        " [-Wshadowing-parent-func-locals]"
                    );
                }
            } else {
                snprintf(warningtypetext,
                    sizeof(warningtypetext) - 1,
                    " [-Wshadowing-globals]"
                );
            }
            char buf[256];
            char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "%s \"%s\" shadowing "
                "previous %s definition "
                "in line %" PRId64 ", column %" PRId64
                "%s",
                deftype, _shortenedname(describebuf, exprname),
                _identifierdeclarationname(
                    shadoweduse->declarationexpr, exprname
                ),
                shadoweduse->declarationexpr->line,
                shadoweduse->declarationexpr->column,
                warningtypetext
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_WARNING, buf, context->fileuri,
                    _refline(
                        context->tokenstreaminfo,
                        parsethis->tokens, i),
                    _refcol(
                        context->tokenstreaminfo,
                        parsethis->tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
        }
        if (!forbidden) {
            if (appends_to_sdef) *appends_to_sdef = NULL;
            return 1;
        }
    }
    if (outofmemory) *outofmemory = 0;
    return 0;
}

int ast_ProcessNewScopeIdentifier(
        h64parsecontext *context,
        h64parsethis *parsethis,
        h64expression *expr,
        const char *identifier,
        int identifierindex,
        h64scope *_add_to_this_scope_instead_of_default,
        int *outofmemory
        ) {
    int i = identifierindex;
    int success = 0;
    if (IdentifierIsReserved(identifier)) {
        char buf[256]; char describebuf[64];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected identifier \"%s\", "
            "this identifier is reserved and cannot be redefined",
            _shortenedname(describebuf, identifier)
        );
        if (!result_AddMessage(
                context->resultmsg,
                H64MSG_ERROR, buf, context->fileuri,
                _refline(context->tokenstreaminfo, parsethis->tokens, i),
                _refcol(context->tokenstreaminfo, parsethis->tokens, i)
                )) {
            if (outofmemory) *outofmemory = 1;
            return 0;
        }
    } else {
        int scopeaddoom = 0;
        h64scopedef *appends_to_sdef = NULL;
        if (ast_CanAddNameToScopeCheck(
                context, parsethis, expr, i - 1,
                &appends_to_sdef, &scopeaddoom
                )) {
            if (appends_to_sdef == NULL) {
                int scopeoom = 0;
                if (!scope_AddItem(
                        (_add_to_this_scope_instead_of_default != NULL ?
                         _add_to_this_scope_instead_of_default :
                          parsethis->scope),
                        identifier, expr, &scopeoom
                        )) {
                    if (scopeoom)
                        if (outofmemory) *outofmemory = 1;
                    return 0;
                }
            } else {
                h64expression **additionaldeclnew = realloc(
                    appends_to_sdef->additionaldecl,
                    sizeof(*appends_to_sdef->additionaldecl) * (
                    appends_to_sdef->additionaldecl_count + 1
                    )
                );
                if (!additionaldeclnew) {
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
                appends_to_sdef->additionaldecl = additionaldeclnew;
                appends_to_sdef->additionaldecl[
                    appends_to_sdef->additionaldecl_count
                ] = expr;
                appends_to_sdef->additionaldecl_count++;
            }
            success = 1;
        } else {
            if (scopeaddoom) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
        }
    }
    if (outofmemory) *outofmemory = 0;
    return success;
}

int ast_ParseExprStmt(
        h64parsecontext *context,
        h64parsethis *parsethis,
        int statementmode,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    int max_tokens_touse = parsethis->max_tokens_touse;
    h64token *tokens = parsethis->tokens;
    const char *fileuri = context->fileuri;

    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[128];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            context->resultmsg, buf, fileuri,
            _refline(context->tokenstreaminfo, tokens, 0),
            _refcol(context->tokenstreaminfo, tokens, 0));
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *expr = ast_AllocExpr(context->ast);
    if (!expr) {
        result_ErrorNoLoc(
            context->resultmsg,
            "failed to allocate expression, "
            "out of memory?",
            fileuri
        );
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));
    expr->storage.eval_temp_id = -1;

    expr->line = tokens[0].line;
    expr->column = tokens[0].column;
    expr->tokenindex = 0 + (
        ((char*)tokens -
         (char*)context->tokenstreaminfo->token) / sizeof(*tokens)
    );

    // Variable definitions:
    if (tokens[0].type == H64TK_KEYWORD &&
            (strcmp(tokens[0].str_value, "var") == 0 ||
             strcmp(tokens[0].str_value, "const") == 0)) {
        int i = 1;
        expr->type = H64EXPRTYPE_VARDEF_STMT;
        if (strcmp(tokens[0].str_value, "const") == 0) {
            expr->vardef.is_const = 1;
        }
        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256];
            char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier to name variable "
                "instead",
                _describetoken(describebuf,
                    context->tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(
                        context->tokenstreaminfo, tokens, i),
                    _refcol(
                        context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->vardef.identifier = strdup(tokens[i].str_value);
        i++;
        if (!expr->vardef.identifier) {
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }

        {
            int newidentifieroom = 0;
            if (!ast_ProcessNewScopeIdentifier(
                    context, parsethis, expr,
                    expr->vardef.identifier, i - 1, NULL,
                    &newidentifieroom
                    )) {
                if (newidentifieroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
            }
        }

        while (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD) {
            if (strcmp(tokens[i].str_value, "deprecated") == 0) {
                expr->vardef.is_deprecated = 1;
                i++;
                continue;
            }
            break;
        }

        char describebuf[64];
        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_BINOPSYMBOL &&
                IS_ASSIGN_OP(tokens[i].int_value)) {
            if (tokens[i].int_value != H64OP_ASSIGN) {
                if (outofmemory) *outofmemory = 0;
                char buf[512]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected '%s', "
                    "expected '=' instead to assign variable "
                    "default value",
                    operator_OpPrintedAsStr(tokens[i].int_value)
                );
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(
                            context->tokenstreaminfo, tokens, i),
                        _refcol(
                            context->tokenstreaminfo, tokens, i)
                        )) {
                    if (outofmemory) *outofmemory = 1;
                    scope_RemoveItem(
                        parsethis->scope, expr->vardef.identifier
                    );
                    if (parsefail) *parsefail = 0;
                    return 0;
                }
                int oldi = i;
                ast_ParseRecover_FindNextStatement(
                    context->tokenstreaminfo, tokens,
                    max_tokens_touse, &i,
                    RECOVERFLAGS_MUSTFORWARD
                );
                assert(i > oldi || i >= max_tokens_touse);
                *out_expr = expr;
                if (out_tokenlen) *out_tokenlen = i;
                if (parsefail) *parsefail = 0;
                return 1;
            }
            i++;
            int tlen = 0;
            int _innerparsefail = 0;
            int _inneroutofmemory = 0;
            h64expression *innerexpr = NULL;
            h64parsethis _buf;
            if (!ast_ParseExprInline(
                    context, newparsethis(
                        &_buf, parsethis,
                        &tokens[i], max_tokens_touse - i
                    ),
                    INLINEMODE_GREEDY,
                    &_innerparsefail,
                    &_inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    )) {
                if (_inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    scope_RemoveItem(
                        parsethis->scope, expr->vardef.identifier
                    );
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                if (!_innerparsefail) {
                    char buf[512]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected inline value assigned to "
                        "variable definition in line %"
                        PRId64 ", column %" PRId64 " instead",
                        _describetoken(describebuf,
                            context->tokenstreaminfo, tokens, i),
                        expr->line, expr->column
                    );
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(
                                context->tokenstreaminfo, tokens, i),
                            _refcol(
                                context->tokenstreaminfo, tokens, i)
                            )) {
                        if (outofmemory) *outofmemory = 1;
                        scope_RemoveItem(
                            parsethis->scope, expr->vardef.identifier
                        );
                        if (parsefail) *parsefail = 0;
                        return 0;
                    }
                    int oldi = i;
                    ast_ParseRecover_FindNextStatement(
                        context->tokenstreaminfo, tokens,
                        max_tokens_touse, &i,
                        RECOVERFLAGS_MUSTFORWARD
                    );
                    assert(i > oldi || i >= max_tokens_touse);
                }
                *out_expr = expr;
                if (out_tokenlen) *out_tokenlen = i;
                if (parsefail) *parsefail = 0;
                return 1;
            }
            expr->vardef.value = innerexpr;
            i += tlen;
        }
        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // Function declarations:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "func") == 0) {
        expr->type = H64EXPRTYPE_FUNCDEF_STMT;
        expr->funcdef.bytecode_func_id = -1;
        expr->funcdef.scope.parentscope = parsethis->scope;
        assert(!parsethis->scope ||
               parsethis->scope->magicinitnum == SCOPEMAGICINITNUM);
        if (!scope_Init(&expr->funcdef.scope)) {
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->funcdef.scope.classandfuncnestinglevel =
            expr->funcdef.scope.parentscope->classandfuncnestinglevel + 1;

        int i = 1;
        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier to name function "
                "instead",
                _reftokname(context->tokenstreaminfo, tokens, i)
            );
            if (parsefail) *parsefail = 1;
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->funcdef.name = strdup(tokens[i].str_value);
        i++;
        if (!expr->funcdef.name) {
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }

        {
            int newidentifieroom = 0;
            if (!ast_ProcessNewScopeIdentifier(
                    context, parsethis, expr,
                    expr->funcdef.name, i - 1, NULL,
                    &newidentifieroom
                    )) {
                if (newidentifieroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
            }
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_BRACKET &&
                tokens[i].char_value == '(') {
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            h64parsethis _buf;
            if (!ast_ParseFuncDefArgs(
                    context, newparsethis_newscope(
                        &_buf, parsethis, &expr->funcdef.scope,
                        tokens + i, max_tokens_touse - i
                    ), expr,
                    &innerparsefail, &inneroom,
                    &expr->funcdef.arguments, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    scope_RemoveItem(parsethis->scope, expr->funcdef.name);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                } else if (innerparsefail) {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    scope_RemoveItem(parsethis->scope, expr->funcdef.name);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected function argument list for function "
                    "definition starting in line %" PRId64
                    ", column %" PRId64,
                    _reftokname(context->tokenstreaminfo, tokens, i),
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                );
                if (outofmemory) *outofmemory = 0;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                scope_RemoveItem(parsethis->scope, expr->funcdef.name);
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i += tlen;
        }
        while (1) {
            if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->funcdef.is_threadable &&
                    strcmp(tokens[i].str_value, "threadable") == 0) {
                i++;
                expr->funcdef.is_threadable = 1;
                continue;
            } else if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->funcdef.is_deprecated &&
                    strcmp(tokens[i].str_value, "deprecated") == 0) {
                i++;
                expr->funcdef.is_deprecated = 1;
                continue;
            } else if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->funcdef.is_getter &&
                    strcmp(tokens[i].str_value, "getter") == 0) {
                i++;
                expr->funcdef.is_getter = 1;
                continue;
            } else if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->funcdef.is_setter &&
                    strcmp(tokens[i].str_value, "setter") == 0) {
                i++;
                expr->funcdef.is_setter = 1;
                continue;
            }
            break;
        }
        if (expr->funcdef.is_setter && expr->funcdef.is_getter) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected combination of getter/setter, "
                "expected just one for function definition "
                " starting in line %" PRId64
                ", column %" PRId64,
                _refline(context->tokenstreaminfo, tokens, 0),
                _refcol(context->tokenstreaminfo, tokens, 0)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                scope_RemoveItem(parsethis->scope, expr->funcdef.name);
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        }
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        h64parsethis _buf;
        if (!ast_ParseCodeBlock(
                context, newparsethis_newscope(
                    &_buf, parsethis,
                    &expr->funcdef.scope,
                    tokens + i, max_tokens_touse - i
                ),
                statementmode,
                &expr->funcdef.stmt, &expr->funcdef.stmt_count,
                &innerparsefail, &inneroom, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                scope_RemoveItem(parsethis->scope, expr->funcdef.name);
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            if (outofmemory) *outofmemory = 0;
            if (!innerparsefail && !result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, "internal error: failed to "
                    "get code block somehow", fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            scope_RemoveItem(parsethis->scope, expr->funcdef.name);
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i += tlen;
        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // Class definitions:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "class") == 0) {
        int i = 0;
        if (statementmode != STATEMENTMODE_TOPLEVEL) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"class\", "
                "this is not valid anywhere but at the top level"
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_CLASSDEF_STMT;
        expr->classdef.bytecode_class_id = -1;
        expr->classdef.scope.parentscope = parsethis->scope;
        if (!scope_Init(&expr->classdef.scope)) {
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->classdef.scope.classandfuncnestinglevel =
            expr->classdef.scope.parentscope->classandfuncnestinglevel + 1;
        i++;
        while (1) {  // read attributes
            if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->classdef.is_deprecated &&
                    strcmp(tokens[i].str_value, "deprecated") == 0) {
                i++;
                expr->classdef.is_deprecated = 1;
                continue;
            } else if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->classdef.is_threadable &&
                    strcmp(tokens[i].str_value, "threadable") == 0) {
                i++;
                expr->classdef.is_threadable = 1;
                continue;
            }
            break;
        }

        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier for class name",
                _describetoken(
                    describebuf,
                    context->tokenstreaminfo, tokens, i
                )
            );
            if (parsefail) *parsefail = 1;
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->classdef.name = strdup(tokens[i].str_value);
        if (!expr->classdef.name) {
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i++;

        {
            int newidentifieroom = 0;
            if (!ast_ProcessNewScopeIdentifier(
                    context, parsethis, expr,
                    expr->classdef.name, i - 1, NULL,
                    &newidentifieroom
                    )) {
                if (newidentifieroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
            }
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "extends") == 0) {
            i++;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroutofmemory = 0;
            h64expression *innerexpr = NULL;
            h64parsethis _buf;
            if (!ast_ParseExprInline(
                    context, newparsethis_newscope(
                        &_buf, parsethis, &expr->classdef.scope,
                        &tokens[i], max_tokens_touse - i
                    ),
                    INLINEMODE_GREEDY,
                    &innerparsefail,
                    &inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    ) || !ast_CanBeClassRef(innerexpr)) {
                if (innerexpr)
                    ast_MarkExprDestroyed(innerexpr);
                if (inneroutofmemory) {
                    if (parsefail) *parsefail = 0;
                    if (outofmemory) *outofmemory = 1;
                    scope_RemoveItem(parsethis->scope, expr->classdef.name);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (innerparsefail) {
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    scope_RemoveItem(parsethis->scope, expr->classdef.name);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected reference to base class "
                    "to extend from",
                    _describetoken(
                        describebuf,
                        context->tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                scope_RemoveItem(parsethis->scope, expr->classdef.name);
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->classdef.baseclass_ref = innerexpr;
            i += tlen;
        }

        while (1) {  // read attributes
            if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->classdef.is_deprecated &&
                    strcmp(tokens[i].str_value, "deprecated") == 0) {
                i++;
                expr->classdef.is_deprecated = 1;
                continue;
            } else if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    !expr->classdef.is_threadable &&
                    strcmp(tokens[i].str_value, "threadable") == 0) {
                i++;
                expr->classdef.is_threadable = 1;
                continue;
            }
            break;
        }

        // Extract class contents:
        int stmt_count = 0;
        h64expression **stmt = NULL;
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        h64parsethis _buf;
        if (!ast_ParseCodeBlock(
                context, newparsethis_newscope(
                    &_buf, parsethis, &expr->classdef.scope,
                    tokens + i, max_tokens_touse - i
                ),
                STATEMENTMODE_INCLASS,
                &stmt, &stmt_count,
                &innerparsefail, &inneroom, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
            } else {
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
            }
            classparsefail: ;
            int k = 0;
            while (k < stmt_count) {
                ast_MarkExprDestroyed(stmt[k]);
                k++;
            }
            free(stmt);
            scope_RemoveItem(parsethis->scope, expr->classdef.name);
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i += tlen;

        // Separate actual definition types:
        int vardefcount = 0;
        int funcdefcount = 0;
        int nameoom = 0;
        int k = 0;
        while (k < stmt_count) {
            assert(stmt[k]->type == H64EXPRTYPE_VARDEF_STMT ||
                    stmt[k]->type == H64EXPRTYPE_FUNCDEF_STMT);
            int64_t nameindex = -1;
            if (stmt[k]->type == H64EXPRTYPE_VARDEF_STMT) {
                if (stmt[k]->vardef.identifier != NULL) {
                    nameindex = (
                        h64debugsymbols_AttributeNameToAttributeNameId(
                            context->project->program->symbols,
                            stmt[k]->vardef.identifier, 1
                        ));
                    if (nameindex < 0) nameoom = 1;
                }
                vardefcount++;
            } else {
                if (stmt[k]->funcdef.name != NULL) {
                    nameindex = (
                        h64debugsymbols_AttributeNameToAttributeNameId(
                            context->project->program->symbols,
                            stmt[k]->funcdef.name, 1
                        ));
                    if (nameindex < 0) nameoom = 1;
                }
                funcdefcount++;
            }
            k++;
        }
        if (nameoom) {
            // Got out of memory trying to map attribute name to index:
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            goto classparsefail;
        }
        if (funcdefcount > 0) {
            expr->classdef.funcdef = malloc(
                sizeof(*expr->classdef.funcdef) *
                funcdefcount
            );
            if (!expr->classdef.funcdef) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                goto classparsefail;
            }
            expr->classdef.funcdef_count = funcdefcount;
            int j = 0;
            k = 0;
            while (k < stmt_count) {
                if (stmt[k]->type == H64EXPRTYPE_FUNCDEF_STMT) {
                    expr->classdef.funcdef[j] = stmt[k];
                    stmt[k] = NULL;
                    j++;
                }
                k++;
            }
        }
        if (vardefcount > 0) {
            expr->classdef.vardef = malloc(
                sizeof(*expr->classdef.vardef) *
                vardefcount
            );
            if (!expr->classdef.vardef) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                goto classparsefail;
            }
            expr->classdef.vardef_count = vardefcount;
            int j = 0;
            k = 0;
            while (k < stmt_count) {
                if (stmt[k] &&
                        stmt[k]->type == H64EXPRTYPE_VARDEF_STMT) {
                    expr->classdef.vardef[k] = stmt[k];
                    stmt[k] = NULL;
                    j++;
                }
                k++;
            }
        }
        #ifndef NDEBUG
        k = 0;
        while (k < stmt_count) {
            assert(stmt[k] == NULL);
            k++;
        }
        #endif
        free(stmt);
        stmt = NULL;

        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        return 1;
    }

    // do statements:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "do") == 0) {
        int i = 0;
        if (statementmode != STATEMENTMODE_INFUNC &&
                statementmode != STATEMENTMODE_INCLASSFUNC) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"try\", "
                "this is only allowed in functions"
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i++;
        expr->type = H64EXPRTYPE_DO_STMT;

        // Get code block in do { ... }
        {
            expr->dostmt.doscope.parentscope = parsethis->scope;
            if (!scope_Init(&expr->dostmt.doscope)) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->dostmt.doscope.classandfuncnestinglevel =
                expr->dostmt.doscope.parentscope->classandfuncnestinglevel;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            h64parsethis _buf;
            if (!ast_ParseCodeBlock(
                    context, newparsethis_newscope(
                        &_buf, parsethis, &expr->dostmt.doscope,
                        tokens + i, max_tokens_touse - i
                    ),
                    statementmode,
                    &expr->dostmt.dostmt,
                    &expr->dostmt.dostmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i += tlen;
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "rescue") == 0) {
            expr->dostmt.rescuescope.parentscope = parsethis->scope;
            if (!scope_Init(&expr->dostmt.rescuescope)) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->dostmt.rescuescope.classandfuncnestinglevel =
                expr->dostmt.rescuescope.parentscope->
                classandfuncnestinglevel;
            int catch_i = i;
            i++;
            while (1) {
                int tlen = 0;
                int innerparsefail = 0;
                int inneroutofmemory = 0;
                h64expression *innerexpr = NULL;
                h64parsethis _buf;
                if (!ast_ParseExprInline(
                        context, newparsethis_newscope(
                            &_buf, parsethis, &expr->dostmt.rescuescope,
                            &tokens[i], max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &innerparsefail,
                        &inneroutofmemory, &innerexpr,
                        &tlen, nestingdepth
                        )) {
                    if (inneroutofmemory) {
                        if (parsefail) *parsefail = 0;
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    if (!innerparsefail) {
                        char buf[256]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected expression describing caught "
                            "error for catch clause in line %" PRId64
                            ", column, %" PRId64,
                            _describetoken(
                                describebuf, context->tokenstreaminfo,
                                tokens, i
                            ),
                            _refline(context->tokenstreaminfo,
                                     tokens, catch_i),
                            _refcol(context->tokenstreaminfo,
                                    tokens, catch_i)
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(context->tokenstreaminfo,
                                         tokens, i),
                                _refcol(context->tokenstreaminfo,
                                        tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i += tlen;

                h64expression **new_errors = realloc(
                    expr->dostmt.errors,
                    sizeof(*new_errors) *
                    (expr->dostmt.dostmt_count + 1)
                );
                if (!new_errors) {
                    if (parsefail) *parsefail = 0;
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(innerexpr);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                expr->dostmt.errors = new_errors;
                expr->dostmt.errors[
                    expr->dostmt.errors_count
                ] = innerexpr;
                expr->dostmt.errors_count++;

                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_COMMA) {
                    i++;
                    continue;
                }
                break;
            }
            if (i >= max_tokens_touse ||
                    ((tokens[i].type != H64TK_KEYWORD ||
                      strcmp(tokens[i].str_value, "as") != 0) &&
                     (tokens[i].type != H64TK_BRACKET ||
                      tokens[i].char_value != '{'))) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected \"as\" or \"{\" for catch clause "
                    "in line %" PRId64
                    ", column, %" PRId64,
                    _describetoken(
                        describebuf, context->tokenstreaminfo, tokens, i
                    ),
                    _refline(context->tokenstreaminfo, tokens, catch_i),
                    _refcol(context->tokenstreaminfo, tokens, catch_i)
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            int named_error = (tokens[i].type == H64TK_KEYWORD);
            if (named_error) i++;
            if (named_error && (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER)) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier to name error "
                    "for catch clause "
                    "in line %" PRId64
                    ", column, %" PRId64,
                    _describetoken(
                        describebuf, context->tokenstreaminfo, tokens, i
                    ),
                    _refline(context->tokenstreaminfo, tokens, catch_i),
                    _refcol(context->tokenstreaminfo, tokens, catch_i)
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            if (named_error) {
                expr->dostmt.error_name = strdup(
                    tokens[i].str_value
                );
                if (!expr->dostmt.error_name) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                {
                    int newidentifieroom = 0;
                    if (!ast_ProcessNewScopeIdentifier(
                            context, parsethis, expr,
                            expr->dostmt.error_name, i - 1,
                            &expr->dostmt.rescuescope,
                            &newidentifieroom
                            )) {
                        if (newidentifieroom) {
                            if (outofmemory) *outofmemory = 1;
                            ast_MarkExprDestroyed(expr);
                            return 0;
                        }
                    }
                }
                i++;
            }

            // Get code block in rescue { ... }
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            h64parsethis _buf;
            if (!ast_ParseCodeBlock(
                    context, newparsethis_newscope(
                        &_buf, parsethis, &expr->dostmt.rescuescope,
                        tokens + i, max_tokens_touse - i
                    ),
                    statementmode,
                    &expr->dostmt.rescuestmt,
                    &expr->dostmt.rescuestmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i += tlen;
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "finally") == 0) {
            i++;

            // Get code block in finally { ... }
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            h64parsethis _buf;
            expr->dostmt.has_finally_block = 1;
            expr->dostmt.finallyscope.parentscope = parsethis->scope;
            if (!scope_Init(&expr->dostmt.finallyscope)) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->dostmt.finallyscope.classandfuncnestinglevel =
                expr->dostmt.finallyscope.parentscope->classandfuncnestinglevel;
            if (!ast_ParseCodeBlock(
                    context, newparsethis_newscope(
                        &_buf, parsethis, &expr->dostmt.finallyscope,
                        tokens + i, max_tokens_touse - i
                    ),
                    statementmode,
                    &expr->dostmt.finallystmt,
                    &expr->dostmt.finallystmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i += tlen;
        }

        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // import statements:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "import") == 0) {
        int brokenimport = 0;
        int i = 0;
        if (statementmode != STATEMENTMODE_TOPLEVEL) {
            brokenimport = 1;
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"import\", "
                "this is only allowed outside of functions and classes"
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        }
        i++;
        expr->type = H64EXPRTYPE_IMPORT_STMT;
        memset(&expr->importstmt, 0, sizeof(expr->importstmt));

        // Get import path:
        while (1) {
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier for import path",
                    _describetoken(
                        describebuf, context->tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            char **new_elements = realloc(
                expr->importstmt.import_elements,
                sizeof(*new_elements) *
                (expr->importstmt.import_elements_count + 1)
            );
            if (!new_elements) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->importstmt.import_elements = new_elements;
            expr->importstmt.import_elements[
                expr->importstmt.import_elements_count
            ] = strdup(tokens[i].str_value);
            if (!expr->importstmt.import_elements[
                    expr->importstmt.import_elements_count
                    ]) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->importstmt.import_elements_count++;
            i++;

            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_BINOPSYMBOL ||
                    tokens[i].int_value !=
                    H64OP_ATTRIBUTEBYIDENTIFIER)
                break;
            i++;  // skip past dot and continue
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "from") == 0) {
            i++;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier following \"from\" keyword",
                    _describetoken(
                        describebuf, context->tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->importstmt.source_library = strdup(
                tokens[i].str_value
            );
            if (!expr->importstmt.source_library) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i++;
        }

        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "as") == 0) {
            i++;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier following \"as\" keyword",
                    _describetoken(
                        describebuf, context->tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->importstmt.import_as = strdup(
                tokens[i].str_value
            );
            if (!expr->importstmt.import_as) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i++;
        }

        if (!brokenimport) {
            int newidentifieroom = 0;
            if (!ast_ProcessNewScopeIdentifier(
                    context, parsethis, expr, (
                        expr->importstmt.import_as ?
                        expr->importstmt.import_as :
                        expr->importstmt.import_elements[0]
                    ), i - 1, NULL,
                    &newidentifieroom
                    )) {
                if (newidentifieroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
            }
        }

        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // return statements:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "return") == 0) {
        int i = 0;
        if (statementmode != STATEMENTMODE_INFUNC &&
                statementmode != STATEMENTMODE_INCLASSFUNC) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"return\", "
                "this is not valid outside of functions"
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_RETURN_STMT;
        i++;

        int tlen = 0;
        int innerparsefail = 0;
        int inneroutofmemory = 0;
        h64expression *innerexpr = NULL;
        h64parsethis _buf;
        if (!ast_ParseExprInline(
                context, newparsethis(
                    &_buf, parsethis,
                    &tokens[i], max_tokens_touse - i
                ),
                INLINEMODE_GREEDY,
                &innerparsefail,
                &inneroutofmemory, &innerexpr,
                &tlen, nestingdepth
                )) {
            if (inneroutofmemory) {
                if (parsefail) *parsefail = 0;
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            innerexpr = NULL;
            tlen = 0;
        }
        i += tlen;

        expr->returnstmt.returned_expression = innerexpr;
        *out_expr = expr;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        if (out_tokenlen) *out_tokenlen = i;
        return 1;
    }

    // 'with' statements:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "with") == 0) {
        expr->type = H64EXPRTYPE_WITH_STMT;
        expr->withstmt.scope.parentscope = parsethis->scope;
        if (!scope_Init(&expr->withstmt.scope)) {
            if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }

        int clause_count = 0;
        int i = 1;
        while (1) {
            // Allocate space for next with clause:
            clause_count++;
            h64expression **withclause_new = realloc(
                expr->withstmt.withclause, sizeof(
                    *withclause_new
                ) * clause_count);
            if (!withclause_new) {
                if (parsefail) *parsefail = 0;
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            expr->withstmt.withclause = withclause_new;
            h64expression *withclause = ast_AllocExpr(context->ast);
            if (!withclause) {
                if (parsefail) *parsefail = 0;
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            memset(withclause, 0, sizeof(*withclause));
            withclause->type = H64EXPRTYPE_WITH_CLAUSE;
            expr->withstmt.withclause[
                expr->withstmt.withclause_count
            ] = withclause;
            expr->withstmt.withclause_count++;
            assert(expr->withstmt.withclause_count == clause_count);

            // Parse expression that is added in via 'with':
            h64expression *innerexpr = NULL;
            int tlen = 0;
            int _innerparsefail = 0;
            int _inneroutofmemory = 0;
            h64parsethis _buf;
            if (!ast_ParseExprInline(
                    context, newparsethis(
                        &_buf, parsethis,
                        &tokens[i], max_tokens_touse - i
                    ),
                    INLINEMODE_GREEDY, &_innerparsefail,
                    &_inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    )) {
                if (_inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (_innerparsefail) {
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected valid inline expression for "
                    "with-bound item",
                    _describetoken(
                        describebuf, context->tokenstreaminfo,
                        tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo,
                                 tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i++;
            withclause->withclause.foundinscope = &expr->withstmt.scope;
            withclause->withclause.withitem_value = innerexpr;

            // Make sure there is an 'as':
            if (i > max_tokens_touse ||
                    tokens[i].type != H64TK_KEYWORD ||
                    strcmp(tokens[i].str_value, "as") == 0) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected \"as\" before the name for "
                    "with-bound item",
                    _describetoken(
                        describebuf, context->tokenstreaminfo,
                        tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo,
                                 tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i++;

            // Get identifier:
            if (i > max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier to specify name for "
                    "with-bound item",
                    _describetoken(
                        describebuf, context->tokenstreaminfo,
                        tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(context->tokenstreaminfo,
                                 tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            withclause->withclause.withitem_identifier = strdup(
                tokens[i].str_value
            );
            i++;
            if (!withclause->withclause.withitem_identifier) {
                if (parsefail) *parsefail = 0;
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }

            // If there's a comma, continue and otherwise bail:
            if (i < max_tokens_touse && tokens[i].type == H64TK_COMMA) {
                i++;
                continue;
            }
            break;
        }

        // Parse the code block contents of with statement:
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        h64parsethis _buf;
        if (!ast_ParseCodeBlock(
                context, newparsethis_newscope(
                    &_buf, parsethis, &expr->whilestmt.scope,
                    tokens + i, max_tokens_touse - i
                ),
                statementmode,
                &expr->withstmt.stmt,
                &expr->withstmt.stmt_count,
                &innerparsefail, &inneroom, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            if (outofmemory) *outofmemory = 0;
            if (!innerparsefail && !result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, "internal error: failed to "
                    "get code block somehow", fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        i += tlen;
        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // 'if' and 'while' conditionals:
    if (tokens[0].type == H64TK_KEYWORD &&
            (strcmp(tokens[0].str_value, "if") == 0 ||
             strcmp(tokens[0].str_value, "while") == 0 ||
             strcmp(tokens[0].str_value, "for") == 0)) {
        int i = 0;
        if (statementmode != STATEMENTMODE_INFUNC &&
                statementmode != STATEMENTMODE_INCLASSFUNC) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected use of \"%s\", "
                "this is not valid outside of functions",
                tokens[0].str_value
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }

        int firstentry = 1;
        struct h64ifstmt *current_clause = NULL;
        while (1) {
            const char *__nm_while = "while";
            const char *__nm_for = "for";
            const char *__nm_elseif = "elseif";
            const char *__nm_else = "else";
            const char *stmt_name = "if";
            int in_elseif = 0;
            int in_else = 0;
            if (strcmp(tokens[i].str_value, "while") == 0) {
                expr->type = H64EXPRTYPE_WHILE_STMT;
                stmt_name = __nm_while;
                expr->whilestmt.scope.parentscope = parsethis->scope;
                // Note: scope_Init() is done further below
            } else if (strcmp(tokens[i].str_value, "for") == 0) {
                expr->type = H64EXPRTYPE_FOR_STMT;
                stmt_name = __nm_for;
                expr->forstmt.scope.parentscope = parsethis->scope;
            } else {
                in_elseif = 0;
                in_else = 0;
                if (firstentry) {
                    assert(strcmp(tokens[i].str_value, "if") == 0);
                    expr->type = H64EXPRTYPE_IF_STMT;
                    expr->ifstmt.scope.parentscope = parsethis->scope;
                } else if (strcmp(tokens[i].str_value, "elseif") == 0) {
                    in_elseif = 1;
                } else {
                    assert(strcmp(tokens[i].str_value, "else") == 0);
                    in_else = 1;
                }
            }
            i++;

            // Parse iterator label + "in" of for loops:
            const char *iteratorname = NULL;
            int _foridentifierindex = 0;
            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                if (i >= max_tokens_touse ||
                        tokens[i].type != H64TK_IDENTIFIER) {
                    char buf[256]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected identifier for iterator "
                        "of \"%s\" statement",
                        _describetoken(
                            describebuf, context->tokenstreaminfo,
                            tokens, i
                        ),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo, tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                _foridentifierindex = i;
                iteratorname = tokens[i].str_value;
                i++;
                if (i >= max_tokens_touse ||
                        tokens[i].type != H64TK_BINOPSYMBOL ||
                        tokens[i].int_value != H64OP_BOOLCOND_IN) {
                    char buf[256]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected identifier for iterator "
                        "of \"%s\" statement",
                        _describetoken(
                            describebuf, context->tokenstreaminfo,
                            tokens, i
                        ),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo,
                                     tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i++;
            }

            // Parse conditional, if any:
            h64expression *innerexpr = NULL;
            if (expr->type != H64EXPRTYPE_IF_STMT || !in_else) {
                int tlen = 0;
                int _innerparsefail = 0;
                int _inneroutofmemory = 0;
                h64parsethis _buf;
                if (!ast_ParseExprInline(
                        context, newparsethis(
                            &_buf, parsethis,
                            &tokens[i], max_tokens_touse - i
                        ),
                        INLINEMODE_GREEDY,
                        &_innerparsefail,
                        &_inneroutofmemory, &innerexpr,
                        &tlen, nestingdepth
                        )) {
                    if (_inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    if (_innerparsefail) {
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    char buf[256]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected valid inline expression for "
                        "%s of \"%s\" statement",
                        _describetoken(
                            describebuf, context->tokenstreaminfo,
                            tokens, i
                        ),
                        (expr->type == H64EXPRTYPE_FOR_STMT ?
                         "iterated container" : "conditional"),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo,
                                     tokens, i),
                            _refcol(context->tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i += tlen;
            }
            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                expr->forstmt.iterator_identifier = strdup(iteratorname);
                if (!expr->forstmt.iterator_identifier) {
                    if (outofmemory) *outofmemory = 1;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                expr->forstmt.iterated_container = innerexpr;
            } else if (expr->type == H64EXPRTYPE_IF_STMT) {
                if (in_elseif || in_else) {
                    struct h64ifstmt *new_current_clause = malloc(
                        sizeof(*new_current_clause)
                    );
                    if (!new_current_clause) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        ast_MarkExprDestroyed(innerexpr);
                        return 0;
                    }
                    memset(new_current_clause, 0,
                        sizeof(*new_current_clause));
                    if (current_clause) {
                        current_clause->followup_clause = new_current_clause;
                    } else {
                        expr->ifstmt.followup_clause = new_current_clause;
                    }
                    current_clause = new_current_clause;
                    if (in_elseif) {
                        current_clause->conditional = innerexpr;
                    } else {
                        assert(innerexpr == NULL);
                    }
                } else {
                    expr->ifstmt.conditional = innerexpr;
                    current_clause = &expr->ifstmt;
                }
            } else {
                assert(expr->type == H64EXPRTYPE_WHILE_STMT);
                expr->whilestmt.conditional = innerexpr;
            }

            h64expression ***stmt_ptr = NULL;
            h64scope *scope = NULL;
            int *stmt_count_ptr = NULL;
            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                stmt_ptr = &expr->forstmt.stmt;
                stmt_count_ptr = &expr->forstmt.stmt_count;
                scope = &expr->forstmt.scope;
            } else if (expr->type == H64EXPRTYPE_WHILE_STMT) {
                stmt_ptr = &expr->whilestmt.stmt;
                stmt_count_ptr = &expr->whilestmt.stmt_count;
                scope = &expr->whilestmt.scope;
            } else if (expr->type == H64EXPRTYPE_IF_STMT) {
                stmt_ptr = &current_clause->stmt;
                stmt_count_ptr = &current_clause->stmt_count;
                scope = &current_clause->scope;
            }
            scope->parentscope = parsethis->scope;
            if (!scope_Init(scope)) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            scope->classandfuncnestinglevel =
                scope->parentscope->classandfuncnestinglevel;

            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                // Add the iterator label to inner scope (but make sure
                // to test it for the OUTER one for shadowing):
                int newidentifieroom = 0;
                assert(_foridentifierindex >= 0);
                if (!ast_ProcessNewScopeIdentifier(
                        context, parsethis,
                        expr, expr->forstmt.iterator_identifier,
                        _foridentifierindex,
                        scope,  // add to inner scope, not outer one
                        &newidentifieroom
                        )) {
                    if (newidentifieroom) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                }
            }

            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            h64parsethis _buf;
            if (!ast_ParseCodeBlock(
                    context, newparsethis_newscope(
                        &_buf, parsethis, scope,
                        tokens + i, max_tokens_touse - i
                    ),
                    statementmode,
                    stmt_ptr, stmt_count_ptr,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        context->resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(context->tokenstreaminfo, tokens, i),
                        _refcol(context->tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            i += tlen;

            // Check continuation with further clauses:
            if (expr->type == H64EXPRTYPE_IF_STMT &&
                    i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    (strcmp(tokens[i].str_value, "elseif") == 0 ||
                     (strcmp(tokens[i].str_value, "else") == 0 &&
                      !in_else))
                    ) {
                firstentry = 0;
                continue;
            }

            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = i;
            if (outofmemory) *outofmemory = 0;
            if (parsefail) *parsefail = 0;
            return 1;
        }
    }

    // Assignments and function calls:
    if (tokens[0].type == H64TK_IDENTIFIER &&
            max_tokens_touse > 1) {
        int i = 0;
        if (statementmode != STATEMENTMODE_INFUNC &&
                statementmode != STATEMENTMODE_INCLASSFUNC) {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected statement starting with identifier "
                "\"%s\", "
                "this is not valid outside of functions",
                _shortenedname(describebuf, tokens[0].str_value)
            );
            if (!result_AddMessage(
                    context->resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(context->tokenstreaminfo, tokens, i),
                    _refcol(context->tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_MarkExprDestroyed(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_ASSIGN_STMT;
        int tlen = 0;
        int _innerparsefail = 0;
        int _inneroutofmemory = 0;
        h64expression *innerexpr = NULL;
        h64parsethis _buf;
        if (!ast_ParseExprInline(
                context, newparsethis(
                    &_buf, parsethis,
                    &tokens[i], max_tokens_touse - i
                ),
                INLINEMODE_GREEDY,
                &_innerparsefail,
                &_inneroutofmemory, &innerexpr,
                &tlen, nestingdepth
                )) {
            if (_inneroutofmemory) {
                if (outofmemory) *outofmemory = 1;
                ast_MarkExprDestroyed(expr);
                return 0;
            }
            if (_innerparsefail) {
                ast_MarkExprDestroyed(expr);
                return 0;
            }
        } else {
            assert(tlen > 0 && innerexpr != NULL);
            i += tlen;
            #ifdef H64AST_DEBUG
            char dbuf[64];
            char *lhandside = ast_ExpressionToJSONStr(innerexpr, NULL);
            printf("horsec: debug: checking statement with lvalue %s, "
                   "we're at token %d -> %s\n",
                   lhandside, i,
                   _describetoken(dbuf,
                        context->tokenstreaminfo, tokens, i));
            if (lhandside) free(lhandside);
            #endif

            if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_BINOPSYMBOL &&
                    IS_ASSIGN_OP(tokens[i].int_value)) {
                int operator = tokens[i].int_value;
                if (!ast_CanBeLValue(innerexpr)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected term at left hand of assignment, "
                        "expected a valid lvalue "
                        "instead"
                    );
                    if (!result_AddMessage(
                            context->resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(context->tokenstreaminfo,
                                     tokens, 0),
                            _refcol(context->tokenstreaminfo,
                                    tokens, 0)
                            )) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(innerexpr);
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    // Continue anyway, to return a usable AST
                }
                i++;
                int tlen = 0;
                int _innerparsefail = 0;
                int _inneroutofmemory = 0;
                h64expression *innerexpr2 = NULL;
                h64parsethis _buf;
                if (i >= max_tokens_touse ||
                        !ast_ParseExprInline(
                            context, newparsethis(
                                &_buf, parsethis,
                                &tokens[i], max_tokens_touse - i
                            ),
                            INLINEMODE_GREEDY,
                            &_innerparsefail,
                            &_inneroutofmemory, &innerexpr2,
                            &tlen, nestingdepth
                        )) {
                    if (_inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        ast_MarkExprDestroyed(expr);
                        return 0;
                    }
                    if (!_innerparsefail) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected inline value assigned to "
                            "assign statement starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(
                                describebuf,
                                context->tokenstreaminfo, tokens, i
                            ),
                            expr->line, expr->column
                        );
                        if (!result_AddMessage(
                                context->resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(context->tokenstreaminfo,
                                    tokens, i),
                                _refcol(context->tokenstreaminfo,
                                    tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_MarkExprDestroyed(innerexpr);
                    ast_MarkExprDestroyed(expr);
                    return 0;
                }
                i += tlen;
                expr->assignstmt.lvalue = innerexpr;
                expr->assignstmt.rvalue = innerexpr2;
                expr->assignstmt.assignop = operator;
                *out_expr = expr;
                if (out_tokenlen) *out_tokenlen = i;
                if (parsefail) *parsefail = 0;
                return 1;
            } else if (innerexpr->type == H64EXPRTYPE_CALL) {
                expr->type = H64EXPRTYPE_CALL_STMT;
                expr->callstmt.call = innerexpr;
                *out_expr = expr;
                if (out_tokenlen) *out_tokenlen = i;
                if (parsefail) *parsefail = 0;
                return 1;
            }
            ast_MarkExprDestroyed(innerexpr);
            innerexpr = NULL;
        }
        expr->type = H64EXPRTYPE_INVALID;  // no assign statement here, continue
    }
    if (parsefail) *parsefail = 0;
    ast_MarkExprDestroyed(expr);
    return 0;
}

int _ast_visit_in_setparent(
        h64expression *expr, h64expression *parent,
        ATTR_UNUSED void *ud
        ) {
    expr->parent = parent;
    return 1;
}

h64ast *ast_ParseFromTokens(
        h64compileproject *project, const char *fileuri,
        h64token *tokens, int token_count
        ) {
    h64ast *result = malloc(sizeof(*result));
    if (!result)
        return NULL;
    memset(result, 0, sizeof(*result));
    result->resultmsg.success = 1;
    result->scope.is_global = 1;
    result->basic_file_access_was_successful = 1;

    tsinfo tokenstreaminfo;
    memset(&tokenstreaminfo, 0, sizeof(tokenstreaminfo));
    tokenstreaminfo.token = tokens;
    tokenstreaminfo.token_count = token_count;

    if (!scope_Init(&result->scope)) {
        result_ErrorNoLoc(
            &result->resultmsg,
            "out of memory / alloc fail",
            fileuri
        );
        ast_FreeContents(result);
        result->resultmsg.success = 0;
        return result;
    }

    int i = 0;
    while (i < token_count) {
        h64expression *expr = NULL;
        int tlen = 0;
        int parsefail = 0;
        int oom = 0;
        h64parsecontext pcontext;
        memset(&pcontext, 0, sizeof(pcontext));
        pcontext.global_scope = &result->scope;
        pcontext.project = project;
        pcontext.ast = result;
        pcontext.resultmsg = &result->resultmsg;
        pcontext.fileuri = fileuri;
        pcontext.tokenstreaminfo = &tokenstreaminfo;
        h64parsethis pthis;
        memset(&pthis, 0, sizeof(pthis));
        pthis.scope = &result->scope;
        pthis.tokens = &tokens[i];
        pthis.max_tokens_touse = token_count - i;
        if (!ast_ParseExprStmt(
                &pcontext, &pthis,
                STATEMENTMODE_TOPLEVEL,
                &parsefail, &oom, &expr, &tlen,
                0
                )) {
            if (oom) {
                ast_FreeContents(result);
                result_ErrorNoLoc(
                    &result->resultmsg,
                    "out of memory / alloc fail",
                    fileuri
                );
                result->resultmsg.success = 0;
                return result;
            }
            result->resultmsg.success = 0;
            if (!parsefail) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected any recognized top level statement",
                    _describetoken(describebuf,
                        &tokenstreaminfo, tokens, i)
                );
                if (!result_AddMessage(
                        &result->resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(&tokenstreaminfo, tokens, i),
                        _refcol(&tokenstreaminfo, tokens, i)
                        ))
                    // OOM on final error msg? Not much we can do...
                    break;
            }
            int previ = i;
            ast_ParseRecover_FindNextStatement(
                &tokenstreaminfo, tokens, token_count, &i,
                RECOVERFLAGS_MUSTFORWARD
            );
            assert(i > previ || i >= token_count);
            continue;
        }
        h64expression **new_stmt = realloc(
            result->stmt,
            sizeof(*new_stmt) * (result->stmt_count + 1)
        );
        if (!new_stmt) {
            ast_MarkExprDestroyed(expr);
            ast_FreeContents(result);
            result_ErrorNoLoc(
                &result->resultmsg,
                "out of memory / alloc fail",
                fileuri
            );
            result->resultmsg.success = 0;
            return result;
        }
        result->stmt = new_stmt;
        result->stmt[result->stmt_count] = expr;
        result->stmt_count++;
        assert(tlen > 0);
        i += tlen;
    }

    assert(result->scope.magicinitnum == SCOPEMAGICINITNUM);
    i = 0;
    while (i < result->stmt_count) {
        assert(result->scope.magicinitnum == SCOPEMAGICINITNUM);
        if (!ast_VisitExpression(
                result->stmt[i], NULL,
                &_ast_visit_in_setparent, NULL, NULL, NULL)) {
            ast_FreeContents(result);
            result_ErrorNoLoc(
                &result->resultmsg,
                "out of memory / alloc fail",
                fileuri
            );
            result->resultmsg.success = 0;
            return result;
        }
        i++;
    }
    assert(result->scope.magicinitnum == SCOPEMAGICINITNUM);

    result->fileuri = uri_Normalize(fileuri, 1);
    if (!result->fileuri) {
        ast_FreeContents(result);
        result_ErrorNoLoc(
            &result->resultmsg,
            "out of memory / alloc fail",
            fileuri
        );
        result->resultmsg.success = 0;
        return result;
    }
    return result;
}

void ast_FreeContents(h64ast *ast) {
    if (!ast)
        return;
    int i = 0;
    while (i < ast->stmt_count) {
        ast_MarkExprDestroyed(ast->stmt[i]);
        i++;
    }
    ast->stmt_count = 0;
    free(ast->stmt);
    ast->stmt = NULL;
    free(ast->fileuri);
    ast->fileuri = NULL;
    free(ast->module_path);
    ast->module_path = NULL;
    free(ast->library_name);
    ast->library_name = NULL;
    if (ast->ast_expr_alloc) {
        poolalloc_Destroy(ast->ast_expr_alloc);
        ast->ast_expr_alloc = NULL;
    }
    scope_FreeData(&ast->scope);
}
