
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "gcvalue.h"
#include "hash.h"
#include "uri.h"

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

    if (!corelib_RegisterErrorClasses(p)) {
        h64program_Free(p);
        return NULL;
    }
    if (!corelib_RegisterFuncs(p)) {
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
    free(p->classes);
    free(p->func);

    free(p);
}

int h64program_RegisterCFunction(
        h64program *p,
        const char *name,
        int (*func)(h64vmthread *vmthread, int stackbottom),
        const char *fileuri,
        int arg_count,
        char **arg_kwarg_name,
        int last_is_multiarg,
        const char *module_path,
        const char *library_name,
        int is_threadable,
        int associated_class_index
        ) {
    assert(p != NULL && p->symbols != NULL);
    h64func *new_func = realloc(
        p->func, sizeof(*p->func) * (p->func_count + 1)
    );
    if (!new_func)
        return -1;
    p->func = new_func;
    memset(&p->func[p->func_count], 0, sizeof(*p->func));

    int fileuriindex = -1;
    if (fileuri) {
        char *normalized_uri = uri_Normalize(fileuri, 1);
        if (!normalized_uri)
            return -1;
        int foundindex = 0;
        int k = 0;
        while (k > p->symbols->fileuri_count) {
            if (strcmp(p->symbols->fileuri[k], normalized_uri) == 0) {
                fileuriindex = k;
                foundindex = 1;
                break;
            }
            k++;
        }
        if (!foundindex) {
            char **new_fileuri = realloc(
                p->symbols->fileuri, sizeof(*new_fileuri) *
                (p->symbols->fileuri_count + 1)
            );
            if (!new_fileuri) {
                free(normalized_uri);
                return -1;
            }
            p->symbols->fileuri[p->symbols->fileuri_count] =
                normalized_uri;
            fileuriindex = p->symbols->fileuri_count;
            p->symbols->fileuri_count++;
            normalized_uri = NULL;
        }
        free(normalized_uri);
    }

    // Add to the func symbols table:
    h64funcsymbol *new_func_symbols = realloc(
        p->symbols->func_symbols,
        sizeof(*p->symbols->func_symbols) * (
            p->symbols->func_count + 1
        ));
    if (!new_func_symbols)
        return -1;
    p->symbols->func_symbols = new_func_symbols;
    memset(&p->symbols->func_symbols[p->symbols->func_count],
        0, sizeof(*p->symbols->func_symbols));
    p->symbols->func_symbols[p->symbols->func_count].name = (
        strdup(name)
    );
    p->symbols->func_symbols[p->symbols->func_count].
        fileuri_index = fileuriindex;
    if (!p->symbols->func_symbols[p->symbols->func_count].name) {
        funcsymboloom:
        h64debugsymbols_ClearFuncSymbol(
            &p->symbols->func_symbols[p->symbols->func_count]
        );
        return -1;
    }
    if (module_path) {
        p->symbols->func_symbols[p->symbols->func_count].modulepath = (
            strdup(module_path)
        );
        if (!p->symbols->func_symbols[p->symbols->func_count].
                modulepath)
            goto funcsymboloom;
    }
    if (library_name) {
        p->symbols->func_symbols[p->symbols->func_count].libraryname = (
            strdup(library_name)
        );
        if (!p->symbols->func_symbols[p->symbols->func_count].
                libraryname)
            goto funcsymboloom;
    }
    p->symbols->func_symbols[p->symbols->func_count].arg_count = arg_count;
    if (arg_count > 0) {
        p->symbols->func_symbols[p->symbols->func_count].
                arg_kwarg_name = (
            malloc(sizeof(*p->symbols->func_symbols[p->symbols->func_count].
                arg_kwarg_name) * arg_count));
        if (!p->symbols->func_symbols[p->symbols->func_count].
                arg_kwarg_name)
            goto funcsymboloom;
        memset(
            p->symbols->func_symbols[p->symbols->func_count].
            arg_kwarg_name, 0,
            sizeof(*p->symbols->func_symbols[p->symbols->func_count].
                arg_kwarg_name) * arg_count);
        int i = 0;
        while (i < arg_count) {
            p->symbols->func_symbols[p->symbols->func_count].
                arg_kwarg_name[i] = (
                (arg_kwarg_name && arg_kwarg_name[i]) ?
                 strdup(arg_kwarg_name[i]) : NULL
                );
            if (arg_kwarg_name && arg_kwarg_name[i] &&
                    p->symbols->func_symbols[p->symbols->func_count].
                    arg_kwarg_name[i] == NULL)
                goto funcsymboloom;
            i++;
        }
    }

    // Add function to lookup-by-namepath hash table:
    char *full_path = malloc(
        (library_name ? strlen("@") + strlen(library_name) +
         strlen(".") : 0) +
        (module_path ? strlen(module_path) + strlen(".") : 0) +
        strlen(name) +
        1  // null-terminator
    );
    if (!full_path)
        goto funcsymboloom;
    full_path[0] = '\0';
    if (library_name) {
        full_path[0] = '@';
        memcpy(full_path + 1, library_name, strlen(library_name));
        full_path[1 + strlen(library_name)] = '.';
        full_path[1 + strlen(library_name) + 1] = '\0';
    }
    if (module_path) {
        memcpy(full_path + strlen(full_path), module_path,
               strlen(module_path) + 1);
        full_path[strlen(full_path) + 1] = '\0';
        full_path[strlen(full_path)] = '.';
    }
    memcpy(full_path + strlen(full_path), name, strlen(name) + 1);
    uint64_t setno = p->symbols->classes_count;
    if (!hash_StringMapSet(
            p->symbols->func_namepath_to_func_id,
            full_path, setno)) {
        free(full_path);
        goto funcsymboloom;
    }
    free(full_path);

    // Add actual function entry:
    p->func[p->func_count].arg_count = arg_count;
    p->func[p->func_count].last_is_multiarg = last_is_multiarg;
    p->func[p->func_count].stack_slots_used = 0;
    p->func[p->func_count].is_threadable = is_threadable;
    p->func[p->func_count].iscfunc = 1;
    p->func[p->func_count].associated_class_index = (
        associated_class_index
    );
    p->func[p->func_count].cfunc_ptr = func;

    p->func_count++;
    p->symbols->func_count++;

    return p->func_count - 1;
}

int h64program_RegisterHorse64Function(
        h64program *p,
        const char *name,
        const char *fileuri,
        int arg_count,
        char **arg_kwarg_name,
        int last_is_multiarg,
        const char *module_path,
        const char *library_name,
        int associated_class_idx
        ) {
    int idx = h64program_RegisterCFunction(
        p, name, NULL, fileuri, arg_count, arg_kwarg_name,
        last_is_multiarg, module_path,
        library_name, -1, associated_class_idx
    );
    if (idx >= 0) {
        p->func[idx].iscfunc = 0;
    }
    return idx;
}

int h64program_AddClass(
        h64program *p,
        const char *name,
        const char *fileuri,
        const char *module_path,
        const char *library_name
        ) {
    assert(p != NULL && p->symbols != NULL);
    h64class *new_classes = realloc(
        p->classes, sizeof(*p->classes) * (p->classes_count + 1)
    );
    if (!new_classes)
        return -1;
    p->classes = new_classes;
    memset(&p->classes[p->classes_count], 0, sizeof(*p->classes));

    int fileuriindex = -1;
    if (fileuri) {
        char *normalized_uri = uri_Normalize(fileuri, 1);
        if (!normalized_uri)
            return -1;
        int foundindex = 0;
        int k = 0;
        while (k > p->symbols->fileuri_count) {
            if (strcmp(p->symbols->fileuri[k], normalized_uri) == 0) {
                fileuriindex = k;
                break;
            }
            k++;
        }
        if (fileuriindex < 0) {
            char **new_fileuri = realloc(
                p->symbols->fileuri, sizeof(*new_fileuri) *
                (p->symbols->fileuri_count + 1)
            );
            if (!new_fileuri) {
                free(normalized_uri);
                return -1;
            }
            p->symbols->fileuri[p->symbols->fileuri_count] =
                normalized_uri;
            fileuriindex = p->symbols->fileuri_count;
            p->symbols->fileuri_count++;
            normalized_uri = NULL;
        }
    }

    // Add to the class symbols table:
    h64classsymbol *new_classes_symbols = realloc(
        p->symbols->classes_symbols,
        sizeof(*p->symbols->classes_symbols) * (
            p->symbols->classes_count + 1
        ));
    if (!new_classes_symbols)
        return -1;
    p->symbols->classes_symbols = new_classes_symbols;
    memset(&p->symbols->classes_symbols[p->symbols->classes_count],
        0, sizeof(*p->symbols->classes_symbols));
    p->symbols->classes_symbols[p->symbols->classes_count].name = (
        strdup(name)
    );
    if (!p->symbols->classes_symbols[p->symbols->classes_count].name) {
        classsymboloom:
        h64debugsymbols_ClearClassSymbol(
            &p->symbols->classes_symbols[p->symbols->classes_count]
        );
        return -1;
    }
    p->symbols->classes_symbols[p->symbols->classes_count].
        fileuri_index = fileuriindex;
    if (module_path) {
        p->symbols->classes_symbols[p->symbols->classes_count].
            modulepath = strdup(module_path);
        if (!p->symbols->classes_symbols[p->symbols->classes_count].
                modulepath)
            goto classsymboloom;
    }
    if (library_name) {
        p->symbols->classes_symbols[p->symbols->classes_count].
            libraryname = strdup(library_name);
        if (!p->symbols->classes_symbols[p->symbols->classes_count].
                libraryname)
            goto classsymboloom;
    }

    // Add class to lookup-by-namepath hash table:
    char *full_path = malloc(
        (library_name ? strlen("@") + strlen(library_name) +
         strlen(".") : 0) +
        (module_path ? strlen(module_path) + strlen(".") : 0) +
        strlen(name) +
        1  // null-terminator
    );
    if (!full_path)
        goto classsymboloom;
    full_path[0] = '\0';
    if (library_name) {
        full_path[0] = '@';
        memcpy(full_path + 1, library_name, strlen(library_name));
        full_path[1 + strlen(library_name)] = '.';
        full_path[1 + strlen(library_name) + 1] = '\0';
    }
    if (module_path) {
        memcpy(full_path + strlen(full_path), module_path,
               strlen(module_path) + 1);
        full_path[strlen(full_path) + 1] = '\0';
        full_path[strlen(full_path)] = '.';
    }
    memcpy(full_path + strlen(full_path), name, strlen(name) + 1);
    uint64_t setno = p->symbols->classes_count;
    if (!hash_StringMapSet(
            p->symbols->class_namepath_to_class_id,
            full_path, setno)) {
        free(full_path);
        goto classsymboloom;
    }
    free(full_path);

    // Add actual class entry:
    p->classes[p->classes_count].members_count = 0;
    p->classes[p->classes_count].methods_count = 0;
    p->classes[p->classes_count].method_func_idx = NULL;

    p->classes_count++;
    p->symbols->classes_count++;

    return p->classes_count - 1;
}
