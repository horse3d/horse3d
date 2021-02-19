// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "bytecodeserialize.h"
#include "valuecontentstruct.h"

#define _DUMPSIZE(item, itemsize) \
    if (*out_len + (int64_t)(itemsize) > out_alloc) {\
        int64_t new_alloc = (\
            *out_len + (int64_t)(itemsize)\
        );\
        if (new_alloc < out_alloc * 2)\
            new_alloc = out_alloc * 2;\
        char *new_out = realloc(\
            *out, new_alloc\
        );\
        if (!new_out) {\
            free(*out);\
            *out = NULL;\
            *out_len = 0;\
            return 0;\
        }\
        *out = new_out;\
    }\
    memcpy(*out + *out_len, (char*)(item), (int64_t)(itemsize));\
    *out_len += (int64_t)(itemsize);

#define _DUMP(item) _DUMPSIZE(&(item), sizeof(item))


int h64program_Dump(h64program *p, char **out, int64_t *out_len) {
    *out = NULL;
    *out_len = 0;
    int64_t out_alloc = 0;

    char fileheader[] = "\x01H64BCODE_V1\x01";
    _DUMPSIZE(fileheader, strlen(fileheader));

    _DUMP(p->classes_count);
    {
        classid_t i = 0;
        while (i < p->classes_count) {
            h64class *c = &p->classes[i];
            _DUMP(c->base_class_global_id);
            _DUMP(c->is_error);
            _DUMP(c->is_threadable);
            _DUMP(c->user_set_parallel);
            _DUMP(c->has_equals_attr);

            _DUMP(c->funcattr_count);
            _DUMPSIZE(
                c->funcattr_global_name_idx,
                sizeof(*c->funcattr_global_name_idx) *
                c->funcattr_count
            );
            _DUMPSIZE(
                c->funcattr_func_idx,
                sizeof(*c->funcattr_func_idx) *
                c->funcattr_count
            );
            _DUMP(c->varattr_count);
            _DUMPSIZE(
                c->varattr_global_name_idx,
                sizeof(*c->varattr_global_name_idx) *
                c->varattr_count
            );
            _DUMPSIZE(
                c->varattr_flags,
                sizeof(*c->varattr_flags) *
                c->varattr_count
            );
            _DUMP(c->hasvarinitfunc);
            _DUMP(c->varinitfuncidx);
            i++;
        }
    }
    _DUMP(p->func_count);
    {
        funcid_t i = 0;
        while (i < p->func_count) {
            h64func *f = &p->func[i];
            _DUMP(f->input_stack_size);
            _DUMP(f->inner_stack_size);
            _DUMP(f->iscfunc);
            _DUMP(f->is_threadable);
            _DUMP(f->user_set_parallel);
            _DUMP(f->kwarg_count);
            _DUMPSIZE(
                f->kwargnameindexes,
                sizeof(*f->kwargnameindexes) *
                f->kwarg_count
            );
            _DUMP(f->async_progress_struct_size);

            int32_t len = (
                f->cfunclookup ? strlen(f->cfunclookup) : 0
            );
            _DUMP(len);
            _DUMPSIZE(
                f->cfunclookup, len
            );

            if (!f->iscfunc) {
                _DUMP(f->instructions_bytes);
                _DUMPSIZE(
                    f->instructions,
                    f->instructions_bytes
                );
                // Now, dump instruction extra data like strings:
                char *pinst = f->instructions;
                while (pinst < (char *)(f->instructions +
                        f->instructions_bytes)) {
                    h64instructionany *inst = (
                        (h64instructionany *)pinst
                    );
                    size_t instsize = (
                        h64program_PtrToInstructionSize(pinst)
                    );
                    if (inst->type == H64INST_SETCONST) {
                        h64instruction_setconst *iconst = (
                            (h64instruction_setconst *)inst
                        );
                        if (iconst->content.type ==
                                H64VALTYPE_CONSTPREALLOCSTR) {
                            int64_t len = (
                                iconst->content.
                                constpreallocstr_len
                            );
                            _DUMP(len);
                            _DUMPSIZE(
                                iconst->content.
                                    constpreallocstr_value,
                                len * sizeof(h64wchar)
                            );
                        } else if (iconst->content.type ==
                                H64VALTYPE_CONSTPREALLOCBYTES) {
                            int64_t len = (
                                iconst->content.
                                constpreallocbytes_len
                            );
                            _DUMP(len);
                            _DUMPSIZE(
                                 iconst->content.
                                    constpreallocbytes_value, len
                            );
                        }
                    }
                    pinst += instsize;
                }
            }

            i++;
        }
    }

    _DUMP(p->main_func_index);
    _DUMP(p->globalinitsimple_func_index);
    _DUMP(p->globalinit_func_index);
    _DUMP(p->has_attr_func_idx);
    _DUMP(p->is_a_func_index);

    _DUMP(p->as_bytes_name_index);
    _DUMP(p->as_str_name_index);
    _DUMP(p->len_name_index);
    _DUMP(p->init_name_index);
    _DUMP(p->on_cloned_name_index);
    _DUMP(p->on_destroy_name_index);
    _DUMP(p->add_name_index);
    _DUMP(p->del_name_index);
    _DUMP(p->contains_name_index);
    _DUMP(p->is_a_name_index);

    _DUMP(p->_io_file_class_idx);
    _DUMP(p->_net_stream_class_idx);
    _DUMP(p->_urilib_uri_class_idx);

    _DUMP(p->globalvar_count);
    {
        globalvarid_t i = 0;
        while (i < p->globalvar_count) {
            h64globalvar *gv = &p->globalvar[i];
            _DUMP(gv->content.type);
            if (gv->content.type == H64VALTYPE_NONE ||
                    gv->content.type == H64VALTYPE_UNSPECIFIED_KWARG) {
                // Nothing to do.
            } else if (gv->content.type == H64VALTYPE_BOOL ||
                    gv->content.type == H64VALTYPE_INT64 ||
                    gv->content.type == H64VALTYPE_FUNCREF ||
                    gv->content.type == H64VALTYPE_CLASSREF) {
                _DUMP(gv->content.int_value);
            } else if (gv->content.type == H64VALTYPE_FLOAT64) {
                _DUMP(gv->content.float_value);
            } else if (gv->content.type == H64VALTYPE_SHORTBYTES) {
                _DUMP(gv->content.shortbytes_len);
                _DUMPSIZE(
                    gv->content.shortbytes_value,
                    gv->content.shortbytes_len
                );
            } else if (gv->content.type ==
                       H64VALTYPE_CONSTPREALLOCBYTES) {
                _DUMP(gv->content.constpreallocbytes_len);
                _DUMPSIZE(
                    gv->content.constpreallocbytes_value,
                    gv->content.constpreallocbytes_len
                );
            } else if (gv->content.type == H64VALTYPE_SHORTSTR) {
                _DUMP(gv->content.shortstr_len);
                _DUMPSIZE(
                    gv->content.shortstr_value,
                    gv->content.shortstr_len*
                        sizeof(h64wchar)
                );
            } else if (gv->content.type ==
                       H64VALTYPE_CONSTPREALLOCSTR) {
                _DUMP(gv->content.constpreallocstr_len);
                _DUMPSIZE(
                    gv->content.constpreallocstr_value,
                    gv->content.constpreallocstr_len *
                        sizeof(h64wchar)
                );
            } else {
                fprintf(stderr,
                    "horse64/bytecodeserialize.c: error: "
                    "unsupported preset global var type %d\n",
                    gv->content.type
                );
            }
            _DUMP(gv->is_simple_constant);
            _DUMP(gv->is_const);
            i++;
        }
    }
    return 1;
}

#define _LOADSIZE(item, itemsize) \
    if (pinlen < (int64_t)(itemsize)) {\
        h64program_Free(p);\
        return 0;\
    }\
    memcpy((char*)(item), pin, (int64_t)(itemsize));\
    pin += itemsize;\
    pinlen -= itemsize;


#define _LOADSIZEALLOC_EX(item, itemsize, extraalloc) \
    if (pinlen < (int64_t)(itemsize)) {\
        h64program_Free(p);\
        return 0;\
    }\
    assert((item) == NULL);\
    item = malloc(itemsize + extraalloc);\
    if (!(item)) {\
        h64program_Free(p);\
        return 0;\
    }\
    memcpy((char*)(item), pin, (int64_t)(itemsize));\
    pin += itemsize;\
    pinlen -= itemsize;

#define _LOADSIZEALLOC(item, itemsize)\
    _LOADSIZEALLOC_EX(item, itemsize, 0);

#define _LOADSIZEALLOC_NULTERMINATED(item, itemsize) \
    _LOADSIZEALLOC_EX(item, itemsize, 1); \
    ((char*)item)[itemsize] = '\0';

#define _LOADSIZEALLOCZEROED(item, itemsize) \
    assert((item) == NULL);\
    item = malloc(itemsize);\
    if (!(item)) {\
        h64program_Free(p);\
        return 0;\
    }\
    memset((char*)(item), 0, (int64_t)(itemsize));

#define _LOAD(item) _LOADSIZE(&(item), sizeof(item));

int h64program_Restore(
        h64program **write_to_ptr, const char *in, int64_t in_len
        ) {
    const char *pin = in;
    int64_t pinlen = in_len;
    h64program *write_to = *write_to_ptr;
    h64program *p = malloc(sizeof(*p));
    if (!p)
        return 0;
    memset(p, 0, sizeof(*p));
    int alwaysfree_writeto = 0;
    if (!write_to) {
        write_to = h64program_New();
        if (!write_to)
            goto loadfail;
        alwaysfree_writeto = 1;
    }

    char fileheader[] = "\x01H64BCODE_V1\x01";
    char headercheck[256];
    _LOADSIZE(headercheck, strlen(fileheader));
    if (memcmp(headercheck, fileheader, strlen(fileheader)) != 0) {
        loadfail:
        h64program_Free(p);
        if (alwaysfree_writeto && write_to)
            h64program_Free(write_to);
        return 0;
    }

    _LOAD(p->classes_count);
    {
        _LOADSIZEALLOCZEROED(
            p->classes,
            sizeof(*p->classes) * p->classes_count
        );
        classid_t i = 0;
        while (i < p->classes_count) {
            h64class *c = &p->classes[i];
            _LOAD(c->base_class_global_id);
            _LOAD(c->is_error);
            _LOAD(c->is_threadable);
            _LOAD(c->user_set_parallel);
            _LOAD(c->has_equals_attr);

            _LOAD(c->funcattr_count);
            _LOADSIZEALLOC(
                c->funcattr_global_name_idx,
                sizeof(*c->funcattr_global_name_idx) *
                c->funcattr_count
            );
            _LOADSIZEALLOC(
                c->funcattr_func_idx,
                sizeof(*c->funcattr_func_idx) *
                c->funcattr_count
            );
            _LOAD(c->varattr_count);
            _LOADSIZEALLOC(
                c->varattr_global_name_idx,
                sizeof(*c->varattr_global_name_idx) *
                c->varattr_count
            );
            _LOADSIZEALLOC(
                c->varattr_flags,
                sizeof(*c->varattr_flags) *
                c->varattr_count
            );
            _LOAD(c->hasvarinitfunc);
            _LOAD(c->varinitfuncidx);
            i++;
        }
    }
    _LOAD(p->func_count);
    {
        _LOADSIZEALLOCZEROED(
            p->func,
            sizeof(*p->func) * p->func_count
        );
        funcid_t i = 0;
        while (i < p->func_count) {
            h64func *f = &p->func[i];
            _LOAD(f->input_stack_size);
            _LOAD(f->inner_stack_size);
            _LOAD(f->iscfunc);
            _LOAD(f->is_threadable);
            _LOAD(f->user_set_parallel);
            _LOAD(f->kwarg_count);
            _LOADSIZEALLOC(
                f->kwargnameindexes,
                sizeof(*f->kwargnameindexes) *
                f->kwarg_count
            );
            _LOAD(f->async_progress_struct_size);

            int32_t len = 0;
            _LOAD(len);
            _LOADSIZEALLOC_NULTERMINATED(
                f->cfunclookup, len
            );

            if (!f->iscfunc) {
                // Load up instructions with a direct copy:
                _LOAD(f->instructions_bytes);
                _LOADSIZEALLOC(
                    f->instructions,
                    f->instructions_bytes
                );
                // Now, we must also get separately allocated data
                // for the instructions. Only used for strings and bytes
                // constants right now.
                char *pinst = f->instructions;
                while (pinst < (char *)(f->instructions +
                        f->instructions_bytes)) {
                    h64instructionany *inst = (
                        (h64instructionany *)pinst
                    );
                    size_t instsize = (
                        h64program_PtrToInstructionSize(pinst)
                    );
                    if (inst->type == H64INST_SETCONST) {
                        h64instruction_setconst *iconst = (
                            (h64instruction_setconst *)inst
                        );
                        if (iconst->content.type ==
                                H64VALTYPE_CONSTPREALLOCSTR) {
                            int64_t len = 0;
                            _LOAD(len);
                            iconst->content.
                                constpreallocstr_value = NULL;
                            _LOADSIZEALLOC(
                                iconst->content.
                                    constpreallocstr_value,
                                len * sizeof(h64wchar)
                            );
                            iconst->content.
                                constpreallocstr_len = len;
                        } else if (iconst->content.type ==
                                H64VALTYPE_CONSTPREALLOCBYTES) {
                            int64_t len = 0;
                            _LOAD(len);
                            iconst->content.
                                constpreallocbytes_value = NULL;
                            _LOADSIZEALLOC(
                                 iconst->content.
                                    constpreallocbytes_value, len
                            );
                            iconst->content.
                                constpreallocbytes_len = len;
                        }
                    }
                    pinst += instsize;
                }
            }

            i++;
        }
    }

    _LOAD(p->main_func_index);
    _LOAD(p->globalinitsimple_func_index);
    _LOAD(p->globalinit_func_index);
    _LOAD(p->has_attr_func_idx);
    _LOAD(p->is_a_func_index);

    _LOAD(p->as_bytes_name_index);
    _LOAD(p->as_str_name_index);
    _LOAD(p->len_name_index);
    _LOAD(p->init_name_index);
    _LOAD(p->on_cloned_name_index);
    _LOAD(p->on_destroy_name_index);
    _LOAD(p->add_name_index);
    _LOAD(p->del_name_index);
    _LOAD(p->contains_name_index);
    _LOAD(p->is_a_name_index);

    _LOAD(p->_io_file_class_idx);
    _LOAD(p->_net_stream_class_idx);
    _LOAD(p->_urilib_uri_class_idx);

    _LOAD(p->globalvar_count);
    {
        _LOADSIZEALLOCZEROED(
            p->globalvar,
            sizeof(*p->globalvar) * p->globalvar_count
        );
        globalvarid_t i = 0;
        while (i < p->globalvar_count) {
            h64globalvar *gv = &p->globalvar[i];
            _LOAD(gv->content.type);
            if (gv->content.type == H64VALTYPE_NONE ||
                    gv->content.type == H64VALTYPE_UNSPECIFIED_KWARG) {
                // Nothing to do.
            } else if (gv->content.type == H64VALTYPE_BOOL ||
                    gv->content.type == H64VALTYPE_INT64 ||
                    gv->content.type == H64VALTYPE_FUNCREF ||
                    gv->content.type == H64VALTYPE_CLASSREF) {
                _LOAD(gv->content.int_value);
            } else if (gv->content.type == H64VALTYPE_FLOAT64) {
                _LOAD(gv->content.float_value);
            } else if (gv->content.type == H64VALTYPE_SHORTBYTES) {
                _LOAD(gv->content.shortbytes_len);
                _LOADSIZE(
                    gv->content.shortbytes_value,
                    gv->content.shortbytes_len
                );
            } else if (gv->content.type ==
                       H64VALTYPE_CONSTPREALLOCBYTES) {
                _LOAD(gv->content.constpreallocbytes_len);
                _LOADSIZEALLOC(
                    gv->content.constpreallocbytes_value,
                    gv->content.constpreallocbytes_len
                );
            } else if (gv->content.type == H64VALTYPE_SHORTSTR) {
                _LOAD(gv->content.shortstr_len);
                _LOADSIZE(
                    gv->content.shortstr_value,
                    gv->content.shortstr_len *
                        sizeof(h64wchar)
                );
            } else if (gv->content.type ==
                       H64VALTYPE_CONSTPREALLOCSTR) {
                _LOAD(gv->content.constpreallocstr_len);
                _LOADSIZEALLOC(
                    gv->content.constpreallocstr_value,
                    gv->content.constpreallocstr_len *
                        sizeof(h64wchar)
                );
            } else {
                fprintf(stderr,
                    "horse64/bytecodeserialize.c: error: "
                    "unsupported preset global var type %d\n",
                    gv->content.type
                );
            }
            _LOAD(gv->is_simple_constant);
            _LOAD(gv->is_const);
            i++;
        }
    }

    // Now we need to map back all C funcs to the right pointers:
    {
        funcid_t i = 0;
        while (i < p->func_count) {
            if (p->func[i].iscfunc) {
                if (!p->func[i].cfunclookup)
                    goto loadfail;
                assert(!p->func[i].cfunc_ptr);
                funcid_t i2 = 0;
                while (i2 < write_to->func_count) {
                    if (!write_to->func[i2].iscfunc) {
                        i2++;
                        continue;
                    }
                    assert(write_to->func[i2].cfunclookup != NULL);
                    if (strcmp(write_to->func[i2].cfunclookup,
                            p->func[i].cfunclookup) == 0) {
                        p->func[i].cfunc_ptr = (
                            write_to->func[i2].cfunc_ptr
                        );
                    }
                    i2++;
                }
            }
            i++;
        }
    }

    // Recompute the hash maps for class attributes:
    {
        classid_t i = 0;
        while (i < p->classes_count) {
            if (!h64program_RebuildClassAttributeHashmap(
                    p, i
                    )) {
                goto loadfail;
            }
            i++;
        }
    }

    // Free old program and copy over new one:
    if (*write_to_ptr)
        h64program_Free(*write_to_ptr);
    if (write_to && alwaysfree_writeto)
        h64program_Free(write_to);
    *write_to_ptr = p;

    return 1;
}