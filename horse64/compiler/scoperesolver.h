#ifndef HORSE64_COMPILER_SCOPERESOLVER_H_
#define HORSE64_COMPILER_SCOPERESOLVER_H_

typedef struct h64ast h64ast;
typedef struct h64compileproject h64compileproject;

int scoperesolver_ResolveAST(
    h64compileproject *pr, h64ast *unresolved_ast,
    int extract_program_main
);

#endif  // HORSE64_COMPILER_SCOPERESOLVER_H_
