
#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "corelib/moduleless.h"
#include "gcvalue.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"
#include "vmlist.h"


int corelib_containeradd(  // $$builtin.$$containeradd
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) >= 2);

    valuecontent *vc = STACK_ENTRY(vmthread->stack, 0);
    assert(vc->type == H64VALTYPE_GCVAL);
    h64gcvalue *gcvalue = (h64gcvalue *)vc->ptr_value;
    if (gcvalue->type == H64GCVALUETYPE_LIST) {
        if (!vmlist_Add(
                gcvalue->list_values, STACK_ENTRY(vmthread->stack, 1)
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

int corelib_print(  // $$builtin.print
        h64vmthread *vmthread
        ) {
    assert(STACK_TOP(vmthread->stack) == 1);
    assert(STACK_ENTRY(vmthread->stack, 0)->type == H64VALTYPE_GCVAL &&
        ((h64gcvalue*)STACK_ENTRY(vmthread->stack, 0)->ptr_value)->type ==
        H64GCVALUETYPE_LIST
    );
    char *buf = alloca(256);
    uint64_t buflen = 256;
    int buffree = 0;
    int i = 0;
    while (i < STACK_TOP(vmthread->stack)) {
        if (i > 0)
            printf(" ");
        valuecontent *c = STACK_ENTRY(vmthread->stack, i);
        switch (c->type) {
        case H64VALTYPE_GCVAL: ;
            h64gcvalue *gcval = c->ptr_value;
            switch (gcval->type) {
            case H64GCVALUETYPE_STRING:
                if (buflen < gcval->str_val.len * 4 + 1) {
                    char *newbuf = malloc(
                        gcval->str_val.len * 4 + 1
                    );
                    buffree = 1;
                    if (newbuf) {
                        if (buffree)
                            free(buf);
                        buf = newbuf;
                        buflen = gcval->str_val.len * 4 + 1;
                    }
                    int64_t outlen = 0;
                    int result = utf32_to_utf8(
                        gcval->str_val.s, gcval->str_val.len,
                        buf, buflen, &outlen, 1
                    );
                    assert(result != 0);
                    if (outlen >= (int64_t)buflen)
                        outlen = buflen - 1;
                    buf[outlen] = '\0';
                    printf("%s", buf);
                }
                break;
            default:
                printf("<unhandled refvalue type=%d>\n", (int)gcval->type);
            }
            break;
        case H64VALTYPE_SHORTSTR:
            assert(buflen >= 25);
            assert(c->shortstr_len >= 0 &&
                   c->shortstr_len < 5);
            unicodechar shortstr_value[
                VALUECONTENT_SHORTSTRLEN + 1
            ];
            memcpy(&shortstr_value, c->shortstr_value,
                   VALUECONTENT_SHORTSTRLEN + 1);
            int64_t outlen = 0;
            int result = utf32_to_utf8(
                shortstr_value, c->shortstr_len,
                buf, 25, &outlen, 1
            );
            assert(result != 0 && outlen > 0 && outlen < 25);
            buf[outlen - 1] = '\0';
            printf("%s", buf);
            break;
        default:
            printf("<unhandled valuecontent type=%d>\n", (int)c->type);
            break;
        }
        i++;
    }
    return 1;
}

int corelib_RegisterFuncs(h64program *p) {
    int64_t idx;
    idx = h64program_RegisterCFunction(
        p, "print", &corelib_print,
        NULL, 1, NULL, 1, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->print_func_index = idx;
    idx = h64program_RegisterCFunction(
        p, "$$containeradd", &corelib_containeradd,
        NULL, 1, NULL, 0, NULL, NULL, 1, -1
    );
    if (idx < 0)
        return 0;
    p->func[idx].input_stack_size++;  // for 'self'
    p->containeradd_func_index = idx;
    return 1;
}
