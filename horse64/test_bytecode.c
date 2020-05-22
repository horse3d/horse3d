#include <assert.h>
#include <check.h>

#include "bytecode.h"
#include "corelib/errors.h"
#include "debugsymbols.h"

#include "testmain.h"

START_TEST (test_bytecode)
{
    h64program *p = h64program_New();

    ck_assert(p != NULL && p->symbols != NULL);
    h64modulesymbols *msymbols = (
        h64debugsymbols_GetBuiltinModule(p->symbols)
    );
    ck_assert(p->classes_count > 0 && msymbols != NULL &&
              p->classes_count == msymbols->classes_count);
    int i = 0;
    while (i < H64STDERROR_TOTAL_COUNT) {
        ck_assert(i < p->classes_count);
        ck_assert(strcmp(msymbols->classes_symbols[i].name,
                         stderrorclassnames[i]) == 0);
        i++;
    }

    h64program_Free(p);
}
END_TEST

TESTS_MAIN(test_bytecode)
