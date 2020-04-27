
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// #define H64AST_DEBUG

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/globallimits.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"


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
    } else if (token[i].type == H64TK_CONSTANT_INT) {
        snprintf(
            buf, maxlen - 1,
            "%" PRId64, token[i].int_value
        );
    }
    buf[maxlen - 1] = '\0';
    return buf;
}

void ast_ParseRecover_FindNextStatement(
        tsinfo *tokenstreaminfo, h64token *tokens,
        int max_tokens_touse, int *k
        ) {
    ptrdiff_t offset = (tokens - tokenstreaminfo->token);
    assert(offset >= 0);
    int starti = ((int64_t)offset) / (int64_t)sizeof(*tokens);
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
        } else if (tokens[i].type == H64TK_KEYWORD) {
            char *s = tokens[i].str_value;
            if (strcmp(s, "if") == 0 ||
                    strcmp(s, "var") == 0 ||
                    strcmp(s, "const") == 0 ||
                    strcmp(s, "for") == 0 ||
                    strcmp(s, "while") == 0 ||
                    strcmp(s, "func") == 0) {
                *k = i;
                return;
            }
        }
        i++;
    }
    *k = i;
}

void ast_ParseRecover_FindEndOfBlock(
        tsinfo *tokenstreaminfo,
        h64token *tokens, int max_tokens_touse, int *k
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
                }
            }
        } else if (tokens[i].type == H64TK_IDENTIFIER) {
            char *s = tokens[i].str_value;
            if (strcmp(s, "function") == 0 ||
                    strcmp(s, "class") == 0 ||
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
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int is_call,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }
    memset(out_funcargs, 0, sizeof(*out_funcargs));

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_ErrorNoLoc(resultmsg, buf, fileuri);
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    int i = 0;
    assert(tokens[0].type == H64TK_BINOPSYMBOL &&
            tokens[0].int_value == H64OP_CALL);
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
            ast_ClearFunctionArgs(out_funcargs);
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
            continue;
        }
        if (!is_call && !kwarg_name) {
            char buf[512]; char describebuf[64];
            int bugindex = i;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier for function argument name",
                    _describetoken(describebuf,
                        tokenstreaminfo, tokens, i)
                );
            } else {
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected ',' or ')' to resume argument list",
                    _describetoken(describebuf,
                        tokenstreaminfo, tokens, i + 1)
                );
                bugindex++;
            }
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, bugindex),
                    _refcol(tokenstreaminfo, tokens, bugindex)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_ClearFunctionArgs(out_funcargs);
            return 0;
        }

        int inneroom = 0;
        int innerparsefail = 0;
        h64expression *expr = NULL;
        int tlen = 0;

        assert(i > 0);
        if (i >= max_tokens_touse ||
                !ast_ParseExprInline(
                        fileuri, resultmsg,
                        addtoscope,
                        tokenstreaminfo, tokens + i, max_tokens_touse - i,
                        INLINEMODE_GREEDY,
                        &innerparsefail, &inneroom,
                        &expr, &tlen,
                        nestingdepth
                        )) {
            if (kwarg_name) free(kwarg_name);
            char buf[512]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected valid inline value for argument list",
                _describetoken(describebuf,
                    tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_ClearFunctionArgs(out_funcargs);
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
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return _ast_ParseFunctionArgList_Ex(
        fileuri, resultmsg, addtoscope,
        tokenstreaminfo, tokens, max_tokens_touse,
        1, parsefail, outofmemory,
        out_funcargs, out_tokenlen, nestingdepth
    );
}

int ast_ParseFuncDefArgs(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64funcargs *out_funcargs,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return _ast_ParseFunctionArgList_Ex(
        fileuri, resultmsg, addtoscope,
        tokenstreaminfo, tokens, max_tokens_touse,
        0, parsefail, outofmemory,
        out_funcargs, out_tokenlen, nestingdepth
    );
}

int ast_ParseExprInlineOperator_Recurse(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        h64expression *lefthandside,
        int lefthandsidetokenlen,
        int precedencelevel,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        // XXX: do NOT free lefthandside if no parse error.
        return 0;
    }
    assert(lefthandside || lefthandsidetokenlen == 0);

    #ifdef H64AST_DEBUG
    char describebuf[64];
    printf("horsecc: debug: OP PARSE FROM %d, token: %s\n", 0,
         _describetoken(describebuf, tokenstreaminfo, tokens, 0));
    #endif

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_ErrorNoLoc(resultmsg, buf, fileuri);
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *original_lefthand = lefthandside;
    int i = lefthandsidetokenlen;
    int operatorsprocessed = 0;

    // Parse left-hand side if we don't have any yet:
    if (i == 0 && !lefthandside &&
            tokens[i].type != H64TK_UNOPSYMBOL) {
        int inneroom = 0;
        int innerparsefail = 0;
        int tlen = 0;
        h64expression *innerexpr = NULL;
        if (!ast_ParseExprInline(
                fileuri, resultmsg,
                addtoscope,
                tokenstreaminfo, tokens, max_tokens_touse,
                INLINEMODE_NONGREEDY,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                return 0;
            } else if (innerparsefail) {
                if (outofmemory) *outofmemory = 0;
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                return 0;
            }
            if (parsefail) *parsefail = 0;
            // XXX: do NOT free lefthandside if no parse error.
            return 0;
        }
        lefthandside = innerexpr;
        lefthandsidetokenlen = tlen;
        i += tlen;
    } else if (!lefthandside && tokens[i].type == H64TK_BINOPSYMBOL) {
        char buf[512]; char describebuf[64];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected left hand value before binary operator",
            _describetoken(describebuf, tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(tokenstreaminfo, tokens, i),
                _refcol(tokenstreaminfo, tokens, i)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        if (original_lefthand && original_lefthand != lefthandside)
            ast_FreeExpression(original_lefthand);
        if (lefthandside) ast_FreeExpression(lefthandside);
        return 0;
    }

    #ifdef H64AST_DEBUG
    char describebufy[64];
    char *lhandside = ast_ExpressionToJSONStr(lefthandside, NULL); 
    printf("horsecc: debug: "
         "GOT LEFT HAND SIDE %s AND NOW AT %d %s - "
         "current handling level: %d\n", lhandside, i,
         _describetoken(describebufy, tokenstreaminfo, tokens, i),
         precedencelevel);
    if (lhandside) free(lhandside);
    #endif

    // Deal with operators we encounter:
    while (i < max_tokens_touse) {
        if (tokens[i].type != H64TK_BINOPSYMBOL &&
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
                _describetoken(describebuf, tokenstreaminfo, tokens, i),
                _refline(tokenstreaminfo, tokens, 0),
                _refline(tokenstreaminfo, tokens, 0)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            if (lefthandside) ast_FreeExpression(lefthandside);
            if (original_lefthand && original_lefthand != lefthandside)
                ast_FreeExpression(original_lefthand);
            return 0;
        }

        // Hand off different precedence levels:
        if (precedence < precedencelevel) {
            #ifdef H64AST_DEBUG
            printf("horsecc: debug: "
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
            if (!ast_ParseExprInlineOperator_Recurse(
                    fileuri, resultmsg,
                    addtoscope,
                    tokenstreaminfo, tokens + i - skipback,
                    max_tokens_touse - (i - skipback),
                    innerrighthand,
                    innerrighthandlen,
                    precedencelevel - 1,
                    &innerparsefail, &inneroom,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_FreeExpression(original_lefthand);
                    ast_FreeExpression(lefthandside);
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                } else if (innerparsefail) {
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_FreeExpression(original_lefthand);
                    ast_FreeExpression(lefthandside);
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    return 0;
                }
                break;
            }
            assert(innerexpr != NULL);
            assert(innerexpr->type == H64EXPRTYPE_BINARYOP ||
                innerexpr->type == H64EXPRTYPE_UNARYOP ||
                innerexpr->type == H64EXPRTYPE_CALL);
            if (lefthandside && i - skipback > 0) {
                if (lefthandside->type == H64EXPRTYPE_BINARYOP) {
                    lefthandside->op.value2 = innerexpr;
                } else {
                    assert(lefthandside->type == H64EXPRTYPE_UNARYOP);
                    lefthandside->op.value1 = innerexpr;
                }
                assert(tlen > innerrighthandlen &&
                    i - skipback + tlen > i);
            } else {
                if (lefthandside)
                    ast_FreeExpression(lefthandside);
                original_lefthand = NULL;
                lefthandside = innerexpr;
            }
            lefthandsidetokenlen = (i - skipback) + tlen;
            #ifdef H64AST_DEBUG
            char *lhandside = ast_ExpressionToJSONStr(lefthandside, NULL);
            printf("horsecc: debug: FROM RECURSIVE INNER, "
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
            printf("horsecc: debug: handling op %s at level %d\n",
                operator_OpPrintedAsStr(tokens[i].int_value),
               precedencelevel);
            #endif
        }
        i++; // go past operator

        int optokenoffset = i - 1;
        operatorsprocessed++;
        assert(
            tokens[i - 1].type == H64TK_BINOPSYMBOL ||
            i - 1 > 0
        );

        // Special handling of call right-hand side:
        if (tokens[i - 1].type == H64TK_BINOPSYMBOL &&
                tokens[i - 1].int_value == H64OP_CALL) {
            h64expression *callexpr = malloc(sizeof(*callexpr));
            if (!callexpr) {
                if (outofmemory) *outofmemory = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_FreeExpression(original_lefthand);
                return 0;
            }
            memset(callexpr, 0, sizeof(*callexpr));
            callexpr->line = tokens[i - 1].line;
            callexpr->column = tokens[i - 1].column;
            callexpr->type = H64EXPRTYPE_CALL;
            i--;
            int tlen = 0;
            int inneroom = 0;
            int innerparsefail = 0;
            if (!ast_ParseFuncCallArgs(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i,
                    max_tokens_touse - i,
                    &innerparsefail, &inneroom,
                    &callexpr->inlinecall.arguments,
                    &tlen, nestingdepth
                    )) {
                ast_FreeExpression(callexpr);
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (lefthandside) ast_FreeExpression(lefthandside);
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_FreeExpression(original_lefthand);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail) {
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, "internal error? "
                            "got no function args "
                            "but no error", fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                }
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_FreeExpression(original_lefthand);
                return 0;
            }
            i += tlen;
            lefthandside = callexpr;
            lefthandsidetokenlen = i;
            #ifdef H64AST_DEBUG
            char describebufy2[64];
            char *lhandside2 = ast_ExpressionToJSONStr(lefthandside, NULL);
            printf("horsecc: debug: "
                "GOT CALL ARGS %s AND NOW AT %d %s (tlen was: %d) - "
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
            if (i >= max_tokens_touse ||
                    !ast_ParseExprInline(
                    fileuri, resultmsg,
                    addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    INLINEMODE_NONGREEDY,
                    &innerparsefail, &inneroom,
                    &righthandside, &righthandsidelen,
                    nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (lefthandside) ast_FreeExpression(lefthandside);
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_FreeExpression(original_lefthand);
                    return 0;
                } else if (innerparsefail) {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    if (lefthandside) ast_FreeExpression(lefthandside);
                    if (original_lefthand && original_lefthand != lefthandside)
                        ast_FreeExpression(original_lefthand);
                    return 0;
                }
                char buf[512]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected right-hand side to binary operator",
                    _describetoken(describebuf,
                        tokenstreaminfo, tokens, i)
                );
                if (outofmemory) *outofmemory = 0;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                if (original_lefthand && original_lefthand != lefthandside)
                    ast_FreeExpression(original_lefthand);
                return 0;
            }
        }
        assert(righthandside != NULL && righthandsidelen > 0);
        i += righthandsidelen;

        h64expression *opexpr = malloc(sizeof(*opexpr));
        if (!opexpr) {
            if (outofmemory) *outofmemory = 1;
            if (lefthandside) ast_FreeExpression(lefthandside);
            if (original_lefthand && original_lefthand != lefthandside)
                ast_FreeExpression(original_lefthand);
            return 0;
        }
        memset(opexpr, 0, sizeof(*opexpr));
        opexpr->op.optype = tokens[optokenoffset].int_value;
        if (tokens[optokenoffset].type == H64TK_UNOPSYMBOL) {
            opexpr->type = H64EXPRTYPE_UNARYOP;
            assert(lefthandside == NULL);
            opexpr->op.value1 = righthandside;
        } else {
            opexpr->type = H64EXPRTYPE_BINARYOP;
            opexpr->op.value1 = lefthandside;
            opexpr->op.value2 = righthandside;
        }
        assert((optokenoffset > 0 ||
            tokens[optokenoffset].type == H64TK_UNOPSYMBOL) &&
            optokenoffset < i);
        opexpr->op.optokenoffset = optokenoffset;
        lefthandside = opexpr;
        lefthandsidetokenlen = i;
        opexpr->op.totaltokenlen = i;

        #ifdef H64AST_DEBUG
        char describebufy2[64];
        char *lhandside2 = ast_ExpressionToJSONStr(lefthandside, NULL);
        printf("horsecc: debug: "
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
            ast_FreeExpression(original_lefthand);
        *out_expr = lefthandside;
        *out_tokenlen = lefthandsidetokenlen;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        return 1;
    } else {
        if (lefthandside &&
                original_lefthand != lefthandside)
            ast_FreeExpression(lefthandside);
    }
    *out_expr = NULL;
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 0;
    return 0;
}

int ast_ParseExprInlineOperator(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return ast_ParseExprInlineOperator_Recurse(
        fileuri, resultmsg, addtoscope,
        tokenstreaminfo, tokens, max_tokens_touse,
        NULL, 0,
        operator_precedences_total_count - 1,
        parsefail, outofmemory,
        out_expr, out_tokenlen, nestingdepth
    ); 
}

int ast_ParseInlineFunc(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if ( max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_Error(
            resultmsg, buf, fileuri,
            _refline(tokenstreaminfo, tokens, 0),
            _refcol(tokenstreaminfo, tokens, 0)
        );
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *expr = malloc(sizeof(*expr));
    if (!expr) {
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));
    expr->type = H64EXPRTYPE_INLINEFUNC; 
    expr->line = _refline(tokenstreaminfo, tokens, 0);
    expr->column = _refcol(tokenstreaminfo, tokens, 0);
    int i = 0;
    if (tokens[0].type == H64TK_BRACKET &&
            tokens[0].char_value == '(') {
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        if (!ast_ParseFuncDefArgs(
                fileuri, resultmsg, addtoscope,
                tokenstreaminfo, tokens, max_tokens_touse,
                &innerparsefail, &inneroom,
                &expr->funcdef.arguments, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_FreeExpression(expr);
                return 0;
            } else if (innerparsefail) {
                if (outofmemory) *outofmemory = 0;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected function argument list for inline "
                "function definition",
                _reftokname(tokenstreaminfo, tokens, i)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, 0),
                    _refcol(tokenstreaminfo, tokens, 0)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_FreeExpression(expr);
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
        if (!expr->funcdef.arguments.arg_name ||
                !expr->funcdef.arguments.arg_value ||
                !expr->funcdef.arguments.arg_name[0]) {
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            ast_FreeExpression(expr);
            return 0;
        }
        i++;
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected function argument list for inline function",
            _reftokname(tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_FreeExpression(expr);
        return 0;
    }
    if (i >= max_tokens_touse ||
            tokens[i].type != H64TK_INLINEFUNC) {
        char buf[256];
        snprintf(buf, sizeof(buf) - 1,
            "unexpected %s, "
            "expected \"=>\" for inline function",
            _reftokname(tokenstreaminfo, tokens, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_FreeExpression(expr);
        return 0;
    }
    i++;
    int tlen = 0;
    int innerparsefail = 0;
    int inneroom = 0;
    if (!ast_ParseCodeBlock(
            fileuri, resultmsg, addtoscope,
            tokenstreaminfo, tokens + i, max_tokens_touse - i,
            STATEMENTMODE_INFUNC,
            &expr->funcdef.stmt, &expr->funcdef.stmt_count,
            &innerparsefail, &inneroom, &tlen, nestingdepth
            )) {
        if (inneroom) {
            if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 0;
            ast_FreeExpression(expr);
            return 0;
        }
        if (outofmemory) *outofmemory = 0;
        if (!innerparsefail && !result_AddMessage(
                resultmsg,
                H64MSG_ERROR, "internal error: failed to "
                "get code block somehow", fileuri,
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        ast_FreeExpression(expr);
        return 0;
    }
    i += tlen;
    *out_expr = expr;
    if (out_tokenlen) *out_tokenlen = 1;
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 0;
    return 1;
}

int ast_ParseExprInline(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int inlinemode,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse < 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_ErrorNoLoc(resultmsg, buf, fileuri);
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *expr = malloc(sizeof(*expr));
    if (!expr) {
        result_ErrorNoLoc(
            resultmsg,
            "failed to allocate expression, "
            "out of memory?",
            fileuri
        );
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));

    expr->line = tokens[0].line;
    expr->column = tokens[0].column;

    if (inlinemode == INLINEMODE_NONGREEDY) {
        if (tokens[0].type == H64TK_IDENTIFIER &&
                max_tokens_touse >= 2 &&
                tokens[1].type == H64TK_INLINEFUNC) {
            h64expression *innerexpr = NULL;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroutofmemory = 0;
            if (!ast_ParseInlineFunc(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens, max_tokens_touse,
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
                            resultmsg,
                            "internal error, unexpectedly failed "
                            "to parse inline func. this should never "
                            "happen, not even when out of memory...",
                            fileuri
                        );
                    }
                }
                ast_FreeExpression(expr);
                return 0;
            }
            assert(innerexpr != NULL);
            ast_FreeExpression(expr);
            expr = innerexpr;
            if (outofmemory) *outofmemory = 0;
            if (parsefail) *parsefail = 0;
            if (out_expr) *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = tlen;
            return 1;
        } else if (tokens[0].type == H64TK_IDENTIFIER) {
            expr->type = H64EXPRTYPE_IDENTIFIERREF;
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = 1;
            if (parsefail) *parsefail = 0;
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
                    ast_FreeExpression(expr);
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
            } else {
                // Should be impossible to reach!
                fprintf(stderr, "horsecc: error: UNHANDLED LITERAL TYPE\n");
                ast_FreeExpression(expr);
                if (outofmemory) *outofmemory = 1;
                return 0;
            }
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = 1;
            if (parsefail) *parsefail = 0;
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
                            fileuri, resultmsg, addtoscope,
                            tokenstreaminfo, tokens, max_tokens_touse,
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
                                    resultmsg,
                                    "internal error, unexpectedly failed "
                                    "to parse inline func. this should never "
                                    "happen, not even when out of memory...",
                                    fileuri
                                );
                            }
                        }
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    assert(innerexpr != NULL);
                    ast_FreeExpression(expr);
                    expr = innerexpr;
                    i += tlen;
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 0;
                    if (out_expr) *out_expr = expr;
                    if (out_tokenlen) *out_tokenlen = i;
                    return 1;
                }
            }
            // Ok, not an inline func. So this must be a normal broken:

            int tlen = 0;
            h64expression *innerexpr = NULL;
            int inneroom = 0;
            int innerparsefail = 0;
            int i = 1;
            if (!ast_ParseExprInline(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    INLINEMODE_NONGREEDY,
                    &innerparsefail, &inneroom,
                    &innerexpr, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                } else if (innerparsefail) {
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
            }
            assert(innerexpr != NULL);
            ast_FreeExpression(expr);
            expr = innerexpr;
            i += tlen;
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_BRACKET ||
                    tokens[i].char_value != ')') {
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected ')' corresponding to opening '(' "
                    "in line %" PRId64 ", column %" PRId64 " instead",
                    _reftokname(
                        tokenstreaminfo, tokens, i),
                    _refline(tokenstreaminfo, tokens, 0),
                    _refcol(tokenstreaminfo, tokens, 0)
                );
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
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
                ismap ? H64EXPRTYPE_MAP : H64EXPRTYPE_LIST
            );

            int hadanyitems = 0;
            int i = 1;
            while (1) {
                int hadcomma = 0;
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_COMMA) {
                    i++;
                }
                if (i < max_tokens_touse &&
                        tokens[i].type == H64TK_BRACKET &&
                        tokens[i].char_value == ']') {
                    i++;
                    break;
                }
                if (hadanyitems && !hadcomma) {
                    char buf[512]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected ']' or ',' resuming or ending %s "
                        "starting in line %"
                        PRId64 ", column %" PRId64 " instead",
                        _describetoken(describebuf,
                            tokenstreaminfo, tokens, i),
                        itemname,
                        expr->line, expr->column
                    );
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
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
                        ast_FreeExpression(expr);
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
                        ast_FreeExpression(expr);
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
                        ast_FreeExpression(expr);
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
                        ast_FreeExpression(expr);
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
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    expr->constructorvector.entry = new_entries;
                } else {
                    // Should never be reached
                    fprintf(stderr, "horsecc: error: unreachable "
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
                                tokenstreaminfo, tokens, i),
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
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
                            tokens[i].int_value < INT_MAX) {
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
                                tokenstreaminfo, tokens, i),
                            expect1dec, expect2dec,
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
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
                                tokenstreaminfo, tokens, i),
                            _refline(tokenstreaminfo, tokens, i - 1),
                            _refcol(tokenstreaminfo, tokens, i - 1)
                        );
                        if (parsefail) *parsefail = 1;
                        if (outofmemory) *outofmemory = 0;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    i++;
                }

                // Get next item:
                h64expression *innerexpr = NULL;
                int tlen = 0;
                int innerparsefail = 0;
                int inneroutofmemory = 0;
                if (!ast_ParseExprInline(
                        fileuri, resultmsg, addtoscope,
                        tokenstreaminfo, tokens, max_tokens_touse,
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
                                tokenstreaminfo, tokens, i),
                            (ismap ? "key" : "entry"),
                            itemname,
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_FreeExpression(expr);
                    return 0;
                }
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
                            tokenstreaminfo, tokens, i),
                        _refline(tokenstreaminfo, tokens, 0),
                        _refcol(tokenstreaminfo, tokens, 0)
                    );
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(innerexpr);
                    ast_FreeExpression(expr);
                    return 0;
                }
                h64expression *innerexpr2 = NULL;
                int tlen2 = 0;
                innerparsefail = 0;
                inneroutofmemory = 0;
                if (!ast_ParseExprInline(
                        fileuri, resultmsg, addtoscope,
                        tokenstreaminfo, tokens, max_tokens_touse,
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
                                tokenstreaminfo, tokens, i),
                            expr->line, expr->column
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_FreeExpression(innerexpr);
                    ast_FreeExpression(expr);
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
            if (out_tokenlen) *out_tokenlen = 1;
            return 1;
        }

        if (parsefail) *parsefail = 0;
        ast_FreeExpression(expr);
        return 0;
    }

    #ifdef H64AST_DEBUG
    char describebuf[64];
    printf("horsecc: debug: GREEDY PARSE FROM %d %s\n", 0,
         _describetoken(describebuf,
             tokenstreaminfo, tokens, 0));
    #endif

    // Try to greedily parse as full operator expression:
    {
        int tlen = 0;
        h64expression *innerexpr = NULL;
        int inneroom = 0;
        int innerparsefail = 0;
        if (!ast_ParseExprInlineOperator(
                fileuri, resultmsg, addtoscope,
                tokenstreaminfo, tokens, max_tokens_touse,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            } else if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_FreeExpression(expr);
                return 0;
            }
        } else {
            ast_FreeExpression(expr);
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
                fileuri, resultmsg, addtoscope,
                tokenstreaminfo, tokens, max_tokens_touse,
                INLINEMODE_NONGREEDY,
                &innerparsefail, &inneroom,
                &innerexpr, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            } else if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_FreeExpression(expr);
                return 0;
            }
        } else {
            ast_FreeExpression(expr);
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
    ast_FreeExpression(expr);
    return 0;
}

int ast_CanBeLValue(h64expression *e) {
    return 0;
}

int ast_CanBeClassRef(h64expression *e) {
    if (e->type == H64EXPRTYPE_IDENTIFIERREF) {
        return 1;
    } else if (e->type == H64EXPRTYPE_BINARYOP) {
        if (e->op.optype != H64OP_MEMBERBYIDENTIFIER)
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
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int statementmode,
        h64expression ***stmt_ptr,
        int *stmt_count_ptr,
        int *parsefail,
        int *outofmemory,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 1;
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, "unexpected missing code block, "
                "expected '{' for code block", fileuri,
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
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
                tokenstreaminfo, tokens, i)
        );
        if (parsefail) *parsefail = 1;
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(tokenstreaminfo, tokens, i),
                _refcol(tokenstreaminfo, tokens, i)
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
            h64expression *innerexpr = NULL;
            if (!ast_ParseExprStmt(
                    fileuri, resultmsg,
                    addtoscope,
                    tokenstreaminfo, &tokens[i], max_tokens_touse - i,
                    (statementmode != STATEMENTMODE_INCLASS &&
                     statementmode != STATEMENTMODE_INCLASSFUNC ?
                     STATEMENTMODE_INFUNC :
                     STATEMENTMODE_INCLASSFUNC),
                    &_innerparsefail,
                    &_inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    )) {
                if (_inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    return 0;
                }
                if (_innerparsefail) {
                    // Try to recover by finding next obvious
                    // statement, or possible function end:
                    int previ = i;
                    i++;
                    ast_ParseRecover_FindNextStatement(
                        tokenstreaminfo, tokens, max_tokens_touse, &i
                    );
                    assert(i > previ || i >= max_tokens_touse);
                    continue;
                }
                break;
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
                    ast_FreeExpression(innerexpr);
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
                "expected \"}\" to end "
                "code block opened with \"{\" in line %"
                PRId64 ", column %" PRId64 " instead",
                _describetoken(describebuf,
                    tokenstreaminfo, tokens, i),
                codeblock_line, codeblock_column
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    )) {
                if (outofmemory) *outofmemory = 1;
                return 0;
            } else {
                ast_ParseRecover_FindEndOfBlock(
                    tokenstreaminfo, tokens, max_tokens_touse, &i
                );
            }
        }
        i++;
        break;
    }
    if (out_tokenlen) *out_tokenlen = i;
    return 1;
}

int ast_ParseExprStmt(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        tsinfo *tokenstreaminfo,
        h64token *tokens,
        int max_tokens_touse,
        int statementmode,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        return 0;
    }

    nestingdepth++;
    if (nestingdepth > H64LIMIT_MAXPARSERECURSION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1,
            "exceeded maximum parser recursion of %d, "
            "less nesting expected", H64LIMIT_MAXPARSERECURSION
        );
        result_ErrorNoLoc(resultmsg, buf, fileuri);
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 1;
        return 0;
    }

    h64expression *expr = malloc(sizeof(*expr));
    if (!expr) {
        result_ErrorNoLoc(
            resultmsg,
            "failed to allocate expression, "
            "out of memory?",
            fileuri
        );
        if (outofmemory) *outofmemory = 1;
        return 0;
    }
    memset(expr, 0, sizeof(*expr));

    expr->line = tokens[0].line;
    expr->column = tokens[0].column;

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
                    tokenstreaminfo, tokens, i)
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(
                        tokenstreaminfo, tokens, i),
                    _refcol(
                        tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->vardef.identifier = strdup(tokens[i].str_value);
        i++;
        if (!expr->vardef.identifier) {
            if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        char describebuf[64];
        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_BINOPSYMBOL &&
                IS_ASSIGN_OP(tokens[i].int_value)) {
            i++;
            int tlen = 0;
            int _innerparsefail = 0;
            int _inneroutofmemory = 0;
            h64expression *innerexpr = NULL;
            if (!ast_ParseExprInline(
                    fileuri, resultmsg,
                    addtoscope,
                    tokenstreaminfo,
                    &tokens[i], max_tokens_touse - i,
                    INLINEMODE_GREEDY,
                    &_innerparsefail,
                    &_inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    )) {
                if (_inneroutofmemory) {
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
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
                            tokenstreaminfo, tokens, i),
                        expr->line, expr->column
                    );
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(
                                tokenstreaminfo, tokens, i),
                            _refcol(
                                tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                }
                ast_FreeExpression(expr);
                return 0;
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
        expr->funcdef.scope.parentscope = addtoscope;

        int i = 1;
        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier to name function "
                "instead",
                _reftokname(tokenstreaminfo, tokens, i)
            );
            if (parsefail) *parsefail = 1;
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->funcdef.identifier = strdup(tokens[i].str_value);
        i++;
        if (!expr->funcdef.identifier) {
            if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->funcdef.scope.parentscope = addtoscope;
        if (i < max_tokens_touse &&
                tokens[i].type == H64TK_BRACKET &&
                tokens[i].char_value == '(') {
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            if (!ast_ParseFuncDefArgs(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    &innerparsefail, &inneroom,
                    &expr->funcdef.arguments, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                } else if (innerparsefail) {
                    if (outofmemory) *outofmemory = 0;
                    if (parsefail) *parsefail = 1;
                    ast_FreeExpression(expr);
                    return 0;
                }
                char buf[256];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected function argument list for function "
                    "definition starting in line %" PRId64
                    ", column %" PRId64,
                    _reftokname(tokenstreaminfo, tokens, i),
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                );
                if (outofmemory) *outofmemory = 0;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
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
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
            );
            if (outofmemory) *outofmemory = 0;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        if (!ast_ParseCodeBlock(
                fileuri, resultmsg, &expr->funcdef.scope,
                tokenstreaminfo, tokens + i, max_tokens_touse - i,
                statementmode,
                &expr->funcdef.stmt, &expr->funcdef.stmt_count,
                &innerparsefail, &inneroom, &tlen, nestingdepth
                )) {
            if (inneroom) {
                if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 0;
                ast_FreeExpression(expr);
                return 0;
            }
            if (outofmemory) *outofmemory = 0;
            if (!innerparsefail && !result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, "internal error: failed to "
                    "get code block somehow", fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            if (parsefail) *parsefail = 1;
            ast_FreeExpression(expr);
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
                "this is not valid outside of functions"
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_CLASSDEF_STMT;
        expr->classdef.scope.parentscope = addtoscope;
        i++;

        if (i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier for class name",
                _describetoken(
                    describebuf,
                    tokenstreaminfo, tokens, i
                )
            );
            if (parsefail) *parsefail = 1;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->classdef.name = strdup(tokens[i].str_value);
        if (!expr->classdef.name) {
            if (parsefail) *parsefail = 0;
            if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        i++;

        if (i < max_tokens_touse &&
                tokens[0].type == H64TK_KEYWORD &&
                strcmp(tokens[0].str_value, "extends") == 0) {
            i++;
            int tlen = 0;
            int innerparsefail = 0;
            int inneroutofmemory = 0;
            h64expression *innerexpr = NULL;
            if (!ast_ParseExprInline(
                    fileuri, resultmsg,
                    &expr->classdef.scope,
                    tokenstreaminfo, &tokens[i], max_tokens_touse - i,
                    INLINEMODE_GREEDY,
                    &innerparsefail,
                    &inneroutofmemory, &innerexpr,
                    &tlen, nestingdepth
                    ) || !ast_CanBeClassRef(innerexpr)) {
                if (innerexpr)
                    ast_FreeExpression(innerexpr);
                if (inneroutofmemory) {
                    if (parsefail) *parsefail = 0;
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                }
                if (innerparsefail) {
                    if (parsefail) *parsefail = 1;
                    if (outofmemory) *outofmemory = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected reference to base class "
                    "to extend from",
                    _describetoken(
                        describebuf,
                        tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            expr->classdef.baseclass_ref = innerexpr;
            i += tlen;
        }

        // Extract class contents:
        int stmt_count = 0;
        h64expression **stmt = NULL;
        int tlen = 0;
        int innerparsefail = 0;
        int inneroom = 0;
        if (!ast_ParseCodeBlock(
                fileuri, resultmsg, addtoscope,
                tokenstreaminfo, tokens + i, max_tokens_touse - i,
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
                        resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
            }
            classparsefail: ;
            int k = 0;
            while (k < stmt_count) {
                ast_FreeExpression(stmt[k]);
                k++;
            }
            free(stmt);
            ast_FreeExpression(expr);
            return 0;
        }
        i += tlen;

        // Separate actual definition types:
        int vardefcount = 0;
        int funcdefcount = 0;
        int k = 0;
        while (k < stmt_count) {
            assert(stmt[k]->type == H64EXPRTYPE_VARDEF_STMT ||
                    stmt[k]->type == H64EXPRTYPE_FUNCDEF_STMT);
            if (stmt[k]->type == H64EXPRTYPE_VARDEF_STMT)
                vardefcount++;
            else
                funcdefcount++;
            k++;
        }
        if (funcdefcount > 0) {
            expr->classdef.funcdef = malloc(
                sizeof(*expr->classdef.funcdef) *
                funcdefcount
            );
            if (!expr->classdef.funcdef)
                goto classparsefail;
            expr->classdef.funcdef_count = funcdefcount;
            k = 0;
            while (k < stmt_count) {
                if (stmt[k]->type == H64EXPRTYPE_FUNCDEF_STMT) {
                    expr->classdef.funcdef[k] = stmt[k];
                    stmt[k] = NULL;
                }
                k++;
            }
        }
        if (vardefcount > 0) {
            expr->classdef.vardef = malloc(
                sizeof(*expr->classdef.vardef) *
                vardefcount
            );
            if (!expr->classdef.vardef)
                goto classparsefail;
            expr->classdef.vardef_count = vardefcount;
            k = 0;
            while (k < stmt_count) {
                if (stmt[k]->type == H64EXPRTYPE_VARDEF_STMT) {
                    expr->classdef.vardef[k] = stmt[k];
                    stmt[k] = NULL;
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

    // try statements:
    if (tokens[0].type == H64TK_KEYWORD &&
            strcmp(tokens[0].str_value, "try") == 0) {
        int i = 0;
        if (statementmode != STATEMENTMODE_INFUNC &&
                statementmode != STATEMENTMODE_INCLASSFUNC) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"try\", "
                "this is only allowed in functions"
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        i++;
        expr->type = H64EXPRTYPE_TRY_STMT;

        // Get code block in try { ... }
        {
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            if (!ast_ParseCodeBlock(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i,
                    max_tokens_touse - i,
                    statementmode,
                    &expr->trystmt.trystmt,
                    &expr->trystmt.trystmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            i += tlen;
        }

        if (i >= max_tokens_touse ||
                tokens[i].type == H64TK_KEYWORD || (
                strcmp(tokens[i].str_value, "catch") != 0 &&
                strcmp(tokens[i].str_value, "finally"))) {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected \"finally\" or \"catch\" for "
                "try statement starting in line %" PRId64
                ", column, %" PRId64,
                _describetoken(
                    describebuf, tokenstreaminfo, tokens, i
                ),
                _refline(tokenstreaminfo, tokens, 0),
                _refcol(tokenstreaminfo, tokens, 0)
            );
            if (parsefail) *parsefail = 1;
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }

        if (strcmp(tokens[i].str_value, "catch") == 0) {
            int catch_i = i;
            i++;
            while (1) {
                int tlen = 0;
                int innerparsefail = 0;
                int inneroutofmemory = 0;
                h64expression *innerexpr = NULL;
                if (!ast_ParseExprInline(
                        fileuri, resultmsg,
                        addtoscope,
                        tokenstreaminfo, &tokens[i],
                        max_tokens_touse - i,
                        INLINEMODE_GREEDY,
                        &innerparsefail,
                        &inneroutofmemory, &innerexpr,
                        &tlen, nestingdepth
                        )) {
                    if (inneroutofmemory) {
                        if (parsefail) *parsefail = 0;
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
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
                                describebuf, tokenstreaminfo, tokens, i
                            ),
                            _refline(tokenstreaminfo, tokens, catch_i),
                            _refcol(tokenstreaminfo, tokens, catch_i)
                        );
                        if (parsefail) *parsefail = 1;
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_FreeExpression(expr);
                    return 0;
                }
                i += tlen;

                h64expression **new_exceptions = realloc(
                    expr->trystmt.exceptions,
                    sizeof(*new_exceptions) *
                    (expr->trystmt.trystmt_count + 1)
                );
                if (!new_exceptions) {
                    if (parsefail) *parsefail = 0;
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(innerexpr);
                    ast_FreeExpression(expr);
                    return 0;
                }
                expr->trystmt.exceptions = new_exceptions;
                expr->trystmt.exceptions[
                    expr->trystmt.trystmt_count
                ] = innerexpr;
                expr->trystmt.trystmt_count++;

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
                    "expected \"as\" or \"{\" for catch clause"
                    "in line %" PRId64
                    ", column, %" PRId64,
                    _describetoken(
                        describebuf, tokenstreaminfo, tokens, i
                    ),
                    _refline(tokenstreaminfo, tokens, catch_i),
                    _refcol(tokenstreaminfo, tokens, catch_i)
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            int named_error = (tokens[i].type == H64TK_KEYWORD);
            i++;
            if (named_error && (i >= max_tokens_touse ||
                    tokens[i].type == H64TK_IDENTIFIER)) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier to name error "
                    "for catch clause"
                    "in line %" PRId64
                    ", column, %" PRId64,
                    _describetoken(
                        describebuf, tokenstreaminfo, tokens, i
                    ),
                    _refline(tokenstreaminfo, tokens, catch_i),
                    _refcol(tokenstreaminfo, tokens, catch_i)
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            if (named_error) {
                expr->trystmt.exception_name = strdup(
                    tokens[i].str_value
                );
                if (!expr->trystmt.exception_name) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                i++;
            }

            // Get code block in catch { ... }
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            if (!ast_ParseCodeBlock(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    statementmode,
                    &expr->trystmt.trystmt, &expr->trystmt.trystmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            i += tlen;
        }

        if (strcmp(tokens[i].str_value, "finally") == 0) {
            i++;

            // Get code block in finally { ... }
            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            if (!ast_ParseCodeBlock(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    statementmode,
                    &expr->trystmt.finallystmt,
                    &expr->trystmt.finallystmt_count,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
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
        int i = 0;
        if (statementmode != STATEMENTMODE_TOPLEVEL) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected \"import\", "
                "this is only allowed outside of functions and classes"
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        i++;

        // Get import path:
        while (1) {
            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_IDENTIFIER) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected identifier for import path",
                    _describetoken(
                        describebuf, tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            char **new_elements = realloc(
                expr->importstmt.import_elements,
                sizeof(expr->importstmt.import_elements_count + 1)
            );
            if (!new_elements) {
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
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
                ast_FreeExpression(expr);
                return 0;
            }
            expr->importstmt.import_elements_count++;
            i++;

            if (i >= max_tokens_touse ||
                    tokens[i].type != H64TK_BINOPSYMBOL ||
                    tokens[i].int_value !=
                    H64OP_MEMBERBYIDENTIFIER)
                break;
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
                        describebuf, tokenstreaminfo, tokens, i
                    )
                );
                if (parsefail) *parsefail = 1;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            expr->importstmt.import_as = strdup(
                tokens[i].str_value
            );
            if (!expr->importstmt.import_as) {
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            i++;
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
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_RETURN_STMT;
        i++;

        int tlen = 0;
        int innerparsefail = 0;
        int inneroutofmemory = 0;
        h64expression *innerexpr = NULL;
        if (!ast_ParseExprInline(
                fileuri, resultmsg,
                addtoscope,
                tokenstreaminfo, &tokens[i], max_tokens_touse - i,
                INLINEMODE_GREEDY,
                &innerparsefail,
                &inneroutofmemory, &innerexpr,
                &tlen, nestingdepth
                )) {
            if (inneroutofmemory) {
                if (parsefail) *parsefail = 0;
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            if (innerparsefail) {
                if (parsefail) *parsefail = 1;
                if (outofmemory) *outofmemory = 0;
                ast_FreeExpression(expr);
                return 0;
            }
            innerexpr = NULL;
            tlen = 0;
        }
        i += tlen;

        expr->returnstmt.returned_expression = expr;
        *out_expr = expr;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        if (out_tokenlen) *out_tokenlen = i;
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
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }

        int firstentry = 1;
        while (1) {
            struct h64ifstmt *current_clause = NULL;
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
                expr->whilestmt.scope.parentscope = addtoscope;
            } else if (strcmp(tokens[i].str_value, "for") == 0) {
                expr->type = H64EXPRTYPE_FOR_STMT;
                stmt_name = __nm_for;
                expr->forstmt.scope.parentscope = addtoscope;
            } else {
                in_elseif = 0;
                in_else = 0;
                if (firstentry) {
                    assert(strcmp(tokens[i].str_value, "if") == 0);
                    expr->type = H64EXPRTYPE_IF_STMT;
                    expr->ifstmt.scope.parentscope = addtoscope;
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
            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                if (i >= max_tokens_touse ||
                        tokens[i].type != H64TK_IDENTIFIER) {
                    char buf[256]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected identifier for iterator "
                        "of \"%s\" statement",
                        _describetoken(
                            describebuf, tokenstreaminfo, tokens, i
                        ),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                }
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
                            describebuf, tokenstreaminfo, tokens, i
                        ),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
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
                if (!ast_ParseExprInline(
                        fileuri, resultmsg,
                        addtoscope,
                        tokenstreaminfo, &tokens[i],
                        max_tokens_touse - i,
                        INLINEMODE_GREEDY,
                        &_innerparsefail,
                        &_inneroutofmemory, &innerexpr,
                        &tlen, nestingdepth
                        )) {
                    if (_inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    if (_innerparsefail) {
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    char buf[256]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected valid inline expression for "
                        "%s of \"%s\" statement",
                        _describetoken(
                            describebuf, tokenstreaminfo, tokens, i
                        ),
                        (expr->type == H64EXPRTYPE_FOR_STMT ?
                         "iterated container" : "conditional"),
                        stmt_name
                    );
                    if (parsefail) *parsefail = 1;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                }
            }
            if (expr->type == H64EXPRTYPE_FOR_STMT) {
                expr->forstmt.iterator_identifier = strdup(iteratorname);
                if (!expr->forstmt.iterator_identifier) {
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                }
                expr->forstmt.iterated_container = innerexpr;
            } else if (expr->type == H64EXPRTYPE_IF_STMT) {
                if (in_elseif || in_else) {
                    struct h64ifstmt *new_current_clause = malloc(
                        sizeof(*new_current_clause)
                    );
                    if (!current_clause) {
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
                        ast_FreeExpression(innerexpr);
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

            int tlen = 0;
            int innerparsefail = 0;
            int inneroom = 0;
            if (!ast_ParseCodeBlock(
                    fileuri, resultmsg, addtoscope,
                    tokenstreaminfo, tokens + i, max_tokens_touse - i,
                    statementmode,
                    stmt_ptr, stmt_count_ptr,
                    &innerparsefail, &inneroom, &tlen, nestingdepth
                    )) {
                if (inneroom) {
                    if (outofmemory) *outofmemory = 1;
                    if (parsefail) *parsefail = 0;
                    ast_FreeExpression(expr);
                    return 0;
                }
                if (outofmemory) *outofmemory = 0;
                if (!innerparsefail && !result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, "internal error: failed to "
                        "get code block somehow", fileuri,
                        _refline(tokenstreaminfo, tokens, i),
                        _refcol(tokenstreaminfo, tokens, i)
                        ))
                    if (outofmemory) *outofmemory = 1;
                if (parsefail) *parsefail = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            i += tlen;

            // Check continuation with further clauses::
            if (expr->type == H64EXPRTYPE_IF_STMT &&
                    i < max_tokens_touse &&
                    tokens[i].type == H64TK_KEYWORD &&
                    (strcmp(tokens[i].str_value, "elseif") == 0 ||
                     (strcmp(tokens[i].str_value, "else") == 0 &&
                      !in_else))
                    ) {
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
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected statement starting with identifier, "
                "this is not valid outside of functions"
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokenstreaminfo, tokens, i),
                    _refcol(tokenstreaminfo, tokens, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        expr->type = H64EXPRTYPE_ASSIGN_STMT;
        int tlen = 0;
        int _innerparsefail = 0;
        int _inneroutofmemory = 0;
        h64expression *innerexpr = NULL;
        if (!ast_ParseExprInline(
                fileuri, resultmsg,
                addtoscope,
                tokenstreaminfo, &tokens[i], max_tokens_touse - i,
                INLINEMODE_GREEDY,
                &_innerparsefail,
                &_inneroutofmemory, &innerexpr,
                &tlen, nestingdepth
                )) {
            if (_inneroutofmemory) {
                if (outofmemory) *outofmemory = 1;
                ast_FreeExpression(expr);
                return 0;
            }
            if (_innerparsefail) {
                ast_FreeExpression(expr);
                return 0;
            }
        } else {
            assert(tlen > 0 && innerexpr != NULL);
            i += tlen;
            #ifdef H64AST_DEBUG
            char dbuf[64];
            char *lhandside = ast_ExpressionToJSONStr(innerexpr, NULL);
            printf("horsecc: debug: checking statement with lvalue %s, "
                   "we're at token %d -> %s\n",
                   lhandside, i,
                   _describetoken(dbuf,
                        tokenstreaminfo, tokens, i));
            if (lhandside) free(lhandside);
            #endif

            if (i < max_tokens_touse &&
                    tokens[i].type == H64TK_BINOPSYMBOL &&
                    IS_ASSIGN_OP(tokens[i].int_value)) {
                if (ast_CanBeLValue(innerexpr)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected term at left hand of assignment, "
                        "expected a valid lvalue "
                        "instead"
                    );
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokenstreaminfo, tokens, i),
                            _refcol(tokenstreaminfo, tokens, i)
                            ))
                        if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(innerexpr);
                    ast_FreeExpression(expr);
                    return 0;
                }
                i++;
                int tlen = 0;
                int _innerparsefail = 0;
                int _inneroutofmemory = 0;
                h64expression *innerexpr2 = NULL;
                if (i >= max_tokens_touse ||
                        !ast_ParseExprInline(
                            fileuri, resultmsg,
                            addtoscope, tokenstreaminfo,
                            &tokens[i], max_tokens_touse - i,
                            INLINEMODE_GREEDY,
                             &_innerparsefail,
                            &_inneroutofmemory, &innerexpr2,
                            &tlen, nestingdepth
                        )) {
                    if (_inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    if (_innerparsefail) {
                        char buf[512]; char describebuf[64];
                        snprintf(buf, sizeof(buf) - 1,
                            "unexpected %s, "
                            "expected inline value assigned to "
                            "assign statement starting in line %"
                            PRId64 ", column %" PRId64 " instead",
                            _describetoken(
                                describebuf,
                                tokenstreaminfo, tokens, i
                            ),
                            expr->line, expr->column
                        );
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokenstreaminfo, tokens, i),
                                _refcol(tokenstreaminfo, tokens, i)
                                ))
                            if (outofmemory) *outofmemory = 1;
                    }
                    ast_FreeExpression(innerexpr);
                    ast_FreeExpression(expr);
                    return 0;
                }
                i += tlen;
                expr->assignstmt.lvalue = innerexpr;
                expr->assignstmt.rvalue = innerexpr2;
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
            ast_FreeExpression(innerexpr);
            innerexpr = NULL;
        }
        expr->type = H64EXPRTYPE_INVALID;  // no assign statement here, continue
    }
    if (parsefail) *parsefail = 0;
    ast_FreeExpression(expr);
    return 0;
}

h64ast ast_ParseFromTokens(
        const char *fileuri,
        h64token *tokens, int token_count
        ) {
    h64ast result;
    memset(&result, 0, sizeof(result));
    result.resultmsg.success = 1;

    tsinfo tokenstreaminfo;
    memset(&tokenstreaminfo, 0, sizeof(tokenstreaminfo));
    tokenstreaminfo.token = tokens;
    tokenstreaminfo.token_count = token_count;

    int i = 0;
    while (i < token_count) {
        h64expression *expr = NULL;
        int tlen = 0;
        int parsefail = 0;
        int oom = 0;
        if (!ast_ParseExprStmt(
                fileuri, &result.resultmsg,
                &result.scope,
                &tokenstreaminfo,
                &tokens[i], token_count - i,
                STATEMENTMODE_TOPLEVEL,
                &parsefail, &oom, &expr, &tlen,
                0
                )) {
            if (oom) {
                result_ErrorNoLoc(
                    &result.resultmsg,
                    "out of memory / alloc fail",
                    fileuri
                );
                ast_FreeContents(&result);
                result.resultmsg.success = 0;
                return result;
            }
            if (!parsefail) {
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected any recognized top level statement",
                    _describetoken(describebuf,
                        &tokenstreaminfo, tokens, i)
                );
                if (!result_AddMessage(
                        &result.resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(&tokenstreaminfo, tokens, i),
                        _refcol(&tokenstreaminfo, tokens, i)
                        ))
                    // OOM on final error msg? Not much we can do...
                    break;
                int previ = i;
                i++;
                ast_ParseRecover_FindNextStatement(
                    &tokenstreaminfo, tokens, token_count, &i
                );
                assert(i > previ || i >= token_count);
                continue;
            }
            result.resultmsg.success = 0;
            return result;
        }
        h64expression **new_stmt = realloc(
            result.stmt, sizeof(*new_stmt) * (result.stmt_count + 1)
        );
        if (!new_stmt) {
            ast_FreeExpression(expr);
            result_ErrorNoLoc(
                &result.resultmsg,
                "out of memory / alloc fail",
                fileuri
            );
            ast_FreeContents(&result);
            result.resultmsg.success = 0;
            return result;
        }
        result.stmt = new_stmt;
        result.stmt[result.stmt_count] = expr;
        result.stmt_count++;
        assert(tlen > 0);
        i += tlen;
    }

    return result;
}

void ast_FreeContents(h64ast *ast) {
    if (!ast)
        return;
    int i = 0;
    while (i < ast->stmt_count) {
        ast_FreeExpression(ast->stmt[i]);
        i++;
    }
    ast->stmt_count = 0;
    free(ast->stmt);
    ast->stmt = NULL;
}
