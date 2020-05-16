
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"

h64program *h64program_New() {
    h64program *p = malloc(sizeof(*p));
    if (!p)
        return NULL;
    memset(p, 0, sizeof(*p));
    p->symbols = h64debugsymbols_New();
    if (!p->symbols) {
        h64program_Free(p);
        return NULL;
    }

    return p;
}

void h64program_Free(h64program *p) {
    if (!p)
        return;

    if (p->symbols)
        h64debugsymbols_Free(p->symbols);
   
    free(p);
}

int h64program_RegisterCFunction(
        const char *name,
        int (*func)(h64vmthread *vmthread),
        int arg_count,
        char **arg_kwarg_name,
        const char *module_path,
        const char *associated_class_name
        ) {
}

int h64program_AddClass(
        const char *name,
        int (*func)(h64vmthread *vmthread),
        const char *module_path
        ) {

}
