// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_ASTTRANSFORM_H_
#define HORSE64_ASTTRANSFORM_H_

typedef struct h64ast h64ast;
typedef struct h64compileproject h64compileproject;

typedef struct asttransforminfo {
    h64compileproject *pr;
    h64ast *ast;
    int isbuiltinmodule;
    int hadoutofmemory, hadunexpectederror;
    int dont_descend_visitation;
    void *userdata;
} asttransforminfo;

int asttransform_Apply(
    h64compileproject *pr, h64ast *ast,
    int (*visit_in)(
        h64expression *expr, h64expression *parent, void *ud
    ),
    int (*visit_out)(
        h64expression *expr, h64expression *parent, void *ud
    ),
    void *ud
);

int _asttransform_cancel_visit_descend_callback(
    h64expression *expr, void *ud
);

#endif  // HORSE64_ASTTRANSFORM_H_
