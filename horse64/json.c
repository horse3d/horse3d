
#include <assert.h>
#include <inttypes.h>
#include <lua.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "luamem.h"


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
                    jv->value_int = atoll(buf);
                } else {
                    jv->type = JSON_VALUE_FLOAT;
                    jv->value_float = atof(buf);
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
        jsonvalue *dict, const char *key, int value
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
        char *resultbuf = malloc(3 + strlen(jv->value_str) * 2);
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
            int64_t index = atoll(val);
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

char *json_EncodedStrFromStackEx(
        lua_State *l, int valueindex, int maxdepth, char **error
        ) {
    if (error)
        *error = NULL;
    if (!luamem_EnsureFreePools(l) || maxdepth <= 0) {
        if (maxdepth <= 0) {
            if (error)
                *error = strdup("table recursing too deep, cannot encode");
        } else {
            if (error)
                *error = strdup("out of memory");
        }
        return NULL;
    }
    if (valueindex < 0)
        valueindex = lua_gettop(l) + 1 - valueindex;
    if (valueindex < 1 || valueindex >= lua_gettop(l)) {
        if (error)
            *error = strdup("internal error: invalid stack index");
        return NULL;
    }

    char *encoded = NULL;
    jsonvalue *v = malloc(sizeof(*v));
    if (!v) {
        outofmemfail:
        if (encoded)
            free(encoded);
        if (v)
            json_Free(v);
        if (error)
            *error = strdup("out of memory");
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    if (lua_type(l, valueindex) == LUA_TSTRING) {
        v->type = JSON_VALUE_STR;
        v->value_str = strdup(lua_tostring(l, valueindex));
        if (!v->value_str)
            goto outofmemfail;
        encoded = json_Dump(v);
        if (!encoded)
            goto outofmemfail;
        json_Free(v);
        return encoded;
    } else if (lua_type(l, valueindex) == LUA_TNUMBER) {
        v->type = JSON_VALUE_FLOAT;
        v->value_float = lua_tonumber(l, valueindex);
        encoded = json_Dump(v);
        if (!encoded)
            goto outofmemfail;
        json_Free(v);
        return encoded;
    } else if (lua_type(l, valueindex) == LUA_TBOOLEAN) {
        v->type = JSON_VALUE_BOOL;
        v->value_bool = lua_toboolean(l, valueindex);
        encoded = json_Dump(v);
        if (!encoded)
            goto outofmemfail;
        json_Free(v);
        return encoded;
    } else if (lua_type(l, valueindex) == LUA_TNIL) {
        json_Free(v);
        return strdup("null");
    } else if (lua_type(l, valueindex) == LUA_TTABLE) {
        assert(encoded == NULL);
        int is_first_value = 1;
        int got_key_value = 0;
        while (lua_next(l, valueindex) != 0) {
            if (lua_rawlen(l, valueindex) > 0) {
                lua_pop(l, 2);  // remove iterated key + value
                if (error) {
                    char buf[512];
                    snprintf(
                        buf, sizeof(buf) - 1,
                        "table %p has both key value items "
                        "and list entries, cannot serialize",
                        lua_topointer(l, valueindex)
                    );
                    *error = strdup(buf);
                }
                return NULL;
            }
            if (encoded == NULL) {
                encoded = strdup("{");
                if (!encoded) {
                    lua_pop(l, 2);  // remove iterated key + value
                    goto outofmemfail;
                }
            }
            if (lua_type(l, -2) != LUA_TSTRING) {
                lua_pop(l, 2);
                if (error) {
                    int wrongtype = lua_type(l, -2);
                    char buf[512];
                    snprintf(
                        buf, sizeof(buf) - 1,
                        "json only supports string keys, cannot "
                        "have key of type %s\n",
                        (wrongtype == LUA_TNUMBER ? "number" :
                        (wrongtype == LUA_TNIL ? "nil" :
                        (wrongtype == LUA_TBOOLEAN ? "boolean" :
                        (wrongtype == LUA_TFUNCTION ? "function" :
                        (wrongtype == LUA_TUSERDATA ? "userdata / special" :
                        "<unknown>")))))
                    );
                    *error = strdup(buf);
                }
                if (encoded)
                    free(encoded);
                json_Free(v);
                return NULL;
            }
            got_key_value = 1;
            char *key_str = json_EncodedStrFromStackEx(
                l, -2, maxdepth - 1, error
            );
            if (!key_str) {
                lua_pop(l, 2);  // remove iterated key + value
                if (encoded)
                    free(encoded);
                json_Free(v);
                return NULL;
            }
            char *value_str = json_EncodedStrFromStackEx(
                l, -1, maxdepth - 1, error
            );
            lua_pop(l, 1);  // remove value now
            if (!value_str) {
                lua_pop(l, 1);  // remove iterated key
                free(key_str);
                if (encoded)
                    free(encoded);
                json_Free(v);
                return NULL;
            }
            char *new_encoded = realloc(
                encoded, strlen(encoded) + 1 + strlen(key_str) + 1 +
                strlen(value_str) + 2
            );
            if (!new_encoded) {
                lua_pop(l, 1);  // remove iterated key
                free(value_str);
                free(key_str);
                if (encoded)
                    free(encoded);
                json_Free(v);
                return NULL;
            }
            encoded = new_encoded;
            assert(strlen(encoded) > 0 && (
                encoded[strlen(encoded) - 1] == '}' ||
                is_first_value
            ));
            if (!is_first_value)
                encoded[strlen(encoded) - 1] = ',';
            int new_len = strlen(encoded) + strlen(key_str);
            memcpy(encoded + strlen(encoded),
                   key_str, strlen(key_str));
            encoded[new_len] = '\0';
            encoded[strlen(encoded) + 1] = '\0';
            encoded[strlen(encoded)] = ':';
            new_len = strlen(encoded) + strlen(value_str);
            memcpy(encoded + strlen(encoded),
                   value_str, strlen(value_str));
            encoded[new_len] = '\0';
            encoded[strlen(encoded) + 1] = '\0';
            encoded[strlen(encoded)] = '}';
            free(key_str);
            free(value_str);
            is_first_value = 0;
        }
        if (!got_key_value) {
            assert(encoded == NULL && is_first_value);
            encoded = strdup("[");
            if (!encoded)
                goto outofmemfail;
            int len = lua_rawlen(l, valueindex);
            int k = 0;
            while (k < len) {
                lua_pushnumber(l, k + 1);
                lua_gettable(l, valueindex);
                char *value_str = json_EncodedStrFromStackEx(
                    l, -1, maxdepth - 1, error
                );
                lua_pop(l, 1);  // remove value now
                if (!value_str) {
                    if (encoded)
                        free(encoded);
                    json_Free(v);
                    return NULL;
                }
                char *new_encoded = realloc(
                    encoded, strlen(encoded) + 1 +
                    strlen(value_str) + 2
                );
                if (!new_encoded) {
                    lua_pop(l, 1);  // remove iterated key
                    free(value_str);
                    if (encoded)
                        free(encoded);
                    json_Free(v);
                    return NULL;
                }
                encoded = new_encoded;
                assert(strlen(encoded) > 0 && (
                    encoded[strlen(encoded) - 1] == ']' ||
                    is_first_value
                ));
                if (!is_first_value)
                    encoded[strlen(encoded) - 1] = ',';
                int new_len = strlen(encoded) + strlen(value_str);
                memcpy(encoded + strlen(encoded),
                       value_str, strlen(value_str));
                encoded[new_len] = '\0';
                encoded[strlen(encoded) + 1] = '\0';
                encoded[strlen(encoded)] = ']';
                free(value_str);
                is_first_value = 0;
            }
            if (len <= 0) {
                free(encoded);
                encoded = strdup("]");
                if (!encoded)
                    goto outofmemfail;
            }
        }
        json_Free(v);
        assert(encoded != NULL);
        return encoded;
    } else if (lua_type(l, valueindex) == LUA_TFUNCTION) {
        json_Free(v);
        if (error)
            *error = strdup("json cannot encode value of type function");
        return NULL;
    } else if (lua_type(l, valueindex) == LUA_TUSERDATA) {
        json_Free(v);
        if (error)
            *error = strdup("json cannot encode value of type "
                "userdata / special object");
        return NULL;
    } else {
        json_Free(v);
        if (error)
            *error = strdup("unknown type encountered");
        return NULL;
    }
}

int json_PushEncodedStrToLuaStack(
        lua_State *l, int valueindex, char **error
        ) {
    char *s = json_EncodedStrFromStackEx(l, valueindex, 64, error);
    if (!s)
        return 0;
    if (!luamem_EnsureCanAllocSize(l, strlen(s) * 2)) {
        free(s);
        return 0;
    }
    lua_pushstring(l, s);
    return 1;
}

int json_PushDecodedValueToLuaStackEx(
        lua_State *l, jsonvalue *jv, int maxdepth
        ) {
    if (!luamem_EnsureFreePools(l) || maxdepth <= 0)
        return 0;
    if (jv->type == JSON_VALUE_NULL) {
        lua_pushnil(l);
        return 1;
    } else if (jv->type == JSON_VALUE_INT) {
        lua_pushnumber(l, (double)jv->value_int);
        return 1;
    } else if (jv->type == JSON_VALUE_FLOAT) {
        lua_pushnumber(l, (double)jv->value_float);
        return 1;
    } else if (jv->type == JSON_VALUE_STR) {
        if (!luamem_EnsureCanAllocSize(l, strlen(jv->value_str) * 2))
            return 0;
        lua_pushstring(l, jv->value_str);
        return 1;
    } else if (jv->type == JSON_VALUE_BOOL) {
        lua_pushboolean(l, jv->value_bool != 0);
        return 1;
    } else if (jv->type == JSON_VALUE_LIST) {
        lua_newtable(l);
        int failedpush = 0;
        int k = 0;
        while (k < jv->value_list.count) {
            if (!json_PushDecodedValueToLuaStackEx(
                    l, jv->value_list.contents[k],
                    maxdepth - 1)) {
                lua_pop(l, 1);  // remove table
                return 0;
            } else {
                lua_pushnumber(l, k + 1);
                lua_insert(l, lua_gettop(l) - 1);
                lua_settable(l, -3);
            }
            k++;
        }
        return 1;
    } else if (jv->type == JSON_VALUE_OBJECT) {
        lua_newtable(l);
        int failedpush = 0;
        int k = 0;
        while (k < jv->value_object.count) {
            if (!luamem_EnsureCanAllocSize(l,
                    strlen(jv->value_object.keys[k]) * 2)) {
                lua_pop(l, 1);  // remove table
                return 0;
            }
            lua_pushstring(l, jv->value_object.keys[k]);
            if (!json_PushDecodedValueToLuaStackEx(
                    l, jv->value_object.values[k],
                    maxdepth - 1)) {
                lua_pop(l, 2);  // remove table + key
                return 0;
            }
            lua_settable(l, -3);
            k++;
        }
        return 1;
    } else {
        return 0;
    }
}

int json_PushDecodedValueToLuaStack(lua_State *l, jsonvalue *jv) {
    return json_PushDecodedValueToLuaStackEx(l, jv, 64);
}
