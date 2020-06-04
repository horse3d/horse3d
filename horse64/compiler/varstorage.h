#ifndef HORSE64_COMPILER_VARSTORAGE_H_
#define HORSE64_COMPILER_VARSTORAGE_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;

int varstorage_AssignLocalStorage(
    h64compileproject *pr, h64ast *ast
);


#endif  // HORSE64_COMPILER_VARSTORAGE_H_
