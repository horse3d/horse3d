// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "compiler/operator.h"
#include "nonlocale.h"


static char h64opprinted_invalid[] = "H64OP_INVALID";
static char h64opprinted_math_divide[] = "/";
static char h64opprinted_math_addition[] = "+";
static char h64opprinted_math_unarysubstract[] = "-";
static char h64opprinted_math_substract[] = "-";
static char h64opprinted_math_multiply[] = "*";
static char h64opprinted_math_modulo[] = "%";
static char h64opprinted_math_binor[] = "|";
static char h64opprinted_math_binand[] = "&";
static char h64opprinted_math_binxor[] = "^";
static char h64opprinted_math_binnot[] = "~";
static char h64opprinted_math_binshiftleft[] = "<<";
static char h64opprinted_math_binshiftright[] = ">>";
static char h64opprinted_assignmath_divide[] = "/=";
static char h64opprinted_assignmath_add[] = "+=";
static char h64opprinted_assignmath_substract[] = "=-";
static char h64opprinted_assignmath_multiply[] = "*=";
static char h64opprinted_assignmath_modulo[] = "%=";
static char h64opprinted_assignmath_binor[] = "|=";
static char h64opprinted_assignmath_binand[] = "&=";
static char h64opprinted_assignmath_binxor[] = "^=";
static char h64opprinted_assignmath_binnot[] = "~=";
static char h64opprinted_assignmath_binshiftleft[] = "<<=";
static char h64opprinted_assignmath_binshiftright[] = ">>=";
static char h64opprinted_assign[] = "=";
static char h64opprinted_cmp_equal[] = "==";
static char h64opprinted_cmp_notequal[] = "!=";
static char h64opprinted_cmp_largerorequal[] = ">=";
static char h64opprinted_cmp_smallerorequal[] = "<=";
static char h64opprinted_cmp_larger[] = ">";
static char h64opprinted_cmp_smaller[] = "<";
static char h64opprinted_boolcond_and[] = "and";
static char h64opprinted_boolcond_or[] = "or";
static char h64opprinted_boolcond_not[] = "not";
static char h64opprinted_attributebyidentifier[] = ".";
static char h64opprinted_indexbyexpr[] = "[";
static char h64opprinted_call[] = "(";
static char h64opprinted_new[] = "new";


const char *operator_OpPrintedAsStr(h64optype type) {
    switch (type) {
    case H64OP_INVALID:
        return h64opprinted_invalid;
    case H64OP_MATH_DIVIDE:
        return h64opprinted_math_divide;
    case H64OP_MATH_ADD:
        return h64opprinted_math_addition;
    case H64OP_MATH_SUBSTRACT:
        return h64opprinted_math_substract;
    case H64OP_MATH_UNARYSUBSTRACT:
        return h64opprinted_math_unarysubstract;
    case H64OP_MATH_MULTIPLY:
        return h64opprinted_math_multiply;
    case H64OP_MATH_MODULO:
        return h64opprinted_math_modulo;
    case H64OP_MATH_BINOR:
        return h64opprinted_math_binor;
    case H64OP_MATH_BINAND:
        return h64opprinted_math_binand;
    case H64OP_MATH_BINXOR:
        return h64opprinted_math_binxor;
    case H64OP_MATH_BINNOT:
        return h64opprinted_math_binnot;
    case H64OP_MATH_BINSHIFTLEFT:
        return h64opprinted_math_binshiftleft;
    case H64OP_MATH_BINSHIFTRIGHT:
        return h64opprinted_math_binshiftright;
    case H64OP_ASSIGNMATH_DIVIDE:
        return h64opprinted_assignmath_divide;
    case H64OP_ASSIGNMATH_ADD:
        return h64opprinted_assignmath_add;
    case H64OP_ASSIGNMATH_SUBSTRACT:
        return h64opprinted_assignmath_substract;
    case H64OP_ASSIGNMATH_MULTIPLY:
        return h64opprinted_assignmath_multiply;
    case H64OP_ASSIGNMATH_MODULO:
        return h64opprinted_assignmath_modulo;
    case H64OP_ASSIGNMATH_BINOR:
        return h64opprinted_assignmath_binor;
    case H64OP_ASSIGNMATH_BINAND:
        return h64opprinted_assignmath_binand;
    case H64OP_ASSIGNMATH_BINXOR:
        return h64opprinted_assignmath_binxor;
    case H64OP_ASSIGNMATH_BINNOT:
        return h64opprinted_assignmath_binnot;
    case H64OP_ASSIGNMATH_BINSHIFTLEFT:
        return h64opprinted_assignmath_binshiftleft;
    case H64OP_ASSIGNMATH_BINSHIFTRIGHT:
        return h64opprinted_assignmath_binshiftright;
    case H64OP_ASSIGN:
        return h64opprinted_assign;
    case H64OP_CMP_EQUAL:
        return h64opprinted_cmp_equal;
    case H64OP_CMP_NOTEQUAL:
        return h64opprinted_cmp_notequal;
    case H64OP_CMP_LARGEROREQUAL:
        return h64opprinted_cmp_largerorequal;
    case H64OP_CMP_SMALLEROREQUAL:
        return h64opprinted_cmp_smallerorequal;
    case H64OP_CMP_LARGER:
        return h64opprinted_cmp_larger;
    case H64OP_CMP_SMALLER:
        return h64opprinted_cmp_smaller;
    case H64OP_BOOLCOND_AND:
        return h64opprinted_boolcond_and;
    case H64OP_BOOLCOND_OR:
        return h64opprinted_boolcond_or;
    case H64OP_BOOLCOND_NOT:
        return h64opprinted_boolcond_not;
    case H64OP_ATTRIBUTEBYIDENTIFIER:
        return h64opprinted_attributebyidentifier;
    case H64OP_INDEXBYEXPR:
        return h64opprinted_indexbyexpr;
    case H64OP_CALL:
        return h64opprinted_call;
    case H64OP_NEW:
        return h64opprinted_new;
    case TOTAL_OP_COUNT:
        return NULL;
    }
    return NULL;
}

static char h64opname_invalid[] = "H64OP_INVALID";
static char h64opname_math_divide[] = "H64OP_MATH_DIVIDE";
static char h64opname_math_addition[] = "H64OP_MATH_ADD";
static char h64opname_math_unarysubstract[] =
    "H64OP_MATH_UNARYSUBSTRACT";
static char h64opname_math_substract[] = "H64OP_MATH_SUBSTRACT";
static char h64opname_math_multiply[] = "H64OP_MATH_MULTIPLY";
static char h64opname_math_modulo[] = "H64OP_MATH_MODULO";
static char h64opname_math_binor[] = "H64OP_MATH_BINOR";
static char h64opname_math_binand[] = "H64OP_MATH_BINAND";
static char h64opname_math_binxor[] = "H64OP_MATH_BINXOR";
static char h64opname_math_binnot[] = "H64OP_MATH_BINNOT";
static char h64opname_math_binshiftleft[] = "H64OP_MATH_BINSHIFTLEFT";
static char h64opname_math_binshiftright[] = "H64OP_MATH_BINSHIFTRIGHT";
static char h64opname_assignmath_divide[] = "H64OP_ASSIGNMATH_DIVIDE";
static char h64opname_assignmath_add[] = "H64OP_ASSIGNMATH_ADD";
static char h64opname_assignmath_substract[] =
    "H64OP_ASSIGNMATH_SUBSTRACT";
static char h64opname_assignmath_multiply[] =
    "H64OP_ASSIGNMATH_MULTIPLY";
static char h64opname_assignmath_modulo[] = "H64OP_ASSIGNMATH_MODULO";
static char h64opname_assignmath_binor[] = "H64OP_ASSIGNMATH_BINOR";
static char h64opname_assignmath_binand[] = "H64OP_ASSIGNMATH_BINAND";
static char h64opname_assignmath_binxor[] = "H64OP_ASSIGNMATH_BINXOR";
static char h64opname_assignmath_binnot[] = "H64OP_ASSIGNMATH_BINNOT";
static char h64opname_assignmath_binshiftleft[] =
    "H64OP_ASSIGNMATH_BINSHIFTLEFT";
static char h64opname_assignmath_binshiftright[] =
    "H64OP_ASSIGNMATH_BINSHIFTRIGHT";
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
static char h64opname_attributebyidentifier[] =
    "H64OP_ATTRIBUTEBYIDENTIFIER";
static char h64opname_indexbyexpr[] = "H64OP_INDEXBYEXPR";
static char h64opname_call[] = "H64OP_CALL";
static char h64opname_new[] = "H64OP_NEW";


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
        h64fprintf(stderr, "horsec: error: FATAL: "
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
        assert(precedence >= 0 &&
               precedence < operator_precedences_total_count);
        int c = operators_by_precedence_counts[precedence];
        h64optype *new_list = realloc(
            operators_by_precedence[precedence],
            sizeof(**operators_by_precedence) * (c + 1)
        );
        if (!new_list)
            goto allocfail;
        operators_by_precedence[precedence] = new_list;
        new_list[c] = i;
        operators_by_precedence_counts[precedence]++;
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
    case H64OP_ATTRIBUTEBYIDENTIFIER:
        return h64opname_attributebyidentifier;
    case H64OP_INDEXBYEXPR:
        return h64opname_indexbyexpr;
    case H64OP_CALL:
        return h64opname_call;
    case H64OP_NEW:
        return h64opname_new;
    case TOTAL_OP_COUNT:
        return NULL;
    }
    return NULL;
}
