
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
#include "refval.h"
#include "stack.h"
#include "unicode.h"
#include "vmexec.h"


int corelib_print(h64vmthread *vmthread, int stackbottom) {
    if (stackbottom >= STACK_SIZE(vmthread->stack)) {
        return stderror(
            vmthread, H64STDERROR_ARGUMENTERROR,
            "missing argument for print call"
        );
    }
    char *buf = alloca(256);
    uint64_t buflen = 256;
    int buffree = 0;
    int i = stackbottom;
    while (i < STACK_SIZE(vmthread->stack)) {
        if (i > 0)
            printf(" ");
        valuecontent *c = STACK_ENTRY(vmthread->stack, i);
        switch (c->type) {
        case H64VALTYPE_REFVAL: ;
            h64refvalue *rval = c->ptr_value;
            switch (rval->type) {
            case H64REFVALTYPE_STRING:
                if (buflen < rval->str_val->len * 4 + 1) {
                    char *newbuf = malloc(
                        rval->str_val->len * 4 + 1
                    );
                    buffree = 1;
                    if (newbuf) {
                        if (buffree)
                            free(buf);
                        buf = newbuf;
                        buflen = rval->str_val->len * 4 + 1;
                    }
                    int64_t outlen = 0;
                    utf32_to_utf8(
                        rval->str_val->s, rval->str_val->len,
                        buf, buflen, &outlen
                    );
                    if (outlen >= (int64_t)buflen)
                        outlen = buflen - 1;
                    buf[outlen] = '\0';
                    printf("%s", buf);
                }
                break;
            case H64REFVALTYPE_SHORTSTR:
                assert(buflen >= 5);
                assert(rval->shortstr_len >= 0 &&
                       rval->shortstr_len < 5);
                memcpy(buf, rval->shortstr_val, rval->shortstr_len);
                buf[rval->shortstr_len] = '\0';
                printf("%s", buf);
                break;
            default:
                printf("<unhandled refvalue type=%d>\n", (int)rval->type);
            }
            break;
        default:
            printf("<unhandled valuecontent type=%d>\n", (int)c->type);
            break;
        }
        i++;
    }
    return 0;
}

int corelib_RegisterFuncs(h64program *p) {
    if (h64program_RegisterCFunction(
            p, "print", &corelib_print,
            NULL, 1, NULL, 1, NULL, 1, -1
            ) < 0)
        return 0;
    return 1;
}
