#ifndef HORSE64_SCOPE_H_
#define HORSE64_SCOPE_H_


typedef struct h64expression h64expression;
typedef struct h64scope h64scope;

typedef struct h64scope {
    int declarationref_count;
    h64expression *declarationref;
    h64scope *parentscope;
} h64scope;


#endif  // HORSE64_SCOPE_H_
