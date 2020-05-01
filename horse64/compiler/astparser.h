#ifndef HORSE64_COMPILER_ASTPARSER_H_
#define HORSE64_COMPILER_ASTPARSER_H_

#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"
#include "compiler/scope.h"


typedef struct h64ast {
    h64result resultmsg;
    h64scope scope;
    int stmt_count;
    h64expression **stmt;
} h64ast;

void ast_FreeContents(h64ast *ast);


typedef struct tsinfo {
    h64token *token;
    int token_count;
} tsinfo;


#define INLINEMODE_NONGREEDY 0
#define INLINEMODE_GREEDY 1

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
);

#define STATEMENTMODE_TOPLEVEL 0
#define STATEMENTMODE_INFUNC 1
#define STATEMENTMODE_INCLASS 2
#define STATEMENTMODE_INCLASSFUNC 3

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
);

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
);

h64ast ast_ParseFromTokens(
    const char *fileuri, h64token *tokens, int token_count
);

int ast_CanBeLValue(h64expression *e);

#endif  // HORSE64_COMPILER_ASTPARSER_H_
