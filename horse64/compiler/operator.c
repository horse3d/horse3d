#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compiler/operator.h"


static char h64opname_invalid[] = "H64OP_INVALID";
static char h64opname_math_divide[] = "H64OP_MATH_DIVIDE";
static char h64opname_math_addition[] = "H64OP_MATH_ADD";
static char h64opname_math_unarysubstract[] = "H64OP_MATH_UNARYSUBSTRACT";
static char h64opname_math_substract[] = "H64OP_MATH_SUBSTRACT";
static char h64opname_math_multiply[] = "H64OP_MATH_MULTIPLY";
static char h64opname_math_modulo[] = "H64OP_MATH_MODULO";
static char h64opname_math_binor[] = "H64OP_MATH_MODULO";
static char h64opname_math_binand[] = "H64OP_MATH_BINAND";
static char h64opname_math_binxor[] = "H64OP_MATH_BINXOR";
static char h64opname_math_binnot[] = "H64OP_MATH_BINNOT";
static char h64opname_math_binshiftleft[] = "H64OP_MATH_BINSHIFTLEFT";
static char h64opname_math_binshiftright[] = "H64OP_MATH_BINSHIFTRIGHT";
static char h64opname_assignmath_divide[] = "H64OP_ASSIGNMATH_DIVIDE";
static char h64opname_assignmath_add[] = "H64OP_ASSIGNMATH_ADD";
static char h64opname_assignmath_substract[] = "H64OP_ASSIGNMATH_SUBSTRACT";
static char h64opname_assignmath_multiply[] = "H64OP_ASSIGNMATH_MULTIPLY";
static char h64opname_assignmath_modulo[] = "H64OP_ASSIGNMATH_MODULO";
static char h64opname_assignmath_binor[] = "H64OP_ASSIGNMATH_BINOR";
static char h64opname_assignmath_binand[] = "H64OP_ASSIGNMATH_BINAND";
static char h64opname_assignmath_binxor[] = "H64OP_ASSIGNMATH_BINXOR";
static char h64opname_assignmath_binnot[] = "H64OP_ASSIGNMATH_BINNOT";
static char h64opname_assignmath_binshiftleft[] = "H64OP_ASSIGNMATH_BINSHIFTLEFT";
static char h64opname_assignmath_binshiftright[] = "H64OP_ASSIGNMATH_BINSHIFTRIGHT";
static char h64opname_assign[] = "H64OP_ASSIGN";
static char h64opname_cmp_equal[] = "H64OP_CMP_EQUAL";
static char h64opname_cmp_notequal[] = "H64OP_CMP_NOTEQUAL";
static char h64opname_cmp_largerorequal[] = "H64OP_CMP_LARGEROREQUAL";
static char h64opname_cmp_smallerorequal[] = "H64OP_CMP_SMALLEROREQUAL";
static char h64opname_cmp_larger[] = "H64OP_CMP_LARGER";
static char h64opname_cmp_smaller[] = "H64OP_CMP_SMALLER";
static char h64opname_boolcond_and[] = "H64OP_BOOLCOND_AND";
static char h64opname_boolcond_or[] = "H64OP_BOOLCOND_OR";
static char h64opname_boolcond_not[] = "H64OP_BOOLCOND_NOT";
static char h64opname_boolcond_in[] = "H64OP_BOOLCOND_IN";
static char h64opname_memberbyidentifier[] = "H64OP_MEMBERBYIDENTIFIER";
static char h64opname_memberbyexpr[] = "H64OP_MEMBERBYEXPR";
static char h64opname_call[] = "H64OP_CALL";


int operator_precedences_total_count = 0;
int *operators_by_precedence_counts = NULL;
h64optype **operators_by_precedence = NULL;

__attribute__((constructor)) void _init_precedences() {
    int i = 1;  // skip H64OP_INVALID
    while (i < TOTAL_OP_COUNT) {
        int precedence = operator_PrecedenceByType(i);
        if (precedence >= operator_precedences_total_count)
            operator_precedences_total_count = precedence + 1;
        i++;
    }
    assert(operator_precedences_total_count > 0);
    operators_by_precedence = malloc(
        sizeof(*operators_by_precedence) *
        operator_precedences_total_count
    );
    if (!operators_by_precedence) {
        allocfail:
        fprintf(stderr, "horsecc: error: FATAL: "
            "operator precedence table alloc fail");
        _exit(1);
        return;
    }
    memset(operators_by_precedence, 0,
        sizeof(*operators_by_precedence) *
        operator_precedences_total_count);
    operators_by_precedence_counts = malloc(
        sizeof(*operators_by_precedence_counts) *
        operator_precedences_total_count
    );
    if (!operators_by_precedence_counts)
        goto allocfail;
    memset(operators_by_precedence_counts, 0,
        sizeof(*operators_by_precedence_counts) *
        operator_precedences_total_count);
    i = 1;  // skip H64OP_INVALID
    while (i < TOTAL_OP_COUNT) {
        int precedence = operator_PrecedenceByType(i);
        int c = operators_by_precedence_counts[i];
        h64optype *new_list = realloc(
            operators_by_precedence[i],
            sizeof(**operators_by_precedence) * (c + 1)
        );
        if (!new_list)
            goto allocfail;
        operators_by_precedence[i] = new_list;
        new_list[c] = i;
        operators_by_precedence_counts[i]++;
        i++;
    }
}

const char *operator_OpTypeToStr(h64optype type) {
    switch (type) {
    case H64OP_INVALID:
        return h64opname_invalid;
    case H64OP_MATH_DIVIDE:
        return h64opname_math_divide;
    case H64OP_MATH_ADD:
        return h64opname_math_addition;
    case H64OP_MATH_SUBSTRACT:
        return h64opname_math_substract;
    case H64OP_MATH_UNARYSUBSTRACT:
        return h64opname_math_unarysubstract;
    case H64OP_MATH_MULTIPLY:
        return h64opname_math_multiply;
    case H64OP_MATH_MODULO:
        return h64opname_math_modulo;
    case H64OP_MATH_BINOR:
        return h64opname_math_binor;
    case H64OP_MATH_BINAND:
        return h64opname_math_binand;
    case H64OP_MATH_BINXOR:
        return h64opname_math_binxor;
    case H64OP_MATH_BINNOT:
        return h64opname_math_binnot;
    case H64OP_MATH_BINSHIFTLEFT:
        return h64opname_math_binshiftleft;
    case H64OP_MATH_BINSHIFTRIGHT:
        return h64opname_math_binshiftright;
    case H64OP_ASSIGNMATH_DIVIDE:
        return h64opname_assignmath_divide;
    case H64OP_ASSIGNMATH_ADD:
        return h64opname_assignmath_add;
    case H64OP_ASSIGNMATH_SUBSTRACT:
        return h64opname_assignmath_substract;
    case H64OP_ASSIGNMATH_MULTIPLY:
        return h64opname_assignmath_multiply;
    case H64OP_ASSIGNMATH_MODULO:
        return h64opname_assignmath_modulo;
    case H64OP_ASSIGNMATH_BINOR:
        return h64opname_assignmath_binor;
    case H64OP_ASSIGNMATH_BINAND:
        return h64opname_assignmath_binand;
    case H64OP_ASSIGNMATH_BINXOR:
        return h64opname_assignmath_binxor;
    case H64OP_ASSIGNMATH_BINNOT:
        return h64opname_assignmath_binnot;
    case H64OP_ASSIGNMATH_BINSHIFTLEFT:
        return h64opname_assignmath_binshiftleft;
    case H64OP_ASSIGNMATH_BINSHIFTRIGHT:
        return h64opname_assignmath_binshiftright;
    case H64OP_ASSIGN:
        return h64opname_assign;
    case H64OP_CMP_EQUAL:
        return h64opname_cmp_equal;
    case H64OP_CMP_NOTEQUAL:
        return h64opname_cmp_notequal;
    case H64OP_CMP_LARGEROREQUAL:
        return h64opname_cmp_largerorequal;
    case H64OP_CMP_SMALLEROREQUAL:
        return h64opname_cmp_smallerorequal;
    case H64OP_CMP_LARGER:
        return h64opname_cmp_larger;
    case H64OP_CMP_SMALLER:
        return h64opname_cmp_smaller;
    case H64OP_BOOLCOND_AND:
        return h64opname_boolcond_and;
    case H64OP_BOOLCOND_OR:
        return h64opname_boolcond_or;
    case H64OP_BOOLCOND_NOT:
        return h64opname_boolcond_not;
    case H64OP_BOOLCOND_IN:
        return h64opname_boolcond_in;
    case H64OP_MEMBERBYIDENTIFIER:
        return h64opname_memberbyidentifier;
    case H64OP_MEMBERBYEXPR:
        return h64opname_memberbyexpr;
    case H64OP_CALL:
        return h64opname_call;
    case TOTAL_OP_COUNT:
        return NULL;
    }
    return NULL;
}
