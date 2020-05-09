#ifndef HORSE64_COMPILER_AST_H_
#define HORSE64_COMPILER_AST_H_

#include "compiler/lexer.h"
#include "compiler/operator.h"
#include "compiler/scope.h"


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
    H64EXPRTYPE_RETURN_STMT,
    H64EXPRTYPE_TRY_STMT,
    H64EXPRTYPE_ASSIGN_STMT,
    H64EXPRTYPE_LITERAL,
    H64EXPRTYPE_IDENTIFIERREF,
    H64EXPRTYPE_INLINEFUNCDEF,
    H64EXPRTYPE_UNARYOP,
    H64EXPRTYPE_BINARYOP,
    H64EXPRTYPE_CALL,
    H64EXPRTYPE_LIST,
    H64EXPRTYPE_SET,
    H64EXPRTYPE_MAP,
    H64EXPRTYPE_VECTOR
} h64expressiontype;

#define IS_STMT(x) (x <= H64EXPRTYPE_ASSIGN_STMT)

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

typedef struct h64funcargs {
    int arg_count;
    char **arg_name;
    h64expression **arg_value;
} h64funcargs;

struct h64ifstmt;

struct h64ifstmt {
    h64scope scope;
    h64expression *conditional;
    int stmt_count;
    h64expression **stmt;

    struct h64ifstmt *followup_clause;
};

typedef struct h64ast h64ast;

typedef struct h64expression {
    int64_t line, column;
    int tokenindex;
    h64expressiontype type;
    union {
        struct vardef {
            int is_deprecated;
            char *identifier;
            int is_const;
            h64expression *value;
        } vardef;
        struct funcdef {
            char *name;
            int is_deprecated;
            int is_threadable;
            int is_getter;
            int is_setter;
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
            h64expression *call;
        } callstmt;
        struct literal {
            h64tokentype type;
            union {
                int64_t int_value;
                double float_value;
                char *str_value;
            };
        } literal;
        struct op {
            int optokenoffset, totaltokenlen;  // needed by parser
            h64optype optype;
            h64expression *value1, *value2;
        } op;
        struct inlinecall {
            h64expression *value;
            h64funcargs arguments;
        } inlinecall;
        struct identifierref {
            char *value;
        } identifierref;
        struct importstmt {
            int import_elements_count;
            char **import_elements;
            char *source_library;
            char *import_as;
            h64ast *referenced_ast;
        } importstmt;
        struct constructorvector {
            int entry_count;
            h64expression **entry;
        } constructorvector;
        struct constructorlist {
            int entry_count;
            h64expression **entry;
        } constructorlist;
        struct constructorset {
            int entry_count;
            h64expression **entry;
        } constructorset;
        struct constructormap {
            int entry_count;
            h64expression **key;
            h64expression **value;
        } constructormap;
        struct returnstmt {
            h64expression *returned_expression;
        } returnstmt;
        struct classdef {
            int is_threadable;
            int is_deprecated;
            h64scope scope;
            char *name;
            h64expression *baseclass_ref;
            int vardef_count;
            h64expression **vardef;
            int funcdef_count;
            h64expression **funcdef;
        } classdef;
        struct forstmt {
            char *iterator_identifier;
            h64scope scope;
            h64expression *iterated_container;
            int stmt_count;
            h64expression **stmt;
        } forstmt;
        struct whilestmt {
            h64scope scope;
            h64expression *conditional;
            int stmt_count;
            h64expression **stmt;
        } whilestmt;
        struct h64ifstmt ifstmt;
        struct trystmt {
            int trystmt_count;
            h64expression **trystmt;
            h64scope tryscope;

            int exceptions_count;
            h64expression **exceptions;
            char *exception_name;
            int catchstmt_count;
            h64expression **catchstmt;
            h64scope catchscope;

            int finallystmt_count;
            h64expression **finallystmt;
            h64scope finallyscope;
        } trystmt;
        struct inlinenew {
            h64expression *value;
        } inlinenew;
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

void ast_ClearFunctionArgs(h64funcargs *fargs);

int ast_VisitExpression(
    h64expression *expr, h64expression *parent,
    int (*visit_in)(
        h64expression *expr, h64expression *parent, void *ud),
    int (*visit_out)(
        h64expression *expr, h64expression *parent, void *ud),
    void *ud
);

#endif  // HORSE64_COMPILER_AST_H_
