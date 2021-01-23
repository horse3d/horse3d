// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "corelib/moduleless_containers.h"
#include "debugsymbols.h"
#include "stack.h"
#include "valuecontentstruct.h"
#include "vmexec.h"
#include "vmlist.h"
#include "vmmap.h"


int corelib_containeradd(  // $$builtin.$$container_add
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        if (!vmlist_Add(
                gcvalue->list_values, STACK_ENTRY(vmthread->stack, 0)
                )) {
            return vmexec_ReturnFuncError(
                vmthread, H64STDERROR_OUTOFMEMORYERROR,
                "alloc failure extending container"
            );
        }
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "cannot .add() on this container type"
        );
    }

    return 1;
}

typedef struct _containerjoin_map_iteratedata {
    genericmap *map;
    h64wchar *result;
    int64_t resultlen, resultalloc;
    int errortype;
    char *errormsg;

    h64wchar *keyvaluesep;
    int64_t keyvalueseplen;
    h64wchar *pairsep;
    int64_t pairseplen;

    valuecontent *_previouskey, *_previousvalue;
} _containerjoin_map_iteratedata;

static int _callback_containerjoin_map(
        void *userdata,
        valuecontent *key, valuecontent *value
        ) {
    _containerjoin_map_iteratedata *data = (
        (_containerjoin_map_iteratedata *)userdata
    );
    assert(!data->errormsg && data->errortype < 0);

    if ((key->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue *)key->ptr_value)->type !=
                H64GCVALUETYPE_STRING) &&
            key->type != H64VALTYPE_SHORTSTR) {
        data->errortype = H64STDERROR_TYPEERROR;
        data->errormsg = strdup(
            "cannot join on dict with non-string keys"
        );
        return 0;
    }
    if ((value->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue *)value->ptr_value)->type !=
                H64GCVALUETYPE_STRING) &&
            value->type != H64VALTYPE_SHORTSTR) {
        data->errortype = H64STDERROR_TYPEERROR;
        data->errormsg = strdup(
            "cannot join on dict with non-string values"
        );
        return 0;
    }
    h64wchar *keystr = NULL;
    int64_t keystrlen = 0;
    h64wchar *valuestr = NULL;
    int64_t valuestrlen = 0;
    if (key->type == H64VALTYPE_GCVAL) {
        assert(((h64gcvalue *)key->ptr_value)->type ==
               H64GCVALUETYPE_STRING);
        keystr = ((h64gcvalue *)key->ptr_value)->str_val.s;
        keystrlen = ((h64gcvalue *)key->ptr_value)->str_val.len;
    } else if (key->type == H64VALTYPE_SHORTSTR) {
        keystr = key->shortstr_value;
        keystrlen = key->shortstr_len;
    }
    if (value->type == H64VALTYPE_GCVAL) {
        assert(((h64gcvalue *)value->ptr_value)->type ==
               H64GCVALUETYPE_STRING);
        valuestr = ((h64gcvalue *)value->ptr_value)->str_val.s;
        valuestrlen = ((h64gcvalue *)value->ptr_value)->str_val.len;
    } else if (value->type == H64VALTYPE_SHORTSTR) {
        valuestr = value->shortstr_value;
        valuestrlen = value->shortstr_len;
    }

    int64_t addlen = (
        (data->_previousvalue != NULL ? data->pairseplen : 0) +
        keystrlen +
        data->keyvalueseplen +
        valuestrlen
    );
    if (data->resultlen + addlen > data->resultalloc) {
        int64_t newalloc = (
            data->resultlen + addlen + 32
        );
        if (newalloc < data->resultalloc * 2)
            newalloc = data->resultalloc * 2;
        h64wchar *newresult = realloc(
            data->result,
            sizeof(*data->result) * newalloc
        );
        if (!newresult) {
            data->errortype = H64STDERROR_OUTOFMEMORYERROR;
            data->errormsg = strdup(
                "out of memory expanding string"
            );
            return 0;
        }
        data->result = newresult;
    }
    int64_t o = data->resultlen;
    if (data->_previousvalue != NULL) {
        memcpy(
            data->result + o, data->pairsep,
            sizeof(*data->pairsep) * data->pairseplen
        );
        o += data->pairseplen;
    }
    memcpy(
        data->result + o, keystr, sizeof(*keystr) * keystrlen
    );
    o += keystrlen;
    memcpy(
        data->result + o, data->keyvaluesep,
        sizeof(*data->keyvaluesep) * data->keyvalueseplen
    );
    o += data->keyvalueseplen;
    memcpy(
        data->result + o, valuestr, sizeof(*valuestr) * valuestrlen
    );
    o += valuestrlen;
    data->resultlen += addlen;
    data->_previousvalue = value;
    data->_previouskey = key;
    return 1;
}

int corelib_containerjoin_map(  // $$builtin.$$container_join_map
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 3);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 2);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_MAP);

    h64wchar *params1 = NULL; int64_t param1len = 0;
    valuecontent *vparam1 = STACK_ENTRY(vmthread->stack, 0);
    if (vparam1->type == H64VALTYPE_SHORTSTR) {
        params1 = vparam1->shortstr_value;
        param1len = vparam1->shortstr_len;
    } else if (vparam1->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vparam1->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        params1 = ((h64gcvalue *)vparam1->ptr_value)->str_val.s;
        param1len = (
            ((h64gcvalue *)vparam1->ptr_value)->str_val.len
        );
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "arguments for map join must be two strings"
        );
    }

    h64wchar *params2 = NULL; int64_t param2len = 0;
    valuecontent *vparam2 = STACK_ENTRY(vmthread->stack, 1);
    if (vparam2->type == H64VALTYPE_SHORTSTR) {
        params2 = vparam2->shortstr_value;
        param2len = vparam2->shortstr_len;
    } else if (vparam2->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vparam2->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        params2 = ((h64gcvalue *)vparam2->ptr_value)->str_val.s;
        param2len = (
            ((h64gcvalue *)vparam2->ptr_value)->str_val.len
        );
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "arguments for map join must be two strings"
        );
    }

    genericmap *map = ((h64gcvalue *)vc->ptr_value)->map_values;
    _containerjoin_map_iteratedata data = {0};
    data.map = map;
    data.errortype = -1;
    data.keyvaluesep = params1;
    data.keyvalueseplen = param1len;
    data.pairsep = params2;
    data.pairseplen = param2len;
    int result = vmmap_IteratePairs(
        map, &data, &_callback_containerjoin_map
    );
    if (!result) {
        assert(data.errortype >= 0);
        int retvalue = vmexec_ReturnFuncError(
            vmthread, data.errortype,
            (data.errormsg ? data.errormsg : "msg alloc fail")
        );
        free(data.errormsg);
        if (data.result)
            free(data.result);
        return retvalue;
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult,
            data.result, data.resultlen)) {
        if (data.result)
            free(data.result);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory creating result string"
        );
    }
    ADDREF_NONHEAP(vcresult);
    if (data.result)
        free(data.result);
    return 1;
}

typedef struct _containerjoin_list_iteratedata {
    genericlist *list;
    h64wchar *result;
    int64_t resultlen, resultalloc;
    int errortype;
    char *errormsg;

    h64wchar *valuesep;
    int64_t valueseplen;

    valuecontent *_previousvalue;
} _containerjoin_list_iteratedata;

static int _callback_containerjoin_list(
        void *userdata, valuecontent *value
        ) {
    _containerjoin_list_iteratedata *data = (
        (_containerjoin_list_iteratedata *)userdata
    );
    assert(!data->errormsg && data->errortype < 0);

    if ((value->type != H64VALTYPE_GCVAL ||
            ((h64gcvalue *)value->ptr_value)->type !=
                H64GCVALUETYPE_STRING) &&
            value->type != H64VALTYPE_SHORTSTR) {
        data->errortype = H64STDERROR_TYPEERROR;
        data->errormsg = strdup(
            "cannot join on list with non-string values"
        );
        return 0;
    }
    h64wchar *valuestr = NULL;
    int64_t valuestrlen = 0;
    if (value->type == H64VALTYPE_GCVAL) {
        assert(((h64gcvalue *)value->ptr_value)->type ==
               H64GCVALUETYPE_STRING);
        valuestr = ((h64gcvalue *)value->ptr_value)->str_val.s;
        valuestrlen = ((h64gcvalue *)value->ptr_value)->str_val.len;
    } else if (value->type == H64VALTYPE_SHORTSTR) {
        valuestr = value->shortstr_value;
        valuestrlen = value->shortstr_len;
    }

    int64_t addlen = (
        (data->_previousvalue != NULL ? data->valueseplen : 0) +
        valuestrlen
    );
    if (data->resultlen + addlen > data->resultalloc) {
        int64_t newalloc = (
            data->resultlen + addlen + 32
        );
        if (newalloc < data->resultalloc * 2)
            newalloc = data->resultalloc * 2;
        h64wchar *newresult = realloc(
            data->result,
            sizeof(*data->result) * newalloc
        );
        if (!newresult) {
            data->errortype = H64STDERROR_OUTOFMEMORYERROR;
            data->errormsg = strdup(
                "out of memory expanding string"
            );
            return 0;
        }
        data->result = newresult;
    }
    int64_t o = data->resultlen;
    if (data->_previousvalue != NULL) {
        memcpy(
            data->result + o, data->valuesep,
            sizeof(*data->valuesep) * data->valueseplen
        );
        o += data->valueseplen;
    }
    memcpy(
        data->result + o, valuestr, sizeof(*valuestr) * valuestrlen
    );
    o += valuestrlen;
    data->resultlen += addlen;
    data->_previousvalue = value;
    return 1;
}

int corelib_containerjoin_list(  // $$builtin.$$container_join_list
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    assert(gcvalue->type == H64GCVALUETYPE_LIST);

    h64wchar *params1 = NULL; int64_t param1len = 0;
    valuecontent *vparam1 = STACK_ENTRY(vmthread->stack, 0);
    if (vparam1->type == H64VALTYPE_SHORTSTR) {
        params1 = vparam1->shortstr_value;
        param1len = vparam1->shortstr_len;
    } else if (vparam1->type == H64VALTYPE_GCVAL &&
            ((h64gcvalue *)vparam1->ptr_value)->type ==
                H64GCVALUETYPE_STRING) {
        params1 = ((h64gcvalue *)vparam1->ptr_value)->str_val.s;
        param1len = (
            ((h64gcvalue *)vparam1->ptr_value)->str_val.len
        );
    } else {
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_TYPEERROR,
            "arguments for list join must be string"
        );
    }

    genericlist *l = ((h64gcvalue *)vc->ptr_value)->list_values;
    _containerjoin_list_iteratedata data = {0};
    data.list = l;
    data.errortype = -1;
    data.valuesep = params1;
    data.valueseplen = param1len;
    int result = vmmap_IterateValues(
        l, &data, &_callback_containerjoin_list
    );
    if (!result) {
        assert(data.errortype >= 0);
        int retvalue = vmexec_ReturnFuncError(
            vmthread, data.errortype,
            (data.errormsg ? data.errormsg : "msg alloc fail")
        );
        free(data.errormsg);
        if (data.result)
            free(data.result);
        return retvalue;
    }
    valuecontent *vcresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vcresult);
    valuecontent_Free(vcresult);
    memset(vcresult, 0, sizeof(*vcresult));
    if (!valuecontent_SetStringU32(
            vmthread, vcresult,
            data.result, data.resultlen)) {
        if (data.result)
            free(data.result);
        return vmexec_ReturnFuncError(
            vmthread, H64STDERROR_OUTOFMEMORYERROR,
            "out of memory creating result string"
        );
    }
    ADDREF_NONHEAP(vcresult);
    if (data.result)
        free(data.result);
    return 1;
}

int corelib_containercontains(  // $$builtin.$$container_contains
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 1);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    int result = 0;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        result = vmlist_Contains(gcvalue->list_values,
            STACK_ENTRY(vmthread->stack, 0)
        );
    } else if (gcvalue->type == H64GCVALUETYPE_MAP) {
        result = vmmap_Contains(gcvalue->map_values,
            STACK_ENTRY(vmthread->stack, 0)
        );
    }

    valuecontent *vresult = STACK_ENTRY(vmthread->stack, 0);
    DELREF_NONHEAP(vresult);
    valuecontent_Free(vresult);
    memset(vresult, 0, sizeof(*vresult));
    vresult->type = H64VALTYPE_BOOL;
    vresult->int_value = result;
    return 1;
}

int corelib_RegisterContainersFunc(
            h64program *p, const char *name,
            funcid_t funcidx
            ) {
    char **new_func_name = realloc(
        p->container_indexes.func_name,
        sizeof(*new_func_name) * (p->container_indexes.func_count + 1)
    );
    if (!new_func_name)
        return 0;
    p->container_indexes.func_name = new_func_name;
    int64_t *new_name_idx = realloc(
        p->container_indexes.func_name_idx,
        sizeof(*new_name_idx) * (p->container_indexes.func_count + 1)
    );
    if (!new_name_idx)
        return 0;
    p->container_indexes.func_name_idx = new_name_idx;
    funcid_t *new_func_idx = realloc(
        p->container_indexes.func_idx,
        sizeof(*new_func_idx) * (p->container_indexes.func_count + 1)
    );
    if (!new_func_idx)
        return 0;
    p->container_indexes.func_idx = new_func_idx;
    p->container_indexes.func_name[
        p->container_indexes.func_count
    ] = strdup(name);
    if (!p->container_indexes.func_name[
            p->container_indexes.func_count])
        return 0;
    p->container_indexes.func_idx[
        p->container_indexes.func_count
    ] = funcidx;
    p->container_indexes.func_name_idx[
        p->container_indexes.func_count
    ] = h64debugsymbols_AttributeNameToAttributeNameId(
        p->symbols, name, 1, 1
    );
    if (p->container_indexes.func_name_idx[
            p->container_indexes.func_count
            ] < 0) {
        free(p->container_indexes.func_name[
             p->container_indexes.func_count]);
        return 0;
    }
    p->container_indexes.func_count++;
    return 1;
}

funcid_t corelib_GetContainerFuncIdx(
        h64program *p, int64_t nameidx, int container_type
        ) {
    int i = 0;
    while (i < p->container_indexes.func_count) {
        if (p->container_indexes.func_name_idx[i] == nameidx) {
            if (container_type == H64GCVALUETYPE_MAP &&
                    strcmp(p->container_indexes.func_name[i], "add") == 0)
                return -1;  // maps have no .add()
            if (strcmp(p->container_indexes.func_name[i],
                    "join") == 0) {
                if (container_type == H64GCVALUETYPE_MAP &&
                        p->func[p->container_indexes.func_idx[i]].
                            input_stack_size != 3) {
                    i++;  // this is not the 2 arg map .join()
                    continue;
                }
                if (container_type == H64GCVALUETYPE_LIST &&
                        p->func[p->container_indexes.func_idx[i]].
                            input_stack_size != 2) {
                    i++;  // this is not the 1 arg list .join()
                    continue;
                }
                if (container_type != H64GCVALUETYPE_MAP &&
                        container_type != H64GCVALUETYPE_LIST)
                    return -1;  // no .join() on this one
            }
            return p->container_indexes.func_idx[i];
        }
        i++;
    }
    return -1;
}

int corelib_RegisterContainerFuncs(h64program *p) {
    int64_t idx;

    // '$$container_add' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_add", &corelib_containeradd,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "add", idx))
        return 0;

    // '$$container_contains' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_contains", &corelib_containercontains,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "contains", idx))
        return 0;

    // '$$container_join_map' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_join_map", &corelib_containerjoin_map,
        NULL, 2, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "join", idx))
        return 0;

    // '$$container_join_list' function:
    idx = h64program_RegisterCFunction(
        p, "$$container_join_list", &corelib_containerjoin_list,
        NULL, 1, NULL, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    if (!corelib_RegisterContainersFunc(p, "join", idx))
        return 0;

    return 1;
}