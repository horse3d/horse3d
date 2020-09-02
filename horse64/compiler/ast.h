// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_AST_H_
#define HORSE64_COMPILER_AST_H_

#include "bytecode.h"
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
    H64EXPRTYPE_DO_STMT,
    H64EXPRTYPE_WITH_STMT,
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
    H64EXPRTYPE_VECTOR,
    H64EXPRTYPE_WITH_CLAUSE
} h64expressiontype;

#define IS_STMT(x) (x <= H64EXPRTYPE_ASSIGN_STMT)

typedef struct h64funcstorageextrainfo h64funcstorageextrainfo;

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
typedef struct h64scopedef h64scopedef;

typedef struct h64funcargs {
    int arg_count;
    int last_posarg_is_multiarg;
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

typedef struct h64scopedef h64scopedef;
typedef struct h64ast h64ast;

#define KNOWNVALUETYPE_KNOWNINT 2
#define KNOWNVALUETYPE_KNOWNFLOAT 3
#define KNOWNVALUETYPE_KNOWNSTR 4

typedef struct h64expression {
    uint8_t destroyed;
    int64_t line, column;
    int tokenindex;
    h64expression *parent;
    h64expressiontype type;
    struct storage {
        int set;
        storageref ref;
        int eval_temp_id;
    } storage;
    struct knownvalue {
        int type;
        union {
            int64_t knownint;
            double knownfloat;
            char *knownstr;
        };
    } knownvalue;
    union {
        struct vardef {
            int is_deprecated;
            char *identifier;
            int is_const;
            h64expression *value;
            h64scope *foundinscope;
        } vardef;
        struct funcdef {
            char *name;
            uint8_t is_deprecated;
            uint8_t is_canasync, is_noasync;
            uint8_t is_getter;
            uint8_t is_setter;
            int stmt_count;
            h64expression **stmt;
            h64scope scope;
            h64scope *foundinscope;
            h64funcargs arguments;
            int64_t bytecode_func_id;

            h64funcstorageextrainfo *_storageinfo;
        } funcdef;
        struct withstmt {
            h64scope scope;
            int withclause_count;
            h64expression **withclause;

            int stmt_count;
            h64expression **stmt;
        } withstmt;
        struct withclause {
            h64scope *foundinscope;
            h64expression *withitem_value;
            char *withitem_identifier;
        } withclause;
        struct assignstmt {
            h64expression *lvalue;
            h64expression *rvalue;
            int assignop;
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
            int str_value_len;
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
            h64scopedef *resolved_to_def;
            h64expression *resolved_to_expr;
            int resolved_to_builtin;
            char *value;
        } identifierref;
        struct importstmt {
            int import_elements_count;
            char **import_elements;
            char *source_library;
            char *import_as;
            h64ast *referenced_ast;
            h64scope *foundinscope;
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
            uint8_t is_canasync, is_noasync;
            uint8_t is_deprecated;
            h64scope scope;
            char *name;
            h64expression *baseclass_ref;

            int vardef_count;
            h64expression **vardef;
            int funcdef_count;
            h64expression **funcdef;
            int64_t bytecode_class_id;

            h64scope *foundinscope;
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
        struct dostmt {
            int dostmt_count;
            h64expression **dostmt;
            h64scope doscope;

            int errors_count;
            h64expression **errors;
            char *error_name;
            int rescuestmt_count;
            h64expression **rescuestmt;
            h64scope rescuescope;

            int has_finally_block;
            int finallystmt_count;
            h64expression **finallystmt;
            h64scope finallyscope;
        } dostmt;
        struct inlinenew {
            h64expression *value;
        } inlinenew;
    }; 
} h64expression;

void ast_FreeExpression(h64expression *expr);

void ast_MarkExprDestroyed(h64expression *expr);

h64scope *ast_GetScope(h64expression *expr, h64scope *global_scope);

const char *ast_ExpressionTypeToStr(h64expressiontype type);

char *ast_ExpressionToJSONStr(
    h64expression *e, const char *fileuri
);

jsonvalue *ast_ExpressionToJSON(
    h64expression *e, const char *fileuri
);

void ast_ClearFunctionArgs(
    h64funcargs *fargs, h64expression *func, int freeargsexprs
);

void ast_ClearFunctionArgsWithoutFunc(
    h64funcargs *fargs, h64scope *scope, int freeargsexprs
);

void ast_FreeExprNonpoolMembers(
    h64expression *expr
);

int ast_VisitExpression(
    h64expression *expr, h64expression *parent,
    int (*visit_in)(
        h64expression *expr, h64expression *parent, void *ud),
    int (*visit_out)(
        h64expression *expr, h64expression *parent, void *ud),
    int (*cancel_visit_descend_callback)(
        h64expression *expr, void *ud
    ),
    void *ud
);

#endif  // HORSE64_COMPILER_AST_H_
