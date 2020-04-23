#ifndef HORSE64_COMPILER_AST_H_
#define HORSE64_COMPILER_AST_H_

#include "compiler/lexer.h"
#include "compiler/operator.h"


typedef enum h64expressiontype {
    H64EXPRTYPE_INVALID = 0,
    H64EXPRTYPE_VARDEF_STMT = 1,
    H64EXPRTYPE_FUNCDEF_STMT,
    H64EXPRTYPE_CALL_STMT,
    H64EXPRTYPE_CLASSDEF_STMT,
    H64EXPRTYPE_IF_STMT,
    H64EXPRTYPE_WHILE_STMT,
    H64EXPRTYPE_FOR_STMT,
    H64EXPRTYPE_IMPORT_STMT,
    H64EXPRTYPE_ASSIGN_STMT,
    H64EXPRTYPE_LITERAL = 50,
    H64EXPRTYPE_IDENTIFIERREF,
    H64EXPRTYPE_UNARYOP,
    H64EXPRTYPE_BINARYOP,
    H64EXPRTYPE_CALL,
    H64EXPRTYPE_LIST,
    H64EXPRTYPE_SET,
    H64EXPRTYPE_DICT
} h64expressiontype;

#define IS_STMT(x) (x < 50)

typedef enum h64literaltype {
    H64LITERAL_INVALID = 0,
    H64LITERAL_INTEGER = 1,
    H64LITERAL_FLOAT,
    H64LITERAL_STRING,
    H64LITERAL_BYTES,
    H64LITERAL_BOOLEAN
} h64literaltype;

typedef struct h64expression h64expression;
typedef struct h64scope h64scope;

typedef struct h64scope {
    int declarationref_count;
    h64expression *declarationref;
    h64scope *parentscope;
} h64scope;

typedef struct h64funcargs {
    int arg_count;
    char **arg_name;
    h64expression *arg_value;
} h64funcargs;

typedef struct h64expression {
    int64_t line, column;
    int scopeline;
    h64expressiontype type;
    union {
        struct vardef {
            char *identifier;
            int is_const;
            h64expression *value;
        } vardef;
        struct funcdef {
            char *identifier;
            int is_threadable;
            int stmt_count;
            h64expression **stmt;
            h64scope scope;
            h64funcargs arguments;
        } funcdef;
        struct assignstmt {
            h64expression *lvalue;
            h64expression *rvalue;
            char assignop[3];
        } assignstmt;
        struct callstmt {
            h64expression *inlinecall;
        } callstmt;
        struct op {
            h64optype optype;
            h64expression *value1, *value2;
        } op;
        struct inlinecall {
            h64expression *value;
            h64funcargs arguments;
        } inlinecall;
    }; 
} h64expression;

void ast_FreeExpression(h64expression *expr);

const char *ast_ExpressionTypeToStr(h64expressiontype type);

char *ast_ExpressionToJSONStr(
    h64expression *e, const char *fileuri
);

jsonvalue *ast_ExpressionToJSON(
    h64expression *e, const char *fileuri
);


#endif  // HORSE64_COMPILER_AST_H_
