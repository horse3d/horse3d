#ifndef HORSE64_COMPILER_OPTIMIZER_H_
#define HORSE64_COMPILER_OPTIMIZER_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;

int optimizer_MoveVardefs(
    h64compileproject *pr, h64ast *ast
);

#endif  // HORSE64_COMPILER_OPTIMIZER_H_
