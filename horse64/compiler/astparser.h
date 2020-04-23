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
);

h64ast ast_ParseFromTokens(
    const char *fileuri, h64token *tokens, int token_count
);

int ast_CanBeLValue(h64expression *e);

#endif  // HORSE64_COMPILER_ASTPARSER_H_
