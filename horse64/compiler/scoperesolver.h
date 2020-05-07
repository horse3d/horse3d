#ifndef HORSE64_COMPILER_SCOPERESOLVER_H_
#define HORSE64_COMPILER_SCOPERESOLVER_H_

typedef struct h64ast h64ast;

int scoperesolver_ResolveAST(
    h64ast *unresolved_ast
);

#endif  // HORSE64_COMPILER_SCOPERESOLVER_H_
