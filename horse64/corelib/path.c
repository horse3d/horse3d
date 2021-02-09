// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
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


/// @module path Work with file system paths, and check their properties.


int pathlib_exists(
        h64vmthread *vmthread
        ) {
    /**
     * Check if the target represented given by the filesystem path exists,
     * as a directory, file, or whatever it might be.
     *
     * @func exists
     * @param path the filesystem path to check for existence
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     * @return @see{yes} if target path points to something that exists,
     *         otherwise @see{no}
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

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

    int result = 0;
    if (!filesys32_TargetExists(
            pathstr, pathlen, &result)) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "unexpected type of I/O error"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    vcresult->type = H64VALTYPE_BOOL;
    vcresult->int_value = (result != 0);
    return 1;
}

int pathlib_is_symlink(
        h64vmthread *vmthread
        ) {
    /**
     * Check whether the given path points to a symbolic link,
     * rather than another type of item like a regular file or directory.
     * If the given path doesn't exist at all, no is returned.
     *
     * **Note for Windows**:
     * On Windows, this will return yes both for an NTFS junction, and
     * for symbolic links.
     *
     * @func exists
     * @param path the filesystem path to check for being a symbolic link
     * @raises IOError raised when there is a failure that is not expected
     *     to go away on retry, like a permission error accessing the info
     *     about the target.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     a write timeout, and similar.
     * @return @see{yes} if target path points to a symbolic link,
     *         otherwise @see{no} (also if target does not exist).
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    int innererr = 0;
    int result = 0;
    if (!filesys32_IsSymlink(
            pathstr, pathlen, &innererr, &result)) {
        if (innererr == FS32_ERR_NOSUCHTARGET) {
            result = 0;
       } else if (innererr == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (innererr == FS32_ERR_OUTOFFDS) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "out of file descriptors"
            );
        } else {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_RESOURCEERROR,
                "unexpected type of I/O error"
            );
        }
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    vcresult->int_value = result;
    return 1;
}

int pathlib_get_cwd(
        h64vmthread *vmthread
        ) {
    /**
     * Get the 'current directory' path.
     *
     * @func get_cwd
     * @raises IOError raised when there was an error changing directory
     *     that will likely persist on immediate retry, like the target
     *     existing directory not existing.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     or similar.
     * @return the current directory as a @see{string}
     */
    assert(STACK_TOP(vmthread->stack) >= 0);
    if (STACK_TOP(vmthread->stack) < 1) {
        // Ensure space for return value
        if (!stack_ToSize(
                vmthread->stack, vmthread->stack->entry_count + 1, 0
                )) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory resizing stack for return value"
            );
        }
    }

    int64_t resultlen = 0;
    h64wchar *result = filesys32_GetCurrentDirectory(
        &resultlen
    );
    if (!result) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_RESOURCEERROR,
            "unexpectedly failed to get current directory"
        );
    }

    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult, result, resultlen)) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory returning current directory"
        );
    }
    return 1;
}

int pathlib_set_cwd(
        h64vmthread *vmthread
        ) {
    /**
     * Change the 'current directory' to the given path.
     *
     * @func set_cwd
     * @param path the target directory to change current directory to
     * @raises IOError raised when there was an error changing directory
     *     that will likely persist on immediate retry, like the target
     *     existing directory not existing.
     * @raises ResourceError raised when there is a failure that might go
     *     away on retry, like an unexpected disk input output error,
     *     or similar.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

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
    int result = filesys32_ChangeDirectory(
        pathstr, pathlen
    );
    if (result < 0) {
        if (result == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (result == FS32_ERR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "out of memory trying to change directory"
            );
        } else if (result == FS32_ERR_TARGETNOTADIRECTORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "target is not a directory"
            );
        } else if (result == FS32_ERR_NOSUCHTARGET) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "no such file or directory"
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
    return 1;
}

int pathlib_is_abs(
        h64vmthread *vmthread
        ) {
    /**
     * This returns whether the path given is an absolute filesystem
     * path. The disk is not touched to check this, so whether the target
     * of the path actually exists is irrelevant.
     *
     * @func is_abs
     * @param path the directory path @see{string} to check
     * @returns a @see{boolean} `yes` when the path is absolute, `no`
     *     when it is relative
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    int result = filesys32_IsAbsolutePath(
        pathstr, pathlen
    );
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    vcresult->type = H64VALTYPE_BOOL;
    vcresult->int_value = (result != 0);
    return 1;
}


int pathlib_normalize(
        h64vmthread *vmthread
        ) {
    /**
     * This normalizes the given filesystem path. All uses of separators,
     * unnecessary ascending double dots, etc. will be normalized.
     * Since this function does not actually access the disk, relative
     * paths remain as such, (since that would otherwise need querying the
     * working directory on the real disk) and letter casing remains
     * untouched since it would need checking whether a path points to
     * a case insensitive filesystem on disk.
     *
     * @func normalize
     * @param path the filesystem path as a @see{string} to be normalized
     * @returns the @see{string} of the resulting normalized path
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    h64wchar *resultstr = NULL;
    int64_t resultstrlen = 0;
    resultstr = filesys32_Normalize(
        pathstr, pathlen, &resultstrlen
    );
    if (!resultstr) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory transforming to normalized path"
        );
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(vmthread, vcresult,
            resultstr, resultstrlen)) {
        free(resultstr);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory transforming to normalized path"
        );
    }
    free(resultstr);
    return 1;
}


int pathlib_to_abs(
        h64vmthread *vmthread
        ) {
    /**
     * This transforms the given relative path into an absolute path. This
     * function does not actually access the disk beyond getting the current
     * directory path, so it will work no matter if the resulting target path
     * actually exists on disk or not. Paths that are already absolute paths
     * will be returned unchanged.
     *
     * @func to_abs
     * @param path the directory path @see{string} to return as absolute path
     * @returns the @see{string} of the path now as an absolute path
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    h64wchar *pathstr = NULL;
    int64_t pathlen = 0;
    valuecontent *vcpath = STACK_ENTRY(vmthread->stack, 0);
    if (vcpath->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue*)(vcpath->ptr_value))->type ==
                H64GCVALUETYPE_STRING) {
        pathstr = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.s
        );
        pathlen = (
            ((h64gcvalue*)(vcpath->ptr_value))->str_val.len
        );
    } else if (vcpath->type == H64VALTYPE_SHORTSTR) {
        pathstr = vcpath->shortstr_value;
        pathlen = vcpath->shortstr_len;
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "path argument must be a string"
        );
    }

    h64wchar *resultstr = NULL;
    int64_t resultstrlen = 0;
    resultstr = filesys32_ToAbsolutePath(
        pathstr, pathlen, &resultstrlen
    );
    if (!resultstr) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory transforming to absolute path"
        );
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(vmthread, vcresult,
            resultstr, resultstrlen)) {
        free(resultstr);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory transforming to absolute path"
        );
    }
    free(resultstr);
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
     * @param full_paths=no whether to return just the names of the entries
     *     themselves (full_paths=no, the default), or each name appended to
     *     the directory path argument you submitted as a combined path
     *     (full_paths=yes).
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

    int error = FS32_ERR_OTHERERROR;
    h64wchar **contents = NULL;
    int64_t *contentslen = NULL;
    int result = filesys32_ListFolder(
        pathstr, pathlen, &contents, &contentslen,
        full_paths, &error
    );
    if (!result) {
        if (error == FS32_ERR_OUTOFMEMORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory while computing path listing"
            );
        } else if (error == FS32_ERR_NOPERMISSION) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "permission denied"
            );
        } else if (error == FS32_ERR_TARGETNOTADIRECTORY) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_IOERROR,
                "target path is not a directory"
            );
        } else if (error == FS32_ERR_OUTOFFDS) {
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
        NULL, 0, 1, path_join_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.set_cwd:
    const char *path_set_cwd_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "set_cwd", &pathlib_set_cwd,
        NULL, 0, 1, path_set_cwd_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.get_cwd:
    const char *path_get_cwd_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "get_cwd", &pathlib_get_cwd,
        NULL, 0, 0, path_get_cwd_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.exists:
    const char *path_exists_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "exists", &pathlib_exists,
        NULL, 0, 1, path_exists_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.normalize:
    const char *path_normalize_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "normalize", &pathlib_normalize,
        NULL, 0, 1, path_normalize_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.is_abs:
    const char *path_is_abs_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "is_abs", &pathlib_is_abs,
        NULL, 0, 1, path_is_abs_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.to_abs:
    const char *path_to_abs_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "to_abs", &pathlib_to_abs,
        NULL, 0, 1, path_to_abs_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.is_symlink:
    const char *path_is_symlink_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "is_symlink", &pathlib_is_symlink,
        NULL, 0, 1, path_is_symlink_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // path.list:
    const char *path_list_kw_arg_name[] = {
        NULL, "full_paths"
    };
    idx = h64program_RegisterCFunction(
        p, "list", &pathlib_list,
        NULL, 0, 2, path_list_kw_arg_name,  // fileuri, args
        "path", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    return 1;
}