#ifndef HORSE3D_COMPILER_OPERATOR_H_
#define HORSE3D_COMPILER_OPERATOR_H_

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
    H64OP_BOOLCOND_IN,
    H64OP_MEMBERBYIDENTIFIER,
    H64OP_MEMBERBYEXPR,
    H64OP_CALL,
    TOTAL_OP_COUNT
} h64optype;

#define IS_UNARY_OP(x) (x == H64OP_MATH_UNARY_SUBSTRACT || \
    x == H64OP_BOOLCOND_NOT || \
    x == H64OP_MATH_BINNOT)
#define IS_ASSIGN_OP(x) (x >= H64OP_ASSIGNMATH_DIVIDE && x <= H64OP_ASSIGN)

extern int operator_precedences_total_count;
extern int *operators_by_precedence_counts;
extern h64optype **operators_by_precedence;

static int operator_PrecedenceByType(int type) {
    if (IS_ASSIGN_OP(type))
        return 10;
    switch (type) {
    case H64OP_MEMBERBYIDENTIFIER: return 0;
    case H64OP_MEMBERBYEXPR: return 0;
    case H64OP_CALL: return 0;
    case H64OP_MATH_UNARYSUBSTRACT: return 1;
    case H64OP_MATH_BINNOT: return 1;
    case H64OP_MATH_BINAND: return 2;
    case H64OP_MATH_BINOR: return 3;
    case H64OP_MATH_BINXOR: return 4;
    case H64OP_MATH_DIVIDE: return 5;
    case H64OP_MATH_MULTIPLY: return 5;
    case H64OP_MATH_MODULO: return 5;
    case H64OP_MATH_ADD: return 6;
    case H64OP_MATH_SUBSTRACT: return 6;
    case H64OP_MATH_BINSHIFTLEFT: return 7;
    case H64OP_MATH_BINSHIFTRIGHT: return 7;
    case H64OP_CMP_EQUAL: return 8;
    case H64OP_CMP_NOTEQUAL: return 8;
    case H64OP_CMP_LARGEROREQUAL: return 9;
    case H64OP_CMP_SMALLEROREQUAL: return 9;
    case H64OP_CMP_LARGER: return 9;
    case H64OP_CMP_SMALLER: return 9;
    case H64OP_BOOLCOND_IN: return 10;
    case H64OP_BOOLCOND_NOT: return 11;
    case H64OP_BOOLCOND_AND: return 12;
    case H64OP_BOOLCOND_OR: return 13;
    }
    return -1;
}

const char *operator_OpTypeToStr(h64optype type);


#endif  // HORSE3D_COMPILER_OPERATOR_H_
