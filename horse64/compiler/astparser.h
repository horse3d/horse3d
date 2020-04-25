#ifndef HORSE64_COMPILER_ASTPARSER_H_
#define HORSE64_COMPILER_ASTPARSER_H_

#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"


typedef struct h64ast {
    h64result resultmsg;
    h64scope scope;
    int stmt_count;
    h64expression **stmt;
} h64ast;

void ast_FreeContents(h64ast *ast);

#define INLINEMODE_NONGREEDY 0
#define INLINEMODE_GREEDY 1

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
);

#define STATEMENTMODE_TOPLEVEL 0
#define STATEMENTMODE_INFUNC 1
#define STATEMENTMODE_INCLASS 2
#define STATEMENTMODE_INCLASSFUNC 3

int ast_ParseExprStmt(
    const char *fileuri,
    h64result *resultmsg,
    h64scope *addtoscope,
    h64token *tokens,
    int token_count,
    int max_tokens_touse,
    int statementmode,
    int *parsefail,
    int *outofmemory,
    h64expression **out_expr,
    int *out_tokenlen,
    int nestingdepth
);

h64ast ast_ParseFromTokens(
    const char *fileuri, h64token *tokens, int token_count
);

int ast_CanBeLValue(h64expression *e);

#endif  // HORSE64_COMPILER_ASTPARSER_H_
