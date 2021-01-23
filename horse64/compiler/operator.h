// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_OPERATOR_H_
#define HORSE64_COMPILER_OPERATOR_H_

#include "compileconfig.h"

#include <stdint.h>
#include <stdlib.h>  // for NULL

#include "json.h"


typedef enum h64optype {
    H64OP_INVALID = 0,
    H64OP_MATH_DIVIDE,
    H64OP_MATH_ADD,
    H64OP_MATH_SUBSTRACT,
    H64OP_MATH_UNARYSUBSTRACT,
    H64OP_MATH_MULTIPLY,
    H64OP_MATH_MODULO,
    H64OP_MATH_BINOR,
    H64OP_MATH_BINAND,
    H64OP_MATH_BINXOR,
    H64OP_MATH_BINNOT,
    H64OP_MATH_BINSHIFTLEFT,
    H64OP_MATH_BINSHIFTRIGHT,
    H64OP_ASSIGNMATH_DIVIDE,
    H64OP_ASSIGNMATH_ADD,
    H64OP_ASSIGNMATH_SUBSTRACT,
    H64OP_ASSIGNMATH_MULTIPLY,
    H64OP_ASSIGNMATH_MODULO,
    H64OP_ASSIGNMATH_BINOR,
    H64OP_ASSIGNMATH_BINAND,
    H64OP_ASSIGNMATH_BINXOR,
    H64OP_ASSIGNMATH_BINNOT,
    H64OP_ASSIGNMATH_BINSHIFTLEFT,
    H64OP_ASSIGNMATH_BINSHIFTRIGHT,
    H64OP_ASSIGN,
    H64OP_CMP_EQUAL,
    H64OP_CMP_NOTEQUAL,
    H64OP_CMP_LARGEROREQUAL,
    H64OP_CMP_SMALLEROREQUAL,
    H64OP_CMP_LARGER,
    H64OP_CMP_SMALLER,
    H64OP_BOOLCOND_AND,
    H64OP_BOOLCOND_OR,
    H64OP_BOOLCOND_NOT,
    H64OP_ATTRIBUTEBYIDENTIFIER,
    H64OP_INDEXBYEXPR,
    H64OP_CALL,
    H64OP_NEW,
    TOTAL_OP_COUNT
} h64optype;

#define IS_UNARY_OP(x) (x == H64OP_MATH_UNARY_SUBSTRACT || \
    x == H64OP_BOOLCOND_NOT || \
    x == H64OP_MATH_BINNOT || x == H64OP_NEW)
#define IS_ASSIGN_OP(x) (x >= H64OP_ASSIGNMATH_DIVIDE && x <= H64OP_ASSIGN)
#define IS_UNWANTED_ASSIGN_OP(x) (x >= H64OP_ASSIGNMATH_MODULO && x <= H64OP_ASSIGNMATH_BINSHIFTRIGHT)

static int operator_AssignOpToMathOp(int assignop) {
    if (assignop >= H64OP_ASSIGNMATH_DIVIDE &&
            assignop <= H64OP_ASSIGNMATH_BINSHIFTRIGHT) {
        return assignop - (H64OP_ASSIGNMATH_DIVIDE - H64OP_MATH_DIVIDE);
    }
    return H64OP_INVALID;
}

extern int operator_precedences_total_count;
extern int *operators_by_precedence_counts;
extern h64optype **operators_by_precedence;

ATTR_UNUSED static int operator_PrecedenceByType(int type) {
    if (IS_ASSIGN_OP(type))
        return 10;
    switch (type) {
    case H64OP_ATTRIBUTEBYIDENTIFIER: return 0;
    case H64OP_INDEXBYEXPR: return 0;
    case H64OP_CALL: return 0;
    case H64OP_NEW: return 1;
    case H64OP_MATH_UNARYSUBSTRACT: return 2;
    case H64OP_MATH_BINNOT: return 3;
    case H64OP_MATH_BINAND: return 3;
    case H64OP_MATH_BINOR: return 4;
    case H64OP_MATH_BINXOR: return 5;
    case H64OP_MATH_DIVIDE: return 6;
    case H64OP_MATH_MULTIPLY: return 6;
    case H64OP_MATH_MODULO: return 6;
    case H64OP_MATH_ADD: return 7;
    case H64OP_MATH_SUBSTRACT: return 7;
    case H64OP_MATH_BINSHIFTLEFT: return 8;
    case H64OP_MATH_BINSHIFTRIGHT: return 8;
    case H64OP_CMP_LARGEROREQUAL: return 9;
    case H64OP_CMP_SMALLEROREQUAL: return 9;
    case H64OP_CMP_LARGER: return 9;
    case H64OP_CMP_SMALLER: return 9;
    case H64OP_CMP_EQUAL: return 10;
    case H64OP_CMP_NOTEQUAL: return 10;
    case H64OP_BOOLCOND_NOT: return 11;
    case H64OP_BOOLCOND_AND: return 12;
    case H64OP_BOOLCOND_OR: return 13;
    }
    return -1;
}

const char *operator_OpPrintedAsStr(h64optype type);

const char *operator_OpTypeToStr(h64optype type);


#endif  // HORSE64_COMPILER_OPERATOR_H_
