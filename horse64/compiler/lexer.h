#ifndef HORSE3D_COMPILER_LEXER_H_
#define HORSE3D_COMPILER_LEXER_H_

#include <stdint.h>
#include <stdlib.h>  // for NULL

#include "compiler/result.h"
#include "json.h"

typedef enum h64tokentype {
    H64TK_INVALID = 0,
    H64TK_IDENTIFIER = 1,
    H64TK_BRACKET,
    H64TK_COMMA,
    H64TK_COLON,
    H64TK_KEYWORD,
    H64TK_CONSTANT_INT,
    H64TK_CONSTANT_FLOAT,
    H64TK_CONSTANT_BOOL,
    H64TK_CONSTANT_NONE,
    H64TK_CONSTANT_STRING,
    H64TK_BINOPSYMBOL,
    H64TK_UNOPSYMBOL,
    H64TK_INLINEFUNC,
    H64TK_MAPARROW
} h64tokentype;

typedef struct h64token {
    h64tokentype type;
    union {
        double float_value;
        int64_t int_value;
        char *str_value;
        uint8_t char_value;
    };
    int64_t line, column;
} h64token;

typedef struct h64tokenizedfile {
    h64result resultmsg;
    int token_count;
    h64token *token;
} h64tokenizedfile;

static char *h64keywords[] = {
    "threadable", "const",
    "if", "while", "func",
    "var", "class", "extends",
    "import", "else", "elseif",
    "break", "continue", "try",
    "catch", "finally", "error",
    "self", "base", "new",
    "return", "from",
    NULL
};

h64tokenizedfile lexer_ParseFromFile(
    const char *fileuri
);

void lexer_ClearToken(h64token *t);

void lexer_FreeFileTokens(h64tokenizedfile *tfile);

const char *lexer_TokenTypeToStr(h64tokentype type);

jsonvalue *lexer_TokenToJSON(h64token *t, const char *fileuri);

int is_valid_utf8_char(
    const unsigned char *p, int size
);

int utf8_char_len(const unsigned char *p);

#endif  // HORSE3D_COMPILER_LEXER_H_
