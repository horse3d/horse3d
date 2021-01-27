// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "filesys32.h"
#include "path.h"
#include "poolalloc.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"
#include "widechar.h"


/// @module path Work with file system paths and folders.



int pathlib_remove(
        h64vmthread *vmthread
        ) {
    /**
     * Remove the target represented given by the filesystem path, which
     * may be a file or a directory.
     *
     * @func remove
     * @param path the filesystem path of the item to be removed
     * @param recursive=no whether a non-empty directory will cause an
     *     IOError (default, recursive=no), or the items will also be
     *     recursively removed (recursive=yes).
     * @raises IOError raised when there was an error removing the item
     *     that will likely persist on immediate retry, like lack of
     *     permissions, the target not existing, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vccomponents = STACK_ENTRY(vmthread->stack, 0);
    if (vccomponents->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vccomponents->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vccomponents->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vccomponents->ptr_value))->str_val.len
        );
    } else if (vccomponents->type == H64VALTYPE_SHORTSTR) {
        pathstr = vccomponents->shortstr_value;
        pathlen = vccomponents->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }
    int recursive = 0;
    valuecontent *vcrecursive = STACK_ENTRY(vmthread->stack, 1);
    if (vcrecursive->type == H64VALTYPE_BOOL) {
        recursive = (vcrecursive->int_value != 0);
    } else if (vcrecursive->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        recursive = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "recursive argument must be a boolean"
        );
    }

    int error = 0;
    int result = filesys32_RemoveFolderRecursively(
        pathstr, pathlen, &error
    );
    if (!result && error == FS32_REMOVEDIR_NOTADIR) {
        result = filesys32_RemoveFileOrEmptyDir(
            pathstr, pathlen, &error
        );
        if (!result) {
            if (error == FS32_REMOVEERR_DIRISBUSY) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "directory is in use by process "
                    "and cannot be deleted"
                );
            } else if (error == FS32_REMOVEERR_NONEMPTYDIRECTORY) {
                // Oops, seems like something concurrently
                // filled in files again.
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "recursive delete encountered unexpected re-added "
                    "file"
                );
            } else if (error == FS32_REMOVEERR_NOPERMISSION) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_IOERROR,
                    "permission denied"
                );
            } else if (error == FS32_REMOVEERR_NOSUCHTARGET) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_IOERROR,
                    "no such file or directory"
                );
            } else if (error == FS32_REMOVEERR_OUTOFFDS) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_RESOURCEERROR,
                    "out of file descriptors"
                );
            }
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "unexpected type of I/O error"
            );
        }
    } else if (!result) {
        if (error == FS32_REMOVEDIR_DIRISBUSY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "directory is in use by process "
                "and cannot be deleted"
            );
        } else if (error == FS32_REMOVEDIR_NONEMPTYDIRECTORY) {
            // Oops, seems like something concurrently
            // filled in files again.
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "encountered unexpected re-added "
                "file"
            );
        } else if (error == FS32_REMOVEDIR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (error == FS32_REMOVEDIR_NOSUCHTARGET) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
            );
        } else if (error == FS32_REMOVEDIR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    vcresult->type = H64VALTYPE_NONE;
    return 1;
}

int pathlib_list(
        h64vmthread *vmthread
        ) {
    /**
     * Return the list of files found in the directory pointed to
     * by the given filesystem path.
     *
     * @func list
     * @param path the directory path to list files from as a @see{string}.
     * @param full_path=no whether to return just the names of the entries
     *     themselves (full_path=no, the default), or each name appended to
     *     the directory path argument you submitted as a combined path
     *     (full_path=yes).
     * @raises IOError raised when there was an error accessing the directory
     *     that will likely persist on immediate retry, like lack of
     *     permissions, the target not being a directory, and so on.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a read timeout, and similar.
     * @returns a @see{list} with the names of all items contained in the
     *          given directory.
     */
    assert(STACK_TOP(vmthread->stack) >= 2);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vccomponents = STACK_ENTRY(vmthread->stack, 0);
    if (vccomponents->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vccomponents->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vccomponents->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vccomponents->ptr_value))->str_val.len
        );
    } else if (vccomponents->type == H64VALTYPE_SHORTSTR) {
        pathstr = vccomponents->shortstr_value;
        pathlen = vccomponents->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }
    int full_paths = 0;
    valuecontent *vcfullpath = STACK_ENTRY(vmthread->stack, 1);
    if (vcfullpath->type == H64VALTYPE_BOOL) {
        full_paths = (vcfullpath->int_value != 0);
    } else if (vcfullpath->type == H64VALTYPE_UNSPECIFIED_KWARG) {
        full_paths = 0;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "full_path argument must be a boolean"
        );
    }

    int error = FS32_LISTFOLDERERR_OTHERERROR;
    h64wchar **contents = NULL;
    int64_t *contentslen = NULL;
    int result = filesys32_ListFolder(
        pathstr, pathlen, &contents, &contentslen,
        full_paths, &error
    );
    if (!result) {
        if (error == FS32_LISTFOLDERERR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory while computing path listing"
            );
        } else if (error == FS32_LISTFOLDERERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (error == FS32_LISTFOLDERERR_TARGETNOTDIRECTORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "target path is not a directory"
            );
        } else if (error == FS32_LISTFOLDERERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        }
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpected type of I/O error"
        );
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    vcresult->type = H64VALTYPE_GCVAL;
    h64gcvalue *gcval = poolalloc_malloc(
        vmthread->heap, 0
    );
    if (!gcval) {
        oomfinallist: ;
        filesys32_FreeFolderList(contents, contentslen);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory while assembling result"
        );
    }
    vcresult->ptr_value = gcval;
    gcval->type = H64GCVALUETYPE_LIST;
    gcval->hash = 0;
    gcval->list_values = vmlist_New();
    if (!gcval->list_values) {
        poolalloc_free(vmthread->heap, gcval);
        vcresult->ptr_value = NULL;
        goto oomfinallist;
    }
    int64_t i = 0;
    while (contents[i]) {
        valuecontent s = {0};
        if (!valuecontent_SetStringU32(
                vmthread, &s, contents[i], contentslen[i]
                ))
            goto oomfinallist;
        ADDREF_NONHEAP(&s);
        int result = vmlist_Set(gcval->list_values, i + 1, &s);
        DELREF_NONHEAP(&s);
        valuecontent_Free(&s);
        if (!result)
            goto oomfinallist;
        i++;
    }
    filesys32_FreeFolderList(contents, contentslen);
    return 1;
}

int pathlib_join(
        h64vmthread *vmthread
        ) {
    /**
     * Join the given list of path components to a combined path.
     * This will use the platform-specific path separators, like
     * backslash (\) on Windows, forward slash (/) on Linux, etc.
     *
     * @func join
     * @param components the @see{list} of path components to be joined,
     *                   each component being a @see{string}.
     * @returns a @see{string} resulting from joining all path components.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vccomponents = STACK_ENTRY(vmthread->stack, 0);
    if (vccomponents->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue*)(vccomponents->ptr_value))->type !=
                H64GCVALUETYPE_LIST) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "components argument must be a list"
        );
    }

    h64wchar *result = NULL;
    int64_t resultlen = 0;
    genericlist *l = (
        ((h64gcvalue*)(vccomponents->ptr_value))->list_values
    );
    const int64_t len = vmlist_Count(l);
    if (len == 0) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "components list must have at least one item"
        );
    }
    int64_t i = 1;
    while (i <= len) {
        valuecontent *component = vmlist_Get(l, i);
        h64wchar *componentstr = NULL;
        int64_t componentlen = 0;
        if (component->type == H64VALTYPE_GCVAL &&
                ((h64gcvalue *)component->ptr_value)->type ==
                    H64GCVALUETYPE_STRING) {
            componentstr = (
                ((h64gcvalue *)component->ptr_value)->str_val.s
            );
            componentlen = (
                ((h64gcvalue *)component->ptr_value)->str_val.len
            );
        } else if (component->type == H64VALTYPE_SHORTSTR) {
            componentstr = component->shortstr_value;
            componentlen = component->shortstr_len;
        } else {
            free(result);
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_TYPEERROR,
                "components in list must each be a string"
            );
        }
        if (result == NULL) {
            result = malloc(componentlen * sizeof(*componentstr));
            if (!result) {
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory creating joined string"
                );
            }
            memcpy(result, componentstr,
                   componentlen * sizeof(*componentstr));
            resultlen = componentlen;
        } else {
            assert(result != NULL && componentstr != NULL);
            int64_t newresultlen = 0;
            h64wchar *newresult = filesys32_Join(
                result, resultlen, componentstr, componentlen,
                &newresultlen
            );
            if (!newresult) {
                free(result);
                return vmexec_ReturnFuncError(
                    vmthread, H64STDERROR_OUTOFMEMORYERROR,
                    "out of memory creating joined string"
                );
            }
            free(result);
            result = newresult;
            resultlen = newresultlen;
        }
        i++;
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, result, resultlen
            )) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory creating joined string"
        );
    }
    return 1;
}

int pathlib_RegisterFuncsAndModules(h64program *p) {
    int64_t idx;

    // path.join:
    const char *path_join_kw_arg_name[] = {
        NULL
    };    
    idx = h64program_RegisterCFunction(
        p, "join", &pathlib_join,
        NULL, 1, path_join_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.remove:
    const char *path_remove_kw_arg_name[] = {
        NULL, "recursive"
    };
    idx = h64program_RegisterCFunction(
        p, "remove", &pathlib_remove,
        NULL, 1, path_remove_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.list:
    const char *path_list_kw_arg_name[] = {
        NULL, "full_path"
    };
    idx = h64program_RegisterCFunction(
        p, "list", &pathlib_list,
        NULL, 2, path_list_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}