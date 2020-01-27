#include <assert.h>
#include <check.h>
#include <math.h>
#include <stdio.h>

#include "compiler/lexer.h"
#include "vfs.h"

#include "../testmain.h"

START_TEST (test_intliterals)
{
    vfs_Init(NULL);

    FILE *f = fopen(".testdata.txt", "wb");
    ck_assert(f != NULL);
    char s[] = "1.5 + 0xA + 0b10";
    ck_assert(fwrite(s, 1, strlen(s), f));
    fclose(f);
    h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
    ck_assert(tfile.token_count == 5);
    ck_assert(tfile.token[0].type == H3DTK_CONSTANT_FLOAT);
    ck_assert(tfile.token[2].type == H3DTK_CONSTANT_INT);
    ck_assert(tfile.token[4].type == H3DTK_CONSTANT_INT);
    ck_assert(fabs(tfile.token[0].float_value - 1.5) < 0.001);
    ck_assert(tfile.token[2].int_value == 10);
    ck_assert(tfile.token[4].int_value == 2);
    lexer_FreeFileTokens(&tfile);
    result_FreeContents(&tfile.resultmsg);
}
END_TEST

START_TEST (test_unaryminus)
{
    vfs_Init(NULL);

    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "-10";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);

        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_CONSTANT_INT);
        ck_assert(tfile.token[0].int_value == -10);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }

    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "1-10";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);

        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.token_count == 3);
        ck_assert(tfile.token[0].type == H3DTK_CONSTANT_INT);
        ck_assert(tfile.token[0].int_value == 1);
        ck_assert(tfile.token[2].type == H3DTK_CONSTANT_INT);
        ck_assert(tfile.token[2].int_value == 10);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
}
END_TEST

START_TEST (test_utf8_literal)
{
    vfs_Init(NULL);
   
    ck_assert(is_valid_utf8_char("\xc3\xb6", 2));
    ck_assert(!is_valid_utf8_char("\xc3\xc3", 2));
    ck_assert(utf8_char_len("\xc3") == 2);

    FILE *f = fopen(".testdata.txt", "wb");
    ck_assert(f != NULL);
    char s[] = "v\xc3\xb6";
    ck_assert(fwrite(s, 1, strlen(s), f));
    fclose(f);
    h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
    ck_assert(tfile.resultmsg.success);
    ck_assert(tfile.token_count == 1);
    lexer_FreeFileTokens(&tfile);
    result_FreeContents(&tfile.resultmsg);
}
END_TEST

START_TEST (test_separation)
{
    vfs_Init(NULL);

    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "false";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_CONSTANT_BOOL);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "falseP";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_IDENTIFIER);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "var";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_KEYWORD);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "varP";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_IDENTIFIER);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
}
END_TEST

START_TEST (test_stringliterals)
{
    vfs_Init(NULL);

    {
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "(\"test string\x32with\nthings\\\\\")";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 3);
        ck_assert(tfile.token[1].type == H3DTK_CONSTANT_STRING);
        ck_assert(
            strcmp(tfile.token[1].str_value, "test string2with\nthings\\") == 0
        );
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
    {  // valid utf-8:
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "\"\xc3\xb6\"";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_CONSTANT_STRING);
        ck_assert(
            strcmp(tfile.token[0].str_value, "\xc3\xb6") == 0
        );
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
    {  // invalid utf-8:
        FILE *f = fopen(".testdata.txt", "wb");
        ck_assert(f != NULL);
        char s[] = "\"\xc3\xc3\"";
        ck_assert(fwrite(s, 1, strlen(s), f));
        fclose(f);
        h3dtokenizedfile tfile = h3dtokenize(".testdata.txt");
        ck_assert(!tfile.resultmsg.success);
        ck_assert(tfile.token_count == 1);
        ck_assert(tfile.token[0].type == H3DTK_INVALID);
        lexer_FreeFileTokens(&tfile);
        result_FreeContents(&tfile.resultmsg);
    }
}
END_TEST

TESTS_MAIN(test_intliterals, test_separation, test_utf8_literal, test_unaryminus, test_stringliterals)
