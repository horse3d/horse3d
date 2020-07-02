#ifndef HORSE64_JSON_H_
#define HORSE64_JSON_H_

#include <stdarg.h>
#include <stdint.h>
#if defined(_WIN32) || defined(_WIN64)
#include <basetsd.h>
#else
#include <sys/types.h>
#endif

#define JSON_VALUE_NULL 0
#define JSON_VALUE_INT 1
#define JSON_VALUE_FLOAT 2
#define JSON_VALUE_STR 3
#define JSON_VALUE_BOOL 4
#define JSON_VALUE_LIST 5
#define JSON_VALUE_OBJECT 6


typedef struct jsonvalue jsonvalue;


struct jsonlist {
    jsonvalue **contents;
    int count;
};


struct jsonobject {
    char **keys;
    jsonvalue **values;
    int count;
};


typedef struct jsonvalue {
    int type;
    union {
        int64_t value_int;
        double value_float;
        char *value_str;
        int value_bool;
        struct jsonlist value_list;
        struct jsonobject value_object;       
    };
} jsonvalue;


jsonvalue *json_Parse(const char *s);

jsonvalue *json_Dict();

jsonvalue *json_List();

int json_AddToList(jsonvalue *list, jsonvalue *value);

int json_AddToListStr(jsonvalue *list, const char *value);

int json_SetDict(jsonvalue *dict, const char *key, jsonvalue *value);

int json_SetDictStr(jsonvalue *dict, const char *key, const char *value);

int json_SetDictInt(jsonvalue *dict, const char *key, int value);

int json_SetDictFloat(jsonvalue *dict, const char *key, double value);

int json_SetDictNull(jsonvalue *dict, const char *key);

int json_SetDictBool(
    jsonvalue *dict, const char *key, int value
);

void json_Free(jsonvalue *v);

jsonvalue *json_GetNestedValue(jsonvalue *v, ...);

ssize_t json_GetNestedLength(jsonvalue *v, ...);

char *json_Dump(jsonvalue *jv);

#endif  // HORSE64_JSON_H_
