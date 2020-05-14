
#include <assert.h>
#include <check.h>
#include <stdarg.h>
#include <stdlib.h>

#define TESTS_MAIN(...) \
void addtests(TCase *tc, ...) {\
    va_list vl;\
    va_start(vl, tc);\
    while (1) {\
        void *ptr = va_arg(vl, void*);\
        if (!ptr)\
            break;\
        tcase_add_test(tc, (const void *)ptr);\
    }\
    va_end(vl);\
}\
int main(void)\
{\
    Suite *s1 = suite_create("Core");\
    TCase *tc1_1 = tcase_create("Core");\
    SRunner *sr = srunner_create(s1);\
    int nf;\
\
    suite_add_tcase(s1, tc1_1);\
    addtests(tc1_1, __VA_ARGS__, NULL);\
\
    srunner_run_all(sr, CK_ENV);\
    nf = srunner_ntests_failed(sr);\
    srunner_free(sr);\
\
    return nf == 0 ? 0 : 1;\
}
