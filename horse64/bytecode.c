
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
            p->symbols->fileuri = new_fileuri;
            p->symbols->fileuri[p->symbols->fileuri_count] =
                normalized_uri;
            fileuriindex = p->symbols->fileuri_count;
            p->symbols->fileuri_count++;
            normalized_uri = NULL;
        }
        free(normalized_uri);
    }

    h64modulesymbols *msymbols = NULL;
    if (module_path) {
        msymbols = h64debugsymbols_GetModule(
            p->symbols, module_path, 1
        );
        if (!msymbols)
            return -1;
    } else {
        msymbols = h64debugsymbols_GetBuiltinModule(p->symbols);
        assert(msymbols != NULL);
    }

    // Add to the func symbols table:
    h64funcsymbol *new_func_symbols = realloc(
        msymbols->func_symbols,
        sizeof(*msymbols->func_symbols) * (
            msymbols->func_count + 1
        ));
    if (!new_func_symbols)
        return -1;
    msymbols->func_symbols = new_func_symbols;
    memset(&msymbols->func_symbols[msymbols->func_count],
        0, sizeof(*msymbols->func_symbols));
    msymbols->func_symbols[msymbols->func_count].name = (
        strdup(name)
    );
    msymbols->func_symbols[msymbols->func_count].
        fileuri_index = fileuriindex;
    if (!msymbols->func_symbols[msymbols->func_count].name) {
        funcsymboloom:
        h64debugsymbols_ClearFuncSymbol(
            &msymbols->func_symbols[msymbols->func_count]
        );
        return -1;
    }
    if (module_path) {
        msymbols->func_symbols[msymbols->func_count].modulepath = (
            strdup(module_path)
        );
        if (!msymbols->func_symbols[msymbols->func_count].
                modulepath)
            goto funcsymboloom;
    }
    if (library_name) {
        msymbols->func_symbols[msymbols->func_count].libraryname = (
            strdup(library_name)
        );
        if (!msymbols->func_symbols[msymbols->func_count].
                libraryname)
            goto funcsymboloom;
    }
    msymbols->func_symbols[msymbols->func_count].arg_count = arg_count;
    if (arg_count > 0) {
        msymbols->func_symbols[msymbols->func_count].
                arg_kwarg_name = (
            malloc(sizeof(*msymbols->func_symbols[msymbols->func_count].
                arg_kwarg_name) * arg_count));
        if (!msymbols->func_symbols[msymbols->func_count].
                arg_kwarg_name)
            goto funcsymboloom;
        memset(
            msymbols->func_symbols[msymbols->func_count].
            arg_kwarg_name, 0,
            sizeof(*msymbols->func_symbols[msymbols->func_count].
                arg_kwarg_name) * arg_count);
        int i = 0;
        while (i < arg_count) {
            msymbols->func_symbols[msymbols->func_count].
                arg_kwarg_name[i] = (
                (arg_kwarg_name && arg_kwarg_name[i]) ?
                 strdup(arg_kwarg_name[i]) : NULL
                );
            if (arg_kwarg_name && arg_kwarg_name[i] &&
                    msymbols->func_symbols[msymbols->func_count].
                    arg_kwarg_name[i] == NULL)
                goto funcsymboloom;
            i++;
        }
    }

    // Add function to lookup-by-name hash table:
    uint64_t setno = msymbols->func_count;
    if (!hash_StringMapSet(
            msymbols->func_name_to_func_id,
            name, setno)) {
        goto funcsymboloom;
    }

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
    msymbols->func_count++;

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
            p->symbols->fileuri = new_fileuri;
            p->symbols->fileuri[p->symbols->fileuri_count] =
                normalized_uri;
            fileuriindex = p->symbols->fileuri_count;
            p->symbols->fileuri_count++;
            normalized_uri = NULL;
        }
    }

    h64modulesymbols *msymbols = NULL;
    if (module_path) {
        msymbols = h64debugsymbols_GetModule(
            p->symbols, module_path, 1
        );
        if (!msymbols)
            return -1;
    } else {
        msymbols = h64debugsymbols_GetBuiltinModule(p->symbols);
        assert(msymbols != NULL);
    }

    // Add to the class symbols table:
    h64classsymbol *new_classes_symbols = realloc(
        msymbols->classes_symbols,
        sizeof(*msymbols->classes_symbols) * (
            msymbols->classes_count + 1
        ));
    if (!new_classes_symbols)
        return -1;
    msymbols->classes_symbols = new_classes_symbols;
    memset(&msymbols->classes_symbols[msymbols->classes_count],
        0, sizeof(*msymbols->classes_symbols));
    msymbols->classes_symbols[msymbols->classes_count].name = (
        strdup(name)
    );
    if (!msymbols->classes_symbols[msymbols->classes_count].name) {
        classsymboloom:
        h64debugsymbols_ClearClassSymbol(
            &msymbols->classes_symbols[msymbols->classes_count]
        );
        return -1;
    }
    msymbols->classes_symbols[msymbols->classes_count].
        fileuri_index = fileuriindex;
    if (module_path) {
        msymbols->classes_symbols[msymbols->classes_count].
            modulepath = strdup(module_path);
        if (!msymbols->classes_symbols[msymbols->classes_count].
                modulepath)
            goto classsymboloom;
    }
    if (library_name) {
        msymbols->classes_symbols[msymbols->classes_count].
            libraryname = strdup(library_name);
        if (!msymbols->classes_symbols[msymbols->classes_count].
                libraryname)
            goto classsymboloom;
    }

    // Add class to lookup-by-name hash table:
    uint64_t setno = msymbols->classes_count;
    if (!hash_StringMapSet(
            msymbols->class_name_to_class_id,
            name, setno)) {
        goto classsymboloom;
    }

    // Add actual class entry:
    p->classes[p->classes_count].members_count = 0;
    p->classes[p->classes_count].methods_count = 0;
    p->classes[p->classes_count].method_func_idx = NULL;

    p->classes_count++;
    msymbols->classes_count++;

    return p->classes_count - 1;
}
