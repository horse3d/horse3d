
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
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
    p->main_func_index = -1;

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

int h64program_RegisterClassMemberEx(
        h64program *p,
        int64_t class_id,
        const char *name,
        int64_t func_idx
        ) {
    if (!p || !p->symbols)
        return 0;

    int64_t nameid = h64debugsymbols_MemberNameToMemberNameId(
        p->symbols, name, 1
    );
    if (nameid < 0)
        return 0;

    // Allocate bucket slot:
    assert(class_id >= 0 && class_id < p->classes_count);
    assert(p->classes[class_id].
           global_name_to_member_hashmap != NULL);
    int bucketindex = (nameid % (int64_t)H64CLASS_HASH_SIZE);
    h64classmemberinfo *buckets =
        (p->classes[class_id].
            global_name_to_member_hashmap[bucketindex]);
    int buckets_count = 0;
    while (buckets[buckets_count].nameid >= 0) {
        if (buckets[buckets_count].nameid == nameid)
            return 0;
        buckets_count++;
    }
    h64classmemberinfo *new_buckets = realloc(
        buckets, sizeof(*new_buckets) * (buckets_count + 2)
    );
    if (!new_buckets)
        return 0;
    p->classes[class_id].global_name_to_member_hashmap[
        bucketindex
    ] = new_buckets;
    buckets = new_buckets;

    // Allocate new slot for either methods or vars:
    int entry_idx = -1;
    if (func_idx >= 0) {
        if (p->classes[class_id].methods_count >=
                H64CLASS_MAX_METHODS)
            return 0;
        int64_t *new_method_global_name_idx = realloc(
            p->classes[class_id].method_global_name_idx,
            sizeof(*p->classes[class_id].
                   method_global_name_idx) *
            (p->classes[class_id].methods_count + 1)
        );
        if (!new_method_global_name_idx)
            return 0;
        p->classes[class_id].method_global_name_idx = (
            new_method_global_name_idx
        );
        int64_t *new_method_func_idx = realloc(
            p->classes[class_id].method_func_idx,
            sizeof(*p->classes[class_id].
                   method_func_idx) *
            (p->classes[class_id].methods_count + 1)
        );
        if (!new_method_func_idx)
            return 0;
        p->classes[class_id].method_func_idx = (
            new_method_func_idx
        );
        new_method_global_name_idx[
            p->classes[class_id].methods_count
        ] = nameid;
        new_method_func_idx[
            p->classes[class_id].methods_count
        ] = func_idx;
        p->classes[class_id].methods_count++;
        entry_idx = p->classes[class_id].methods_count - 1;
    } else {
        int64_t *new_vars_global_name_idx = realloc(
            p->classes[class_id].vars_global_name_idx,
            sizeof(*p->classes[class_id].
                   vars_global_name_idx) *
            (p->classes[class_id].vars_count + 1)
        );
        if (!new_vars_global_name_idx)
            return 0;
        p->classes[class_id].vars_global_name_idx = (
            new_vars_global_name_idx
        );
        new_vars_global_name_idx[
            p->classes[class_id].vars_count
        ] = nameid;
        p->classes[class_id].vars_count++;
        entry_idx = p->classes[class_id].vars_count - 1;
    }

    // Add into buckets:
    buckets[buckets_count + 1].nameid = -1;
    buckets[buckets_count + 1].methodorvaridx = -1;
    buckets[buckets_count].nameid = nameid;
    buckets[buckets_count].methodorvaridx = (
        func_idx > 0 ?
        entry_idx : (H64CLASS_MAX_METHODS + entry_idx)
    );
    return 1;
}

void h64program_LookupClassMember(
        h64program *p, int64_t class_id, int64_t nameid,
        int *out_membervarid, int *out_memberfuncid
        ) {
    assert(p != NULL && p->symbols != NULL);
    int bucketindex = (nameid % (int64_t)H64CLASS_HASH_SIZE);
    h64classmemberinfo *buckets =
        (p->classes[class_id].
            global_name_to_member_hashmap[bucketindex]);
    int i = 0;
    while (buckets[i].nameid >= 0) {
        if (buckets[i].nameid == nameid) {
            int64_t result = buckets[i].methodorvaridx;
            if (result < H64CLASS_MAX_METHODS) {
                *out_memberfuncid = result;
                *out_membervarid = -1;
            } else {
                *out_memberfuncid = -1;
                *out_membervarid = (result - H64CLASS_MAX_METHODS);
            }
            return;
        }
        i++;
    }
    *out_memberfuncid = -1;
    *out_membervarid = -1;
}

void h64program_LookupClassMemberByname(
        h64program *p, int64_t class_id, const char *name,
        int *out_membervarid, int *out_memberfuncid
        ) {
    int64_t nameid = h64debugsymbols_MemberNameToMemberNameId(
        p->symbols, name, 0
    );
    if (nameid < 0) {
        *out_membervarid = -1;
        *out_memberfuncid = -1;
    }
    return h64program_LookupClassMember(
        p, class_id, nameid, out_membervarid, out_memberfuncid
    );
}

void h64program_PrintBytecodeStats(h64program *p) {
    char _prefix[] = "horsec: info:";
    printf("%s bytecode func count: %" PRId64 "\n",
           _prefix, (int64_t)p->func_count);
    printf("%s bytecode global vars count: %" PRId64 "\n",
           _prefix, (int64_t)p->globals_count);
    printf("%s bytecode class count: %" PRId64 "\n",
           _prefix, (int64_t)p->classes_count);
    int i = 0;
    while (i < p->func_count) {
        const char _noname[] = "(unnamed)";
        const char *name = "(no symbols)";
        if (p->symbols) {
            h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
                p->symbols, i
            );
            assert(fsymbol != NULL);
            name = fsymbol->name;
            if (!name) name = _noname;
        }
        char associatedclass[64] = "";
        if (p->func[i].associated_class_index >= 0) {
            snprintf(
                associatedclass, sizeof(associatedclass) - 1,
                " (CLASS: %d)", p->func[i].associated_class_index
            );
        }
        printf(
            "%s bytecode func id=%" PRId64 " "
            "name: \"%s\" cfunction: %d%s%s\n",
            _prefix, (int64_t)i, name, p->func[i].iscfunc,
            (i == p->main_func_index ? " (PROGRAM START)" : ""),
            associatedclass
        );
        i++;
    }
    i = 0;
    while (i < p->classes_count) {
        const char *name = "(no symbols)";
        if (p->symbols) {
            h64classsymbol *csymbol = h64debugsymbols_GetClassSymbolById(
                p->symbols, i
            );
            assert(csymbol != NULL && csymbol->name != NULL);
            name = csymbol->name;
        }
        printf(
            "%s bytecode class id=%" PRId64 " "
            "name: \"%s\"\n",
            _prefix, (int64_t)i, name
        );
        i++;
    }
}

void h64program_Free(h64program *p) {
    if (!p)
        return;

    if (p->symbols)
        h64debugsymbols_Free(p->symbols);
    if (p->classes) {
        int i = 0;
        while (i < p->classes_count) {
            if (p->classes[i].global_name_to_member_hashmap) {
                int k = 0;
                while (k < H64CLASS_HASH_SIZE) {
                    free(p->classes[i].
                         global_name_to_member_hashmap[k]);
                    k++;
                }
                free(p->classes[i].global_name_to_member_hashmap);
            }
            free(p->classes[i].method_func_idx);
            free(p->classes[i].method_global_name_idx);
            free(p->classes[i].vars_global_name_idx);
            i++;
        }
    }
    free(p->classes);
    free(p->func);
    int i = 0;
    while (i < p->globalvar_count) {
        h64program_ClearValueContent(
            &p->globalvar[i].content, 0
        );
        i++;
    }
    free(p->globalvar);

    free(p);
}

static int _getfileuriindex(h64program *p, const char *fileuri) {
    char *normalized_uri = uri_Normalize(fileuri, 1);
    if (!normalized_uri)
        return -1;
    int fileuriindex = -1;
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
    return fileuriindex;
}

int h64program_AddGlobalvar(
        h64program *p,
        const char *name,
        int is_const,
        const char *fileuri,
        const char *module_path,
        const char *library_name
        ) {
    assert(p != NULL && p->symbols != NULL);
    h64globalvar *new_globalvar = realloc(
        p->globalvar, sizeof(*p->globalvar) * (p->globalvar_count + 1)
    );
    if (!new_globalvar)
        return -1;
    p->globalvar = new_globalvar;
    memset(&p->globalvar[p->globalvar_count], 0, sizeof(*p->globalvar));

    int fileuriindex = -1;
    if (fileuri) {
        fileuriindex = _getfileuriindex(p, fileuri);
        if (fileuriindex < 0)
            return -1;
    }

    h64modulesymbols *msymbols = NULL;
    if (module_path) {
        msymbols = h64debugsymbols_GetModule(
            p->symbols, module_path, library_name, 1
        );
        if (!msymbols)
            return -1;
    } else {
        assert(library_name == NULL);
        msymbols = h64debugsymbols_GetBuiltinModule(p->symbols);
        assert(msymbols != NULL);
    }

    // Add to the globalvar symbols table:
    h64globalvarsymbol *new_globalvar_symbols = realloc(
        msymbols->globalvar_symbols,
        sizeof(*msymbols->globalvar_symbols) * (
            msymbols->globalvar_count + 1
        ));
    if (!new_globalvar_symbols)
        return -1;
    msymbols->globalvar_symbols = new_globalvar_symbols;
    memset(&msymbols->globalvar_symbols[msymbols->globalvar_count],
        0, sizeof(*msymbols->globalvar_symbols));
    msymbols->globalvar_symbols[msymbols->globalvar_count].name = (
        strdup(name)
    );
    if (!msymbols->globalvar_symbols[msymbols->globalvar_count].name) {
        globalvarsymboloom:
        h64debugsymbols_ClearGlobalvarSymbol(
            &msymbols->globalvar_symbols[msymbols->globalvar_count]
        );
        return -1;
    }
    msymbols->globalvar_symbols[msymbols->globalvar_count].
        fileuri_index = fileuriindex;
    msymbols->globalvar_symbols[msymbols->globalvar_count].
        is_const = is_const;

    // Add globals to lookup-by-name hash table:
    uint64_t setno = msymbols->globalvar_count;
    assert(msymbols->globalvar_name_to_entry != NULL);
    if (!hash_StringMapSet(
            msymbols->globalvar_name_to_entry,
            name, setno)) {
        goto globalvarsymboloom;
    }

    // Add actual globalvar entry:
    memset(
        &p->globalvar[p->globalvar_count], 0,
        sizeof(p->globalvar[p->globalvar_count])
    );

    p->globalvar_count++;
    msymbols->globalvar_count++;

    return p->globalvar_count - 1;
}

int h64program_RegisterCFunction(
        h64program *p,
        const char *name,
        int (*func)(h64vmthread *vmthread),
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
    assert(name != NULL || associated_class_index < 0);
    h64func *new_func = realloc(
        p->func, sizeof(*p->func) * (p->func_count + 1)
    );
    if (!new_func)
        return -1;
    p->func = new_func;
    memset(&p->func[p->func_count], 0, sizeof(*p->func));

    int fileuriindex = -1;
    if (fileuri) {
        int fileuriindex = _getfileuriindex(p, fileuri);
        if (fileuriindex < 0)
            return -1;
    }

    h64modulesymbols *msymbols = NULL;
    if (module_path) {
        msymbols = h64debugsymbols_GetModule(
            p->symbols, module_path, library_name, 1
        );
        if (!msymbols)
            return -1;
    } else {
        assert(library_name == NULL);
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
    if (name) {
        msymbols->func_symbols[msymbols->func_count].name = (
            strdup(name)
        );
    }
    msymbols->func_symbols[msymbols->func_count].
        fileuri_index = fileuriindex;
    if (!msymbols->func_symbols[msymbols->func_count].name) {
        funcsymboloom:
        if (name)
            hash_StringMapUnset(
                msymbols->func_name_to_entry, name
            );
        if (p->symbols) {
            hash_IntMapUnset(
                p->symbols->func_id_to_module_symbols_index,
                p->func_count
            );
            hash_IntMapUnset(
                p->symbols->func_id_to_module_symbols_func_subindex,
                p->func_count
            );
        }
        h64debugsymbols_ClearFuncSymbol(
            &msymbols->func_symbols[msymbols->func_count]
        );
        return -1;
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
    if (name && !hash_StringMapSet(
            msymbols->func_name_to_entry,
            name, setno)) {
        goto funcsymboloom;
    }

    // Add it to lookups from func id to debug symbols:
    if (p->symbols && !hash_IntMapSet(
            p->symbols->func_id_to_module_symbols_index, p->func_count,
            (uint64_t)msymbols->index)) {
        goto funcsymboloom;
    }
    if (p->symbols && !hash_IntMapSet(
            p->symbols->func_id_to_module_symbols_func_subindex,
            p->func_count,
            (uint64_t)msymbols->func_count)) {
        goto funcsymboloom;
    }

    // Register function as class method if it is one:
    if (associated_class_index >= 0) {
        if (!h64program_RegisterClassMemberEx(
                p, associated_class_index,
                name, p->func_count
                )) {
            goto funcsymboloom;
        }
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
    msymbols->func_symbols[msymbols->func_count].global_id = p->func_count;

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
        int fileuriindex = _getfileuriindex(p, fileuri);
        if (fileuriindex < 0)
            return -1;
    }

    h64modulesymbols *msymbols = NULL;
    if (module_path) {
        msymbols = h64debugsymbols_GetModule(
            p->symbols, module_path, library_name, 1
        );
        if (!msymbols)
            return -1;
    } else {
        assert(library_name == NULL);
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
        if (name)
            hash_StringMapUnset(
                msymbols->class_name_to_entry, name
            );
        if (p->symbols) {
            hash_IntMapUnset(
                p->symbols->class_id_to_module_symbols_index,
                p->func_count
            );
            hash_IntMapUnset(
                p->symbols->class_id_to_module_symbols_class_subindex,
                p->func_count
            );
        }
        if (p->classes[p->classes_count].global_name_to_member_hashmap) {
            int i = 0;
            while (i < H64CLASS_HASH_SIZE) {
                free(p->classes[p->classes_count].
                     global_name_to_member_hashmap[i]);
                i++;
            }
            free(p->classes[p->classes_count].
                 global_name_to_member_hashmap);
        }
        h64debugsymbols_ClearClassSymbol(
            &msymbols->classes_symbols[msymbols->classes_count]
        );
        return -1;
    }
    msymbols->classes_symbols[msymbols->classes_count].
        fileuri_index = fileuriindex;

    // Add class to lookup-by-name hash table:
    uint64_t setno = msymbols->classes_count;
    if (!hash_StringMapSet(
            msymbols->class_name_to_entry,
            name, setno)) {
        goto classsymboloom;
    }

    // Add it to lookups from class id to debug symbols:
    if (p->symbols && !hash_IntMapSet(
            p->symbols->class_id_to_module_symbols_index, p->classes_count,
            (uint64_t)msymbols->index)) {
        goto classsymboloom;
    }
    if (p->symbols && !hash_IntMapSet(
            p->symbols->class_id_to_module_symbols_class_subindex,
            p->classes_count,
            (uint64_t)msymbols->classes_count)) {
        goto classsymboloom;
    }

    // Add actual class entry:
    p->classes[p->classes_count].global_name_to_member_hashmap = malloc(
        H64CLASS_HASH_SIZE * sizeof(
            *p->classes[p->classes_count].global_name_to_member_hashmap
        )
    );
    if (!p->classes[p->classes_count].global_name_to_member_hashmap)
        goto classsymboloom;
    memset(
        p->classes[p->classes_count].global_name_to_member_hashmap,
        0, H64CLASS_HASH_SIZE * sizeof(
            *p->classes[p->classes_count].global_name_to_member_hashmap
        )
    );
    int i = 0;
    while (i < H64CLASS_HASH_SIZE) {
        p->classes[p->classes_count].global_name_to_member_hashmap[i] =
            malloc(
                sizeof(**(p->classes[p->classes_count].
                global_name_to_member_hashmap))
            );
        if (!p->classes[p->classes_count].
                global_name_to_member_hashmap[i]) {
            goto classsymboloom;
        }
        p->classes[p->classes_count].
            global_name_to_member_hashmap[i][0].nameid = -1;
        p->classes[p->classes_count].
            global_name_to_member_hashmap[i][0].methodorvaridx = -1;
        i++;
    }

    p->classes_count++;
    msymbols->classes_count++;

    return p->classes_count - 1;
}

int h64program_RegisterClassVariable(
        h64program *p,
        int64_t class_id,
        const char *name
        ) {
    return h64program_RegisterClassMemberEx(
        p, class_id, name, -1
    );
}
