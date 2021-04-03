// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "bytecode.h"
#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "corelib/errors.h"
#include "corelib/system.h"
#include "filesys32.h"
#include "osinfo.h"
#include "packageversion.h"
#include "poolalloc.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmstrings.h"
#include "widechar.h"


int systemlib_platform(
        h64vmthread *vmthread
        ) {
    /**
     * Get the name of the current operating system platform.
     * Examples: "Windows", "Linux", "macOS", "Android", and more.
     * If the platform is unknown, this function returns none.
     *
     * @func platform
     * @returns the platform name if known as a @see{string}, or none
     */
    if (STACK_TOP(vmthread->stack) == 0) {
        int result = stack_ToSize(
            vmthread->stack, vmthread,
            vmthread->stack->entry_count + 1, 0
        );
        if (!result) {
            returnvaloom:
            return vmexec_ReturnFuncError(vmthread,
                H64STDERROR_OUTOFMEMORYERROR,
                "out of memory when returning value"
            );
        }
    }

    const char *platname = osinfo_PlatformName();
    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(vmthread, retval);
    memset(retval, 0, sizeof(*retval));
    if (platname != NULL && strlen(platname) > 0) {
        int64_t platname_u32len = 0;
        h64wchar *platname_u32 = (
            utf8_to_utf32(
                platname, strlen(platname),
                NULL, NULL, &platname_u32len
            ));
        if (!platname_u32)
            goto returnvaloom;
        assert(platname_u32len > 0);

        retval->type = H64VALTYPE_GCVAL;
        retval->ptr_value = poolalloc_malloc(
            vmthread->heap, 0
        );
        if (!retval->ptr_value) {
            free(platname_u32);
            goto returnvaloom;
        }
        h64gcvalue *gcval = (h64gcvalue*)retval->ptr_value;
        memset(gcval, 0, sizeof(*gcval));
        gcval->type = H64GCVALUETYPE_STRING;
        gcval->externalreferencecount = 1;
        gcval->heapreferencecount = 0;
        if (!vmstrings_AllocBuffer(
                vmthread, &gcval->str_val, platname_u32len)) {
            poolalloc_free(vmthread->heap, gcval);
            retval->ptr_value = NULL;
            free(platname_u32);
            goto returnvaloom;
        }
        memcpy(
            gcval->str_val.s, platname_u32,
            sizeof(*platname_u32) * platname_u32len
        );
        free(platname_u32);
    } else {
        retval->type = H64VALTYPE_NONE;
    }
    return 1;
}

int systemlib_cores(
        h64vmthread *vmthread
        ) {
    /**
     * Get the amount of processor cores on the current system.
     *
     * @func cores
     * @param physical=no
     *    Whether to return the logical cores of the CPU
     *    (physical=no, the default) which are often also named
     *    "CPU threads", or the physical cores (physical=yes).
     * @returns the processor cores as a @see{number}.
     */
    assert(STACK_TOP(vmthread->stack) >= 1);

    valuecontent *vcphysical = STACK_ENTRY(vmthread->stack, 0);
    if (vcphysical->type != H64VALTYPE_BOOL &&
            vcphysical->type != H64VALTYPE_UNSPECIFIED_KWARG) {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "physical must be a boolean"
        );
    }
    int physical = 0;
    if (vcphysical->type == H64VALTYPE_BOOL)
        physical = (vcphysical->int_value != 0);

    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(vmthread, retval);
    memset(retval, 0, sizeof(*retval));
    retval->type = H64VALTYPE_INT64;
    retval->int_value = (
        physical == 0 ? osinfo_CpuThreads() :
        osinfo_CpuCores()
    );
    return 1;
}

int systemlib_vm_exec_path(
        h64vmthread *vmthread
        ) {
    /**
     * Get the executable of horsevm/this program. For any standalone
     * program (result of "horsec compile") this returns the path to
     * your own program, if not deleted. For any script executed on-the-fly
     * via "horsec run", this returns the path to the horsec used to execute
     * the script. Returns none if the path cannot be determined, for
     * example due to a disk error, or the binary being deleted while running
     * which is allowed on most Unix systems.
     *
     * @func vm_exec_path
     * @returns the path to the executable as a @see{string}, or @see{none}.
     */
    assert(STACK_TOP(vmthread->stack) >= 0);
    if (STACK_TOP(vmthread->stack) < 1) {
        if (!stack_ToSize(
                vmthread->stack, vmthread,
                vmthread->stack->entry_count + 1, 0)) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory allocating return value"
            );
        }
    }

    int oom = 0;
    int64_t execpathlen = 0;
    h64wchar *execpath = (
        filesys32_GetOwnExecutable(&execpathlen, &oom)
    );
    if (!execpath) {
        if (oom) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "out of memory obtaining path"
            );
        }
        valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
        DELREF_NONHEAP(retval);
        valuecontent_Free(vmthread, retval);
        memset(retval, 0, sizeof(*retval));
        retval->type = H64VALTYPE_NONE;
        return 1;
    }

    valuecontent *retval = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(retval);
    valuecontent_Free(vmthread, retval);
    memset(retval, 0, sizeof(*retval));
    if (!valuecontent_SetStringU32(
            vmthread, retval, execpath, execpathlen
            )) {
        free(execpath);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory allocating return value"
        );
    }
    free(execpath);
    return 1;
}

int systemlib_RegisterFuncsAndModules(h64program *p) {
    // system.cores:
    const char *system_cores_kw_arg_name[] = {
        "physical"
    };
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "cores", &systemlib_cores,
        NULL, 0, 1, system_cores_kw_arg_name,  // fileuri, args
        "system", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // system.vm_exec_path:
    const char *system_vm_exec_path_kw_arg_name[] = {
        NULL
    };
    idx = h64program_RegisterCFunction(
        p, "vm_exec_path", &systemlib_vm_exec_path,
        NULL, 0, 0, system_vm_exec_path_kw_arg_name,  // fileuri, args
        "system", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // system.platform:
    const char *system_platform_kw_arg_name[] = {
        ""
    };
    idx = h64program_RegisterCFunction(
        p, "platform", &systemlib_platform,
        NULL, 0, 0, system_platform_kw_arg_name,  // fileuri, args
        "system", "core.horse64.org", 1, -1
    );
    if (idx < 0)
        return 0;

    // system.core_version:
    idx = h64program_AddGlobalvar(
        p, "core_version", 1, NULL, 0, "system", "core.horse64.org"
    );
    if (idx < 0)
        return 0;
    assert(p->globalvar[idx].content.type == H64VALTYPE_NONE);
    if (!valuecontent_SetPreallocStringU8(
            p, &p->globalvar[idx].content, CORELIB_VERSION
            ))
        return 0;
    ADDREF_NONHEAP(&p->globalvar[idx].content);
    assert(
        p->globalvar[idx].content.type != H64VALTYPE_GCVAL ||
        ((h64gcvalue*)p->globalvar[idx].content.ptr_value)->
            externalreferencecount == 1
    );

    return 1;
}