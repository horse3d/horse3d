#ifndef HORSE64_VMSTRINGS_H_
#define HORSE64_VMSTRINGS_H_

#include <stdint.h>

typedef uint32_t unicodechar;
typedef struct h64vmthread h64vmthread;

typedef struct h64stringval {
    unicodechar *s;
    uint64_t len;
    int refcount;
} h64stringval;

int vmstrings_Set(
    h64vmthread *vthread, h64stringval *v, uint64_t len
);

void vmstrings_Free(h64vmthread *vthread, h64stringval *v);

#endif  // HORSE64_VMSTRINGS_H_
