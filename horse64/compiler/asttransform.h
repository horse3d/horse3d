#ifndef HORSE64_ASTTRANSFORM_H_
#define HORSE64_ASTTRANSFORM_H_

typedef struct h64ast h64ast;
typedef struct h64compileproject h64compileproject;

typedef struct asttransforminfo {
    h64compileproject *pr;
    h64ast *ast;
    int isbuiltinmodule;
    int hadoutofmemory, hadunexpectederror;
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

#endif  // HORSE64_ASTTRANSFORM_H_
