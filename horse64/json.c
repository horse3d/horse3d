// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nonlocale.h"
#include "json.h"


static void skipwhitespace(const char **sptr) {
    const char *s = *((char**)sptr);
    while (1) {
        if (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') {
            s++;
        } else if (s[0] == '/' && s[1] == '/') {
            s += 2;
            while (*s != '\0' && *s != '\r' && *s != '\n')
                s++;
            continue;
        } else {
            break;
        }
    }
    *((char**)sptr) = (char*)s;
}

int json_ParseEx(
        const char *jsons,
        jsonvalue **parsed_result,
        int *parsed_len,
        int max_depth
        ) {
    if (!jsons || strlen(jsons) == 0 || max_depth == 0)
        return 0;
    jsonvalue *jv = malloc(sizeof(*jv));
    if (!jv)
        return 0;
    memset(jv, 0, sizeof(*jv));
    const char *s = jsons;
    skipwhitespace(&s);
    if ((*s >= '0' && *s <= '9') ||
            (*s == '-' && (s[1] >= '0' && s[1] <= '9'))) {
        int dotsfound = 0;
        unsigned int k = 0;
        if (*s == '-')
            k++;
        unsigned int len = strlen(s);
        while (k < len) {
            if (s[k] == '.' && k > 0 && (
                    s[k + 1] >= '0' && s[k + 1] <= '9'
                    ) && (
                    s[k - 1] >= '0' && s[k - 1] <= '9'
                    )) {
                dotsfound++;
                if (dotsfound > 1) {
                    free(jv);
                    return 0;
                }
                k++;
                continue;
            } else if ((s[k] >= '0' && s[k] <= '9') && (
                       k + 1 >= strlen(s) ||
                       s[k + 1] == ' ' || s[k + 1] == '\t' ||
                       s[k + 1] == '\r' ||
                       s[k + 1] == '\n' ||
                       (s[k + 1] == '/' && s[k + 2] == '/') ||
                       s[k + 1] == ']' || s[k + 1] == '}' ||
                       s[k + 1] == ',')) {
                char *buf = malloc(k + 2);
                if (!buf) {
                    free(jv);
                    return 0;
                }
                memcpy(buf, s, k + 1);
                buf[k + 1] = '\0';
                if (dotsfound <= 0) {
                    jv->type = JSON_VALUE_INT;
                    jv->value_int = h64atoll(buf);
                } else {
                    jv->type = JSON_VALUE_FLOAT;
                    jv->value_float = h64atof(buf);
                }
                free(buf);
                s += k + 1;
                skipwhitespace(&s);
                if (parsed_len)
                    *parsed_len = (s - jsons);
                *parsed_result = jv;
                return 1;
            } else if (s[k] >= '0' && s[k] <= '9') {
                k++;
            } else {
                break;
            }
        }
        free(jv);
        return 0;
    } else if (
            (strlen(s) >= strlen("true") &&
             memcmp(s, "true", strlen("true")) == 0) ||
            (strlen(s) >= strlen("false") &&
             memcmp(s, "false", strlen("false")) == 0)
            ) {
        int is_true = 0;
        if (*s == 't') {
            is_true = 1;
            s += strlen("true");
        } else {
            s += strlen("false");
        }
        if (*s == '\0' || *s == ',' || *s == ' ' || *s == '\t' ||
                *s == '\r' || *s == '\n' || *s == '/' || *s == ']' ||
                *s == '}' || *s == ':') {
            skipwhitespace(&s);
            if (parsed_len)
                *parsed_len = (s - jsons);
            jv->value_bool = is_true;
            jv->type = JSON_VALUE_BOOL;
            *parsed_result = jv;
            return 1;
        }
        free(jv);
        return 0;
    } else if ((strlen(s) >= strlen("null") &&
            memcmp(s, "null", strlen("null")) == 0)) {
        s += strlen("null");
        if (*s == '\0' || *s == ',' || *s == ' ' || *s == '\t' ||
                *s == '\r' || *s == '\n' || *s == '/' || *s == ']' ||
                *s == '}' || *s == ':') {
            skipwhitespace(&s);
            if (parsed_len)
                *parsed_len = (s - jsons);
            jv->type = JSON_VALUE_NULL;
            *parsed_result = jv;
            return 1;
        }
        free(jv);
        return 0;
    } else if (*s == '"' || *s == '\'') {
        char *strbuf = malloc(32);
        int strbuf_fill = 0;
        int strbuf_alloc = 32;
        if (!strbuf) {
            free(jv);
            return 0;
        }
        char in_quotes = *s;
        s++;
        int backslash_escaped = 0;
        while ((*s != in_quotes || backslash_escaped)
               && *s != '\0') {
            if (backslash_escaped) {
                backslash_escaped = 0;
            } else {
                if (*s == '\\') {
                    backslash_escaped = 1;
                    s++;
                    continue;
                }
            }
            if (strbuf_fill + 1 >= strbuf_alloc) {
                char *newstrbuf = realloc(
                    strbuf, strbuf_alloc + 32
                );
                if (!newstrbuf) {
                    free(jv);
                    return 0;
                }
                strbuf = newstrbuf;
                strbuf_alloc += 32;
            }
            strbuf[strbuf_fill] = *s;
            strbuf_fill++;
            s++;
        }
        if (*s != in_quotes) {
            free(strbuf);
            free(jv);
            return 0;
        }
        strbuf[strbuf_fill] = '\0';
        s++;
        skipwhitespace(&s);
        if (parsed_len)
            *parsed_len = (s - jsons);
        jv->type = JSON_VALUE_STR;
        jv->value_str = strbuf;
        *parsed_result = jv;
        return 1;
    } else if (*s == '[') {
        jv->type = JSON_VALUE_LIST;
        jv->value_list.count = 0;
        jv->value_list.contents = malloc(
            sizeof(*jv->value_list.contents) * 16
        );
        if (!jv->value_list.contents) {
            free(jv);
            return 0;
        }
        int alloc_entries = 16;
        s++;
        while (*s != ']' && *s != '\0') {
            skipwhitespace(&s);
            int parsedlen = 0;
            jsonvalue *result = NULL;
            if (!json_ParseEx(
                    s, &result, &parsedlen, max_depth - 1)) {
                json_Free(jv);
                return 0;
            }
            if (jv->value_list.count >= alloc_entries) {
                jsonvalue **newcontents = malloc(
                    sizeof(*jv->value_list.contents) * (alloc_entries + 16)
                );
                if (!newcontents) {
                    json_Free(result);
                    json_Free(jv);
                    return 0;
                }
                alloc_entries += 16;
                memcpy(
                    newcontents, jv->value_list.contents,
                    sizeof(*jv->value_list.contents) * jv->value_list.count
                );
                free(jv->value_list.contents);
                jv->value_list.contents = newcontents;
            }
            jv->value_list.contents[jv->value_list.count] = result;
            jv->value_list.count++;
            s += parsedlen;
            skipwhitespace(&s);
            if (*s != ',' && *s != ']') {
                json_Free(jv);
                return 0;
            }
            if (*s == ',')
                s++;
        }
        if (*s != ']') {
            json_Free(jv);
        }
        s++;
        skipwhitespace(&s);
        if (parsed_len)
            *parsed_len = (s - jsons);
        *parsed_result = jv;
        assert(jv->value_list.contents || jv->value_list.count == 0);
        return 1;
     } else if (*s == '{') {
        jv->type = JSON_VALUE_OBJECT;
        jv->value_object.count = 0;
        jv->value_object.values = malloc(
            sizeof(*jv->value_object.values) * 16
        );
        if (!jv->value_object.values) {
            free(jv);
            return 0;
        }
        jv->value_object.keys = malloc(
            sizeof(*jv->value_object.keys) * 16
        );
        if (!jv->value_object.keys) {
            free(jv->value_object.values);
            free(jv);
            return 0;
        }
        int alloc_entries = 16;
        s++;
        while (*s != '}' && *s != '\0') {
            skipwhitespace(&s);
            int parsedlen = 0;
            jsonvalue *result = NULL;
            if (!json_ParseEx(
                    s, &result, &parsedlen, max_depth - 1)) {
                json_Free(jv);
                return 0;
            }
            if (result->type != JSON_VALUE_STR) {
                json_Free(result);
                json_Free(jv);
                return 0;
            }
            if (jv->value_object.count >= alloc_entries) {
                char **newkeys = malloc(
                    sizeof(*jv->value_object.keys) * (alloc_entries + 16)
                );
                if (!newkeys) {
                    json_Free(result);
                    json_Free(jv);
                    return 0;
                }
                jsonvalue **newvalues = malloc(
                    sizeof(*jv->value_object.values) * (alloc_entries + 16)
                );
                if (!newvalues) {
                    free(newkeys);
                    json_Free(result);
                    json_Free(jv);
                    return 0;
                }
                alloc_entries += 16;
                memcpy(
                    newkeys, jv->value_object.keys,
                    sizeof(*jv->value_object.keys) * jv->value_object.count
                );
                memcpy(
                    newvalues, jv->value_object.values,
                    sizeof(*jv->value_object.values) * jv->value_object.count
                );
                free(jv->value_object.keys);
                free(jv->value_object.values);
                jv->value_object.keys = newkeys;
                jv->value_object.values = newvalues;
            }
            s += parsedlen;
            skipwhitespace(&s);
            if (*s != ':') {
                json_Free(jv);
                return 0;
            }
            s++;
            skipwhitespace(&s);
            parsedlen = 0;
            jsonvalue *result_value = NULL;
            if (!json_ParseEx(
                    s, &result_value, &parsedlen, max_depth - 1)) {
                json_Free(result);
                json_Free(jv);
                return 0;
            }
            jv->value_object.keys[jv->value_object.count] = (
                result->value_str
            );
            result->value_str = NULL;
            free(result);
            jv->value_object.values[jv->value_object.count] = result_value;
            jv->value_object.count++;
            s += parsedlen;
            skipwhitespace(&s);
            if (*s != ',' && *s != '}') {
                json_Free(jv);
                return 0;
            }
            if (*s == ',')
                s++;
        }
        if (*s != '}') {
            json_Free(jv);
        }
        s++;
        skipwhitespace(&s);
        if (parsed_len)
            *parsed_len = (s - jsons);
        *parsed_result = jv;
        return 1;
   } else {
        free(jv);
        return 0;
    }
}


void json_Free(jsonvalue *jv) {
    if (!jv)
        return;
    if (jv->type == JSON_VALUE_STR) {
        if (jv->value_str)
            free(jv->value_str);
    } else if (jv->type == JSON_VALUE_LIST) {
        int i = 0;
        while (i < jv->value_list.count) {
            json_Free(jv->value_list.contents[i]);
            i++;
        }
        free(jv->value_list.contents);
    } else if (jv->type == JSON_VALUE_OBJECT) {
        int i = 0;
        while (i < jv->value_object.count) {
            free(jv->value_object.keys[i]);
            json_Free(jv->value_object.values[i]);
            i++;
        }
        free(jv->value_object.keys);
        free(jv->value_object.values);
    }
    free(jv);
}


jsonvalue *json_Parse(const char *jsons) {
    jsonvalue *result = NULL;
    int parsedlen = 0;
    if (!json_ParseEx(
            jsons, &result, &parsedlen, 20))
        return NULL;
    if (parsedlen < (int)strlen(jsons)) {
        json_Free(result);
        return NULL;
    }
    return result;
}

jsonvalue *json_Dict() {
    jsonvalue *jv = malloc(sizeof(*jv));
    if (!jv)
        return NULL;
    memset(jv, 0, sizeof(*jv));
    jv->type = JSON_VALUE_OBJECT;
    return jv;
}

jsonvalue *json_List() {
    jsonvalue *jv = malloc(sizeof(*jv));
    if (!jv)
        return NULL;
    memset(jv, 0, sizeof(*jv));
    jv->type = JSON_VALUE_LIST;
    return jv;
}

int json_AddToListStr(
        jsonvalue *list, const char *value
        ) {
    if (!list || list->type != JSON_VALUE_LIST || !value)
        return 0;

    jsonvalue *svalue = malloc(sizeof(*svalue));
    if (!svalue)
        return 0;
    memset(svalue, 0, sizeof(*svalue));
    svalue->type = JSON_VALUE_STR;
    svalue->value_str = strdup(value);
    if (!svalue->value_str) {
        json_Free(svalue);
        return 0;
    }

    if (!json_AddToList(list, svalue)) {
        json_Free(svalue);
        return 0;
    }
    return 1;
}  

int json_AddToList(
        jsonvalue *list, jsonvalue *value
        ) {
    if (!list || list->type != JSON_VALUE_LIST || !value)
        return 0;

    jsonvalue **new_contents = realloc(
        list->value_list.contents,
        sizeof(*new_contents) * (list->value_list.count + 1)
    );
    if (!new_contents)
        return 0;
    list->value_list.contents = new_contents;

    list->value_list.contents[list->value_list.count] = value;
    list->value_list.count++;
    return 1;
}

int json_SetDict(
        jsonvalue *dict, const char *key, jsonvalue *value
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT || !value)
        return 0;

    char **new_keys = realloc(
        dict->value_object.keys,
        sizeof(*new_keys) * (dict->value_object.count + 1)
    );
    if (!new_keys)
        return 0;
    dict->value_object.keys = new_keys;

    jsonvalue **new_objects = realloc(
        dict->value_object.values,
        sizeof(*new_objects) * (dict->value_object.count + 1)
    );
    if (!new_objects)
        return 0;
    dict->value_object.values = new_objects;

    dict->value_object.keys[dict->value_object.count] = strdup(key);
    if (!dict->value_object.keys[dict->value_object.count])
        return 0;
    dict->value_object.values[dict->value_object.count] = value;
    dict->value_object.count++;
    return 1;
}

int json_SetDictNull(
        jsonvalue *dict, const char *key
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT)
        return 0;

    jsonvalue *valueobj = malloc(sizeof(*valueobj));
    if (!valueobj)
        return 0;
    memset(valueobj, 0, sizeof(*valueobj));
    valueobj->type = JSON_VALUE_NULL;

    if (!json_SetDict(dict, key, valueobj)) {
        json_Free(valueobj);
        return 0;
    }
    return 1;
}

int json_SetDictBool(
        jsonvalue *dict, const char *key, int value
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT)
        return 0;

    jsonvalue *valueobj = malloc(sizeof(*valueobj));
    if (!valueobj)
        return 0;
    memset(valueobj, 0, sizeof(*valueobj));
    valueobj->type = JSON_VALUE_BOOL;
    valueobj->value_bool = (value != 0);

    if (!json_SetDict(dict, key, valueobj)) {
        json_Free(valueobj);
        return 0;
    }
    return 1;
}

int json_SetDictStr(
        jsonvalue *dict, const char *key, const char *value
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT || !value)
        return 0;

    jsonvalue *valueobj = malloc(sizeof(*valueobj));
    if (!valueobj)
        return 0;
    memset(valueobj, 0, sizeof(*valueobj));
    valueobj->type = JSON_VALUE_STR;
    valueobj->value_str = strdup(value);
    if (!valueobj->value_str) {
        json_Free(valueobj);
        return 0;
    }

    if (!json_SetDict(dict, key, valueobj)) {
        json_Free(valueobj);
        return 0;
    }
    return 1;
}

int json_SetDictFloat(
        jsonvalue *dict, const char *key, double value
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT)
        return 0;

    jsonvalue *valueobj = malloc(sizeof(*valueobj));
    if (!valueobj)
        return 0;
    memset(valueobj, 0, sizeof(*valueobj));
    valueobj->type = JSON_VALUE_FLOAT;
    valueobj->value_float = value;

    if (!json_SetDict(dict, key, valueobj)) {
        json_Free(valueobj);
        return 0;
    }
    return 1;
}

int json_SetDictInt(
        jsonvalue *dict, const char *key, int64_t value
        ) {
    if (!dict || dict->type != JSON_VALUE_OBJECT)
        return 0;

    jsonvalue *valueobj = malloc(sizeof(*valueobj));
    if (!valueobj)
        return 0;
    memset(valueobj, 0, sizeof(*valueobj));
    valueobj->type = JSON_VALUE_INT;
    valueobj->value_int = value;

    if (!json_SetDict(dict, key, valueobj)) {
        json_Free(valueobj);
        return 0;
    }
    return 1;
}


char *json_Dump(jsonvalue *jv) {
    if (!jv)
        return NULL;
    if (jv->type == JSON_VALUE_STR) {
        if (!jv->value_str)
            return NULL;
        char *resultbuf = malloc(
            3 + strlen(jv->value_str) * 6
        );
        if (!resultbuf)
            return NULL;
        resultbuf[0] = '"';
        int fill = 1;
        int i = 0;
        while (i < (int)strlen(jv->value_str)) {
            char c = jv->value_str[i];
            if (c == '\\' || c == '"' || c == '\n' || c == '\r') {
                resultbuf[fill] = '\\';
                fill++;
                if (c == '\n')
                    c = 'n';
                else if (c == '\r')
                    c = 'r';
            } else if (c < 32) {
                char escaped[10] = "";
                snprintf(
                    escaped, sizeof(escaped) - 1,
                    "00%x", c
                );
                while (strlen(escaped) < 4) {
                    memmove(escaped + 1, escaped, strlen(escaped) + 1);
                    escaped[0] = '0';
                }
                memcpy(resultbuf + fill, "\\u", strlen("\\u"));
                fill += strlen("\\u");
                memcpy(resultbuf + fill, escaped, strlen(escaped));
                fill += strlen(escaped);
                i++;
                continue;
            }
            resultbuf[fill] = c;
            fill++;
            i++;
        }
        resultbuf[fill] = '"';
        resultbuf[fill + 1] = '\0';
        return resultbuf;
    } else if (jv->type == JSON_VALUE_LIST) {
        assert(jv->value_list.contents || jv->value_list.count == 0);
        char *resultbuf = malloc(strlen("[") + 2);
        if (!resultbuf)
            return NULL;
        resultbuf[0] = '[';
        resultbuf[1] = '\0';
        int i = 0;
        while (i < jv->value_list.count) {
            char *s = json_Dump(jv->value_list.contents[i]);
            if (!s) {
                free(resultbuf);
                return NULL;
            }
            char *newresultbuf = realloc(
                resultbuf, strlen(resultbuf) + strlen(s) + 3
            );
            if (!newresultbuf) {
                free(resultbuf);
                free(s);
                return NULL;
            }
            resultbuf = newresultbuf;
            memcpy(resultbuf + strlen(resultbuf),
                   s, strlen(s) + 1);
            free(s);
            if (i + 1 < jv->value_list.count) {
                resultbuf[strlen(resultbuf) + 1] = '\0';
                resultbuf[strlen(resultbuf)] = ',';
            }
            i++;
        }
        resultbuf[strlen(resultbuf) + 1] = '\0';
        resultbuf[strlen(resultbuf)] = ']';
        return resultbuf;
    } else if (jv->type == JSON_VALUE_OBJECT) {
        char *resultbuf = malloc(strlen("{") + 2);
        if (!resultbuf)
            return NULL;
        resultbuf[0] = '{';
        resultbuf[1] = '\0';
        int i = 0;
        while (i < jv->value_object.count) {
            jsonvalue keyvalue;
            memset(&keyvalue, 0, sizeof(keyvalue));
            keyvalue.type = JSON_VALUE_STR;
            keyvalue.value_str = strdup(jv->value_object.keys[i]);
            if (!keyvalue.value_str) {
                free(resultbuf);
                return NULL;
            }
            char *keys = json_Dump(&keyvalue);
            free(keyvalue.value_str);
            if (!keys) {
                free(resultbuf);
                return NULL;
            }
            char *s = json_Dump(jv->value_object.values[i]);
            if (!s) {
                free(keys);
                free(resultbuf);
                return NULL;
            }
            char *newresultbuf = realloc(
                resultbuf, strlen(resultbuf) + strlen(keys) +
                1 + strlen(s) + 3
            );
            if (!newresultbuf) {
                free(resultbuf);
                free(keys);
                free(s);
                return NULL;
            }
            resultbuf = newresultbuf;
            memcpy(resultbuf + strlen(resultbuf),
                   keys, strlen(keys) + 1);
            resultbuf[strlen(resultbuf) + 1] = '\0';
            resultbuf[strlen(resultbuf)] = ':';
            memcpy(resultbuf + strlen(resultbuf),
                   s, strlen(s) + 1);
            free(keys);
            free(s);
            if (i + 1 < jv->value_object.count) {
                resultbuf[strlen(resultbuf) + 1] = '\0';
                resultbuf[strlen(resultbuf)] = ',';
            }
            i++;
        }
        resultbuf[strlen(resultbuf) + 1] = '\0';
        resultbuf[strlen(resultbuf)] = '}';
        return resultbuf;
    } else if (jv->type == JSON_VALUE_INT) {
        char resultbuf[128];
        snprintf(resultbuf, sizeof(resultbuf) - 1,
                 "%" PRId64, jv->value_int);
        return strdup(resultbuf);
    } else if (jv->type == JSON_VALUE_FLOAT) {
        char resultbuf[128];
        snprintf(resultbuf, sizeof(resultbuf) - 1,
                 "%f", jv->value_float);
        return strdup(resultbuf);
    } else if (jv->type == JSON_VALUE_BOOL) {
        if (jv->value_bool)
            return strdup("true");
        return strdup("false");
    } else if (jv->type == JSON_VALUE_NULL) {
        return strdup("null");
    }
    return NULL;
}


jsonvalue *_GoToNested(jsonvalue *jv, va_list vl) {
    while (1) {
        char *val = va_arg(vl, char*);
        if (!val) {
            break;
        }
        if (jv->type != JSON_VALUE_OBJECT &&
                jv->type != JSON_VALUE_LIST) {
            failure:
            while (va_arg(vl, char*))
                continue;
            va_end(vl);
            return NULL;
        }
        if (jv->type == JSON_VALUE_LIST) {
            assert(jv->value_list.contents || jv->value_list.count == 0);
            int valid = (strlen(val) >= 1);
            int i = 0;
            while (i < (int)strlen(val)) {
                if (val[i] >= '0' && val[i] <= '9') {
                    i++;
                    continue;
                }
                valid = 0;
                break;
            }
            if (!valid)
                goto failure;
            int64_t index = h64atoll(val);
            if (index < 0 || index >= jv->value_list.count)
                goto failure;
            jv = jv->value_list.contents[index];
            assert(jv);
        } else if (jv->type == JSON_VALUE_OBJECT) {
            int found = 0;
            int k = 0;
            while (k < jv->value_object.count) {
                if (strcmp(jv->value_object.keys[k], val) == 0) {
                    jv = jv->value_object.values[k];
                    found = 1;
                    break;
                }
                k++;
            }
            if (!found)
                goto failure;
        }
    }
    va_end(vl);
    return jv;
}


jsonvalue *json_GetNestedValue(jsonvalue *jv, ...) {
    va_list args;
    va_start(args, jv);
    return _GoToNested(jv, args);
}


ssize_t json_GetNestedLength(jsonvalue *jv, ...) {
    va_list args;
    va_start(args, jv);
    jsonvalue *item = _GoToNested(jv, args);
    if (!item)
        return -1;
    if (item->type == JSON_VALUE_OBJECT) {
        return item->value_object.count;
    } else if (item->type == JSON_VALUE_LIST) {
        return item->value_list.count;
    } else {
        return -1;
    }
}
