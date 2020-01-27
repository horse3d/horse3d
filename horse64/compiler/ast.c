
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "compiler/ast.h"
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
    if (token[i].type == H64TK_BINOPSYMBOL ||
            token[i].type == H64TK_BRACKET) {
        snprintf(
            buf, maxlen - 1,
            "'%c'", token[i].char_value
        );
    }
    buf[maxlen - 1] = '\0';
    return buf;
}

void ast_ParseRecoverInnerStatementBlock(
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

#define INLINEMODE_NONGREEDY 0
#define INLINEMODE_GREEDY 1

typedef struct h64inlinepart {
    h64expression *part;
    h64token *operator_after;
} h64inlinepart;

typedef struct h64opsortinfo {
    h64token *op;
    int precedence;
    int inlinepartindex, tokenindex;
} h64opsortinfo;

typedef struct h64returnedopinfo {
    h64expression *opexpression;
    int tokenindexstart, tokenindexend;
} h64returnedopinfo;

static void insert_op_precedence(
        h64token *op, int oppartindex, int tokenindex,
        h64opsortinfo *precedencelist,
        int *precedencelist_count
        ) {
    // FIXME: use binary search here for long expressions
    int insert_precedence = operator_PrecedenceByType(
        op->int_value
    );
    int count = *precedencelist_count;
    int i = 0;
    while (i < count) {
        int precedence = precedencelist[i].precedence;
        if (insert_precedence > precedence) {
            memmove(
                &precedencelist[i],
                &precedencelist[i + 1],
                sizeof(*precedencelist) * (count - i)
            );
            precedencelist[i].op = op;
            precedencelist[i].tokenindex = tokenindex;
            precedencelist[i].inlinepartindex = oppartindex;
            precedencelist[i].precedence = insert_precedence;
            *precedencelist_count = count + 1;
            return;
        }
        i++;
    }
    precedencelist[count].op = op;
    precedencelist[count].tokenindex = tokenindex;
    precedencelist[count].inlinepartindex = oppartindex;
    precedencelist[count].precedence = insert_precedence;
    *precedencelist_count = count + 1;
}

int ast_ParseExprInlineOperator_Recurse(
        const char *fileuri,
        h64result *resultmsg,
        h64scope *addtoscope,
        h64token *tokens,
        int token_count,
        int max_tokens_touse,
        h64expression *lefthandside,
        int lefthandsidetokenlen,
        int i,
        int precedencelevel,
        int *parsefail,
        int *outofmemory,
        h64expression **out_expr,
        int *out_tokenlen
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (token_count < 0 || max_tokens_touse < 0 ||
            i < 0 || i >= token_count ||
            i >= max_tokens_touse) {
        if (parsefail) *parsefail = 0;
        // XXX: do NOT free lefthandside if no parse error.
        return 0;
    }

    // Parse left-hand side if we don't have any yet:
    if (i == 0 && tokens[i].type != H64TK_UNOPSYMBOL) {
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
                &innerexpr, &tlen)) {
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
        if (lefthandside) ast_FreeExpression(lefthandside);
        lefthandside = innerexpr;
        lefthandsidetokenlen = tlen;
    }

    // Deal with operators we encounter:
    while (i < token_count && i < max_tokens_touse) {
        if (tokens[i].type == H64TK_BINOPSYMBOL ||
                tokens[i].type == H64TK_UNOPSYMBOL) {
            int precedence = operator_PrecedenceByType(
                tokens[i].int_value
            );

            // Hand off different precedence levels:
            if (precedence < precedencelevel) {
                assert(precedence >= 0);
                int inneroom = 0;
                int innerparsefail = 0;
                int tlen = 0;
                h64expression *innerexpr = NULL;
                if (!ast_ParseExprInlineOperator_Recurse(
                        fileuri, resultmsg,
                        addtoscope,
                        tokens, token_count, max_tokens_touse,
                        lefthandside, lefthandsidetokenlen,
                        i, precedencelevel - 1,
                        &innerparsefail, &inneroom,
                        &innerexpr, &tlen)) {
                    if (inneroom) {
                        if (outofmemory) *outofmemory = 1;
                        return 0;
                    } else if (innerparsefail) {
                        if (outofmemory) *outofmemory = 0;
                        if (parsefail) *parsefail = 1;
                        return 0;
                    }
                    break;
                }
                assert(innerexpr != NULL);
                lefthandside = innerexpr; 
                // XXX: don't free previous "lefthandside", it's reparented.
                lefthandsidetokenlen = tlen;
            } else if (precedence > precedencelevel) {
                // Needs to be handled by parent call, we're done here.
                break;
            }

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
                return 0;
            }
            if (tokens[i].type == H64TK_BINOPSYMBOL && (
                    i == 0 || lefthandside == NULL
                    )) {
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
                if (lefthandside) ast_FreeExpression(lefthandside);
                return 0;
            }

            // Parse right-hand side:
            h64expression *righthandside = NULL;
            int righthandsidelen = 0;
            {
                int inneroom = 0;
                int innerparsefail = 0;
                if (!ast_ParseExprInline(
                        fileuri, resultmsg,
                        addtoscope,
                        tokens, token_count, max_tokens_touse,
                        INLINEMODE_NONGREEDY,
                        &innerparsefail, &inneroom,
                        &righthandside, &righthandsidelen)) {
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
            }
            assert(righthandside != NULL && righthandsidelen > 0);

            h64expression *opexpr = malloc(sizeof(*opexpr));
            if (!opexpr) {
                if (outofmemory) *outofmemory = 1;
                if (lefthandside) ast_FreeExpression(lefthandside);
                return 0;
            }
            memset(opexpr, 0, sizeof(*opexpr));
            opexpr->op.optype = tokens[i].int_value;
            if (tokens[i].type == H64TK_UNOPSYMBOL) {
                assert(lefthandside == NULL);
                opexpr->op.value1 = righthandside;
            } else {
                opexpr->op.value1 = lefthandside;
                opexpr->op.value2 = righthandside;
            }
            lefthandside = opexpr;
            lefthandsidetokenlen = i;
        } else {
            break;
        }
        i++;
    }
    if (lefthandside) {
        *out_expr = lefthandside;
        *out_tokenlen = lefthandsidetokenlen;
        if (outofmemory) *outofmemory = 0;
        if (parsefail) *parsefail = 0;
        return 1;
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
        int *out_tokenlen
        ) {
    return ast_ParseExprInlineOperator_Recurse(
        fileuri, resultmsg, addtoscope, tokens,
        token_count, max_tokens_touse,
        NULL, 0, 0, operator_precedences_total_count - 1,
        parsefail, outofmemory,
        out_expr, out_tokenlen
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
        int *out_tokenlen
        ) {
    if (outofmemory) *outofmemory = 0;
    if (parsefail) *parsefail = 1;
    if (token_count < 0 || max_tokens_touse < 0) {
        if (parsefail) *parsefail = 0;
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
                tokens[0].type == H64TK_CONSTANT_NULL ||
                tokens[0].type == H64TK_CONSTANT_STRING) {
            expr->type = H64EXPRTYPE_LITERAL;
            *out_expr = expr;
            if (out_tokenlen) *out_tokenlen = 1;
            if (parsefail) *parsefail = 0;
            return 1;
        }

        if (parsefail) *parsefail = 0;
        ast_FreeExpression(expr);
        return 0;
    }

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
                &innerexpr, &tlen
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
                &innerexpr, &tlen
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
        int *out_tokenlen
        ) {
    if (outofmemory) *outofmemory = 1;
    if (parsefail) *parsefail = 1;
    if (token_count < 0 || max_tokens_touse < 0) {
        if (parsefail) *parsefail = 0;
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
                    &tlen)) {
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
                "expected '{' for "
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
                        &tlen)) {
                    if (_inneroutofmemory) {
                        if (outofmemory) *outofmemory = 1;
                        ast_FreeExpression(expr);
                        return 0;
                    }
                    if (_innerparsefail) {
                        // Try to recover by finding next obvious
                        // statement, or possible function end:
                        ast_ParseRecoverInnerStatementBlock(
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
                    "expected '}' to end "
                    "code block opened with '{' in line %"
                    PRId64 ", column %" PRId64 " instead",
                    _describetoken(describebuf, tokens, token_count, i),
                    codeblock_line, codeblock_column
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
                &tlen)) {
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
                            &tlen
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

void ast_FreeExpression(h64expression *expr) {
    if (!expr)
        return;

    if (expr->type == H64EXPRTYPE_VARDEF_STMT) {
        if (expr->vardef.identifier)
            free(expr->vardef.identifier);
        if (expr->vardef.value)
            ast_FreeExpression(expr->vardef.value);
    } else if (expr->type == H64EXPRTYPE_FUNCDEF_STMT) {
        if (expr->funcdef.identifier)
            free(expr->funcdef.identifier);
        int i = 0;
        while (i < expr->funcdef.stmt_count) {
            ast_FreeExpression(expr->funcdef.stmt[i]);
            i++;
        }
        i = 0;
        while (i < expr->funcdef.arguments.arg_count) {
            if (expr->funcdef.arguments.arg_name[i])
                free(expr->funcdef.arguments.arg_name[i]);
            if (expr->funcdef.arguments.arg_value)
                ast_FreeExpression(expr->funcdef.arguments.arg_value);
            i++;
        }
    } else if (expr->type == H64EXPRTYPE_INVALID) {
        // Nothing to do.
    } else {
        fprintf(stderr, "horsecc: warning: internal issue, "
            "unhandled expression in ast_FreeExpression(): "
            "type=%d\n", expr->type);
    }

    free(expr);
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

static char _h64exprname_invalid[] = "H64EXPRTYPE_INVALID";
static char _h64exprname_vardef_stmt[] = "H64EXPRTYPE_VARDEF_STMT";
static char _h64exprname_funcdef_stmt[] = "H64EXPRTYPE_FUNCDEF_STMT";

const char *ast_ExpressionTypeToStr(h64expressiontype type) {
    if (type == H64EXPRTYPE_INVALID || type == 0) {
        return _h64exprname_invalid;
    } else if (type == H64EXPRTYPE_VARDEF_STMT) {
        return _h64exprname_vardef_stmt;
    } else if (type == H64EXPRTYPE_FUNCDEF_STMT) {
        return _h64exprname_funcdef_stmt;
    }
    return NULL;
}

char *ast_ExpressionToJSONStr(
        h64expression *e, const char *fileuri
        ) {
    jsonvalue *v = ast_ExpressionToJSON(e, fileuri);
    if (!v)
        return NULL;
    char *s = json_Dump(v);
    json_Free(v);
    return s;
}

jsonvalue *ast_ExpressionToJSON(
        h64expression *e, const char *fileuri
        ) {
    if (!e)
        return NULL;
    int fail = 0;
    jsonvalue *v = json_Dict();
    char *typestr = strdup(ast_ExpressionTypeToStr(e->type));
    if (typestr) {
        if (!json_SetDictStr(v, "type", typestr)) {
            fail = 1;
        }
        free(typestr);
    } else {
        fprintf(stderr, "horsecc: error: internal error, "
            "fail of handling expression type %d in "
            "ast_ExpressionTypeToStr\n",
            e->type);
        fail = 1;
    }
    if (e->line >= 0) {
        if (!json_SetDictInt(v, "line", e->line)) {
            fail = 1;
        } else if (e->column >= 0) {
            if (!json_SetDictInt(v, "column", e->column)) {
                fail = 1;
            }
        }
    }
    if (e->type == H64EXPRTYPE_VARDEF_STMT) {
        if (e->vardef.identifier &&
                !json_SetDictStr(v, "name", e->vardef.identifier))
            fail = 1;
    } else if (e->type == H64EXPRTYPE_FUNCDEF_STMT) {
        jsonvalue *attributes = json_List();
        if (e->funcdef.identifier &&
                !json_SetDictStr(v, "name", e->funcdef.identifier))
            fail = 1;
        jsonvalue *statements = json_List();
        int i = 0;
        while (i < e->funcdef.stmt_count) {
            jsonvalue *stmtjson = ast_ExpressionToJSON(
                e->funcdef.stmt[i], fileuri
            );
            if (!json_AddToList(statements, stmtjson)) {
                fail = 1;
                break;
            }
            i++;
        }
        if (!json_SetDict(v, "statements", statements)) {
            fail = 1;
            json_Free(statements);
        }
        if (e->funcdef.is_threadable) {
            if (!json_AddToListStr(attributes, "threadable"))
                fail = 1;
        }
        if (!json_SetDict(v, "attributes", attributes)) {
            fail = 1;
            json_Free(attributes);
        }
    }
    if (fileuri) {
        if (!json_SetDictStr(v, "file-uri", fileuri))
            fail = 1;
    }
    if (fail) {
        json_Free(v);
        return NULL;
    }
    return v;
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
                &parsefail, &oom, &expr, &tlen
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
