
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// #define H64AST_DEBUG_OPRECURSE

#include "compiler/ast.h"
#include "compiler/astparser.h"
#include "compiler/globallimits.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"

static int64_t _refline(h64token *token, int token_count, int i) {
    if (i > token_count - 1)
        i = token_count - 1;
    return token[i].line;
}

static int64_t _refcol(h64token *token, int token_count, int i) {
    if (i > token_count - 1)
        i = token_count - 1;
    return token[i].column;
}

static char _reftokname_none[] = "end of file";

static const char *_reftokname(h64token *token, int token_count, int i) {
    if (i >= token_count) {
        return _reftokname_none;
    }
    return lexer_TokenTypeToStr(token[i].type);
}

static const char *_describetoken(
        char *buf, h64token *token, int token_count, int i) {
    if (!token || i >= token_count)
        return _reftokname_none;
    int maxlen = 64;
    snprintf(buf, maxlen - 1, "%s", _reftokname(token, token_count, i));
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
        h64token *tokens, int token_count, int max_tokens_touse, int *k
        ) {
    int brackets_depth = 0;
    int i = *k;
    while (i < token_count && i < max_tokens_touse) {
        if (tokens[i].type == H64TK_BRACKET) {
            char c = tokens[i].char_value;
            if (c == '{' || c == '[' || c == '(') {
                brackets_depth++;
            } else {
                brackets_depth--;
                if (brackets_depth < 0) brackets_depth = 0;
                if (brackets_depth == 0 && c == '}') {
                    *k = i;
                    return;
                }
            }
        } else if (tokens[i].type == H64TK_IDENTIFIER) {
            char *s = tokens[i].str_value;
            if (strcmp(s, "if") == 0 ||
                    strcmp(s, "var") == 0 ||
                    strcmp(s, "const") == 0 ||
                    strcmp(s, "for") == 0 ||
                    strcmp(s, "while") == 0) {
                *k = i;
                return;
            }
        }
        i++;
    }
    *k = i;
}

void ast_ParseRecover_FindEndOfBlock(
        h64token *tokens, int token_count, int max_tokens_touse, int *k
        ) {
    int brackets_depth = 0;
    int i = *k;
    while (i < token_count && i < max_tokens_touse) {
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

int ast_ParseExprInlineOperator_Recurse(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        h64token *tokens,
        int token_count,
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
    if (token_count <= 0 || max_tokens_touse <= 0) {
        if (parsefail) *parsefail = 0;
        // XXX: do NOT free lefthandside if no parse error.
        return 0;
    }
    assert(lefthandside || lefthandsidetokenlen == 0);

    #ifdef H64AST_DEBUG_OPRECURSE
    char describebuf[64];
    printf("horsecc: debug: OP PARSE FROM %d, token: %s\n", 0,
         _describetoken(describebuf, tokens, token_count, 0));
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
                tokens, token_count, max_tokens_touse,
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
            _describetoken(describebuf, tokens, token_count, i)
        );
        if (outofmemory) *outofmemory = 0;
        if (!result_AddMessage(
                resultmsg,
                H64MSG_ERROR, buf, fileuri,
                _refline(tokens, token_count, i),
                _refcol(tokens, token_count, i)
                ))
            if (outofmemory) *outofmemory = 1;
        if (parsefail) *parsefail = 1;
        if (original_lefthand && original_lefthand != lefthandside)
            ast_FreeExpression(original_lefthand);
        if (lefthandside) ast_FreeExpression(lefthandside);
        return 0;
    }

    #ifdef H64AST_DEBUG_OPRECURSE
    char describebufy[64];
    char *lhandside = ast_ExpressionToJSONStr(lefthandside, NULL); 
    printf("horsecc: debug: "
         "GOT LEFT HAND SIDE %s AND NOW AT %d %s - "
         "current handling level: %d\n", lhandside, i,
         _describetoken(describebufy, tokens, token_count, i),
         precedencelevel);
    if (lhandside) free(lhandside);
    #endif

    // Deal with operators we encounter:
    while (i < token_count && i < max_tokens_touse) {
        if (tokens[i].type == H64TK_BINOPSYMBOL ||
                tokens[i].type == H64TK_UNOPSYMBOL) {
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
                    _describetoken(describebuf, tokens, token_count, i),
                    _refline(tokens, token_count, 0),
                    _refline(tokens, token_count, 0)
                );
                if (outofmemory) *outofmemory = 0;
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokens, token_count, i),
                        _refcol(tokens, token_count, i)
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
                #ifdef H64AST_DEBUG_OPRECURSE
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
                        tokens + i - skipback,
                        token_count - (i - skipback),
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
                    innerexpr->type == H64EXPRTYPE_UNARYOP);
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
                #ifdef H64AST_DEBUG_OPRECURSE
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
                #ifdef H64AST_DEBUG_OPRECURSE
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

            // Parse right-hand side:
            h64expression *righthandside = NULL;
            int righthandsidelen = 0;
            {
                int inneroom = 0;
                int innerparsefail = 0;
                if (i >= token_count || !ast_ParseExprInline(
                        fileuri, resultmsg,
                        addtoscope,
                        tokens + i, token_count - i, max_tokens_touse - i,
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
                        _describetoken(describebuf, tokens, token_count, i)
                    );
                    if (outofmemory) *outofmemory = 0;
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokens, token_count, i),
                            _refcol(tokens, token_count, i)
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

            #ifdef H64AST_DEBUG_OPRECURSE
            char describebufy2[64];
            char *lhandside2 = ast_ExpressionToJSONStr(lefthandside, NULL);
            printf("horsecc: debug: "
                "GOT NEW LEFT HAND SIDE %s AND NOW AT %d %s - "
                 "current handling level: %d\n", lhandside2, i,
                 _describetoken(describebufy2, tokens, token_count, i),
                 precedencelevel);
            if (lhandside2) free(lhandside2);
            #endif
        } else {
            break;
        }
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
        h64token *tokens,
        int token_count,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    return ast_ParseExprInlineOperator_Recurse(
        fileuri, resultmsg, addtoscope, tokens,
        token_count, max_tokens_touse,
        NULL, 0,
        operator_precedences_total_count - 1,
        parsefail, outofmemory,
        out_expr, out_tokenlen, nestingdepth
    ); 
}

int ast_ParseExprInline(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        h64token *tokens,
        int token_count,
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
    if (token_count < 0 || max_tokens_touse < 0) {
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
        if (tokens[0].type == H64TK_IDENTIFIER) {
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
        }

        if (parsefail) *parsefail = 0;
        ast_FreeExpression(expr);
        return 0;
    }

    #ifdef H64AST_DEBUG_OPRECURSE
    char describebuf[64];
    printf("horsecc: debug: GREEDY PARSE FROM %d %s\n", 0,
         _describetoken(describebuf, tokens, token_count, 0));
    #endif

    // Try to greedily parse as full operator expression:
    {
        int tlen = 0;
        h64expression *innerexpr = NULL;
        int inneroom = 0;
        int innerparsefail = 0;
        if (!ast_ParseExprInlineOperator(
                fileuri, resultmsg, addtoscope,
                tokens, token_count, max_tokens_touse,
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
                tokens, token_count, max_tokens_touse,
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

int ast_ParseExprStmt(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        h64token *tokens,
        int token_count,
        int max_tokens_touse,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen,
        int nestingdepth
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (token_count < 0 || max_tokens_touse < 0) {
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
        if (i >= token_count || i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256];
            char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier to name variable "
                "instead",
                _describetoken(describebuf, tokens, token_count, i)
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokens, token_count, i),
                    _refcol(tokens, token_count, i)
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
        if (i < token_count && i < max_tokens_touse &&
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
                    &tokens[i], token_count - i,
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
                if (!_innerparsefail) {
                    char buf[512]; char describebuf[64];
                    snprintf(buf, sizeof(buf) - 1,
                        "unexpected %s, "
                        "expected inline value assigned to "
                        "variable definition in line %"
                        PRId64 ", column %" PRId64 " instead",
                        _describetoken(describebuf, tokens, token_count, i),
                        expr->line, expr->column
                    );
                    if (!result_AddMessage(
                            resultmsg,
                            H64MSG_ERROR, buf, fileuri,
                            _refline(tokens, token_count, i),
                            _refcol(tokens, token_count, i)
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
        int i = 1;
        if (i >= token_count || i >= max_tokens_touse ||
                tokens[i].type != H64TK_IDENTIFIER) {
            char buf[256];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected identifier to name function "
                "instead",
                _reftokname(tokens, token_count, i)
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokens, token_count, i),
                    _refcol(tokens, token_count, i)
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
        if (i < token_count && i < max_tokens_touse &&
                tokens[i].type == H64TK_BRACKET &&
                tokens[i].char_value == '(') {

        }
        if (i < token_count && i < max_tokens_touse &&
                tokens[i].type == H64TK_KEYWORD &&
                strcmp(tokens[i].str_value, "threadable") == 0) {
            i++;
            expr->funcdef.is_threadable = 1;
        }
        if (i >= token_count || i >= max_tokens_touse ||
                tokens[i].type != H64TK_BRACKET ||
                tokens[i].char_value != '{') {
            char buf[256]; char describebuf[64];
            snprintf(buf, sizeof(buf) - 1,
                "unexpected %s, "
                "expected \"{\" for "
                "code block for "
                "function definition in line %"
                PRId64 ", column %" PRId64 " instead",
                _describetoken(describebuf, tokens, token_count, i),
                expr->line, expr->column
            );
            if (!result_AddMessage(
                    resultmsg,
                    H64MSG_ERROR, buf, fileuri,
                    _refline(tokens, token_count, i),
                    _refcol(tokens, token_count, i)
                    ))
                if (outofmemory) *outofmemory = 1;
            ast_FreeExpression(expr);
            return 0;
        }
        int64_t codeblock_line = tokens[i].line;
        int64_t codeblock_column = tokens[i].column;
        i++;
        while (1) {
            if (i < token_count && i < max_tokens_touse && (
                    tokens[i].type != H64TK_BRACKET ||
                    tokens[i].char_value != '}')) {
                int tlen = 0;
                int _innerparsefail = 0;
                int _inneroutofmemory = 0;
                h64expression *innerexpr = NULL;
                if (!ast_ParseExprStmt(
                        fileuri, resultmsg,
                        &expr->funcdef.scope,
                        &tokens[i], token_count - i,
                        max_tokens_touse - i,
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
                        // Try to recover by finding next obvious
                        // statement, or possible function end:
                        ast_ParseRecover_FindNextStatement(
                            tokens, token_count, max_tokens_touse, &i
                        );
                        continue;
                    }
                } else {
                    assert(innerexpr != NULL);
                    assert(tlen > 0);
                    h64expression **new_stmt = realloc(
                        expr->funcdef.stmt,
                        sizeof(*new_stmt) *
                        (expr->funcdef.stmt_count + 1)
                    );
                    if (!new_stmt) {
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(innerexpr);
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    expr->funcdef.stmt = new_stmt;
                    expr->funcdef.stmt[
                        expr->funcdef.stmt_count
                    ] = innerexpr;
                    expr->funcdef.stmt_count++;
                    i += tlen;
                    continue;
                }
            }
            if (i >= token_count || i >= max_tokens_touse ||
                    tokens[i].type != H64TK_BRACKET ||
                    tokens[i].char_value != '}') {
                char *s = ast_ExpressionToJSONStr(expr, fileuri);
                if (s) free(s);
                char buf[256]; char describebuf[64];
                snprintf(buf, sizeof(buf) - 1,
                    "unexpected %s, "
                    "expected \"}\" to end "
                    "code block opened with \"{\" in line %"
                    PRId64 ", column %" PRId64 " instead",
                    _describetoken(describebuf, tokens, token_count, i),
                    codeblock_line, codeblock_column
                );
                if (!result_AddMessage(
                        resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokens, token_count, i),
                        _refcol(tokens, token_count, i)
                        )) {
                    if (outofmemory) *outofmemory = 1;
                    ast_FreeExpression(expr);
                    return 0;
                } else {
                    ast_ParseRecover_FindEndOfBlock(
                        tokens, token_count, max_tokens_touse, &i
                    );
                }
            }
            i++;
            break;
        }
        *out_expr = expr;
        if (out_tokenlen) *out_tokenlen = i;
        if (parsefail) *parsefail = 0;
        return 1;
    }

    // Assignments and function calls:
    if (tokens[0].type == H64TK_IDENTIFIER &&
            token_count > 1 && max_tokens_touse > 1) {
        int i = 0;
        expr->type = H64EXPRTYPE_ASSIGN_STMT;
        int tlen = 0;
        int _innerparsefail = 0;
        int _inneroutofmemory = 0;
        h64expression *innerexpr = NULL;
        if (!ast_ParseExprInline(
                fileuri, resultmsg,
                addtoscope,
                &tokens[i], token_count - i,
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
        } else {
            assert(tlen > 0 && innerexpr != NULL);
            i += tlen;
            if (i < token_count && i < max_tokens_touse &&
                    tokens[i].type == H64TK_BINOPSYMBOL &&
                    IS_ASSIGN_OP(tokens[i].int_value)) {
                i++;
                int tlen = 0;
                int _innerparsefail = 0;
                int _inneroutofmemory = 0;
                h64expression *innerexpr2 = NULL;
                if (i >= token_count || i >= max_tokens_touse ||
                        !ast_ParseExprInline(
                            fileuri, resultmsg,
                            addtoscope,
                            &tokens[i], token_count - i,
                            max_tokens_touse - i,
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
                                describebuf, tokens, token_count, i
                            ),
                            expr->line, expr->column
                        );
                        if (!result_AddMessage(
                                resultmsg,
                                H64MSG_ERROR, buf, fileuri,
                                _refline(tokens, token_count, i),
                                _refcol(tokens, token_count, i)
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
                expr->callstmt.inlinecall = innerexpr;
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

    int i = 0;
    while (i < token_count) {
        h64expression *expr = NULL;
        int tlen = 0;
        int parsefail = 0;
        int oom = 0;
        if (!ast_ParseExprStmt(
                fileuri, &result.resultmsg,
                &result.scope,
                &tokens[i], token_count - i,
                token_count - i,
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
                    _describetoken(describebuf, tokens, token_count, i)
                );
                if (!result_AddMessage(
                        &result.resultmsg,
                        H64MSG_ERROR, buf, fileuri,
                        _refline(tokens, token_count, i),
                        _refcol(tokens, token_count, i)
                        ))
                    // OOM on final error msg? Not much we can do...
                    break;
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
