
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
#include "compiler/globallimits.h"
#include "compiler/lexer.h"
#include "compiler/operator.h"


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

static char _h64exprname_invalid[] = "H64EXPRTYPE_INVALID";
static char _h64exprname_vardef_stmt[] = "H64EXPRTYPE_VARDEF_STMT";
static char _h64exprname_funcdef_stmt[] = "H64EXPRTYPE_FUNCDEF_STMT";
static char _h64exprname_call_stmt[] = "H64EXPRTYPE_CALL_STMT";
static char _h64exprname_classdef_stmt[] = "H64EXPRTYPE_CLASSDEF_STMT";
static char _h64exprname_if_stmt[] = "H64EXPRTYPE_IF_STMT";
static char _h64exprname_while_stmt[] = "H64EXPRTYPE_WHILE_STMT";
static char _h64exprname_for_stmt[] = "H64EXPRTYPE_FOR_STMT";
static char _h64exprname_import_stmt[] = "H64EXPRTYPE_IMPORT_STMT";
static char _h64exprname_assign_stmt[] = "H64EXPRTYPE_ASSIGN_STMT";
static char _h64exprname_literal[] = "H64EXPRTYPE_LITERAL";
static char _h64exprname_identifierref[] = "H64EXPRTYPE_IDENTIFERREF";
static char _h64exprname_unaryop[] = "H64EXPRTYPE_UNARYOP";
static char _h64exprname_binaryop[] = "H64EXPRTYPE_BINARYOP";
static char _h64exprname_call[] = "H64EXPRTYPE_CALL";
static char _h64exprname_list[] = "H64EXPRTYPE_LIST";
static char _h64exprname_set[] = "H64EXPRTYPE_SET";
static char _h64exprname_dict[] = "H64EXPRTYPE_DICT";

const char *ast_ExpressionTypeToStr(h64expressiontype type) {
    if (type == H64EXPRTYPE_INVALID || type <= 0)
        return _h64exprname_invalid;
    switch (type) {
    case H64EXPRTYPE_VARDEF_STMT:
        return _h64exprname_vardef_stmt;
    case H64EXPRTYPE_FUNCDEF_STMT:
        return _h64exprname_funcdef_stmt;
    case H64EXPRTYPE_CALL_STMT:
        return _h64exprname_call_stmt;
    case H64EXPRTYPE_CLASSDEF_STMT:
        return _h64exprname_classdef_stmt;
    case H64EXPRTYPE_IF_STMT:
        return _h64exprname_if_stmt;
    case H64EXPRTYPE_WHILE_STMT:
        return _h64exprname_while_stmt;
    case H64EXPRTYPE_FOR_STMT:
        return _h64exprname_for_stmt;
    case H64EXPRTYPE_IMPORT_STMT:
        return _h64exprname_import_stmt;
    case H64EXPRTYPE_ASSIGN_STMT:
        return _h64exprname_assign_stmt;
    case H64EXPRTYPE_LITERAL:
        return _h64exprname_literal;
    case H64EXPRTYPE_IDENTIFIERREF:
        return _h64exprname_identifierref;
    case H64EXPRTYPE_UNARYOP:
        return _h64exprname_unaryop;
    case H64EXPRTYPE_BINARYOP:
        return _h64exprname_binaryop;
    case H64EXPRTYPE_CALL:
        return _h64exprname_call;
    case H64EXPRTYPE_LIST:
        return _h64exprname_list;
    case H64EXPRTYPE_SET:
        return _h64exprname_set;
    case H64EXPRTYPE_DICT:
        return _h64exprname_dict;
    default:
        return NULL;
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
    const char *typestrconst = ast_ExpressionTypeToStr(e->type);
    char *typestr = typestrconst ? strdup(typestrconst) : NULL;
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
        if (e->vardef.value) {
            jsonvalue *val = ast_ExpressionToJSON(e->vardef.value, fileuri);
            if (!val) {
                fail = 1;
            } else {
                if (!json_SetDict(v, "value", val)) {
                    json_Free(val);
                    fail = 1;
                }
            }
        }
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
