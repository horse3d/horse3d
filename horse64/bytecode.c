
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"

h64program *h64program_New() {
    h64program *p = malloc(sizeof(*p));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));

    return p;
}
