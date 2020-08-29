// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "uri.h"


static char *uri_ParsePath(
        const char *escaped_path,
        int enforceleadingslash
        ) {
    char *unescaped_path = strdup(escaped_path);
    unsigned int i = 0;
    while (i < strlen(unescaped_path)) {
        if (unescaped_path[i] == '%' &&
                ((unescaped_path[i + 1] >= '0' &&
                  unescaped_path[i + 1] <= '9') ||
                 (unescaped_path[i + 1] >= 'a' &&
                  unescaped_path[i + 1] <= 'a') ||
                 (unescaped_path[i + 1] >= 'A' &&
                  unescaped_path[i + 1] <= 'Z')) &&
                ((unescaped_path[i + 2] >= '0' &&
                  unescaped_path[i + 2] <= '9') ||
                 (unescaped_path[i + 2] >= 'a' &&
                  unescaped_path[i + 2] <= 'z') ||
                 (unescaped_path[i + 2] >= 'A' &&
                  unescaped_path[i + 2] <= 'Z'))
                ) {
            char hexvalue[3];
            hexvalue[0] = unescaped_path[i + 1];
            hexvalue[1] = unescaped_path[i + 2];
            hexvalue[2] = '\0';
            int number = (int)strtol(hexvalue, NULL, 16);
            if (number > 255 || number == 0)
                number = '?';
            ((uint8_t*)unescaped_path)[i] = (uint8_t)number;
            memmove(
                &unescaped_path[i + 1], &unescaped_path[i + 3],
                strlen(unescaped_path) - (i + 3) + 1
            );
        }
        i++;
    }
    if (unescaped_path[0] != '/' && unescaped_path[0] != '\\' &&
            enforceleadingslash) {
        char *unescaped_path_2 = malloc(
            strlen(unescaped_path) + 2
        );
        unescaped_path_2[0] = '/';
        memcpy(unescaped_path_2 + 1, unescaped_path,
               strlen(unescaped_path) + 1);
        free(unescaped_path);
        return unescaped_path_2;
    }
    return unescaped_path;
}

uriinfo *uri_ParseEx(
        const char *uri,
        const char *default_remote_protocol
        ) {
    if (!uri)
        return NULL;

    uriinfo *result = malloc(sizeof(*result));
    if (!result)
        return NULL;
    memset(result, 0, sizeof(*result));
    result->port = -1;

    int lastdotindex = -1;
    const char *part_start = uri;
    const char *part = uri;
    while (*part != ' ' && *part != ';' && *part != ':' &&
            *part != '/' && *part != '\\' && *part != '\0' &&
            *part != '\n' && *part != '\r' && *part != '\t' &&
            *part != '@' && *part != '*' && *part != '&' &&
            *part != '%' && *part != '#' && *part != '$' &&
            *part != '!' && *part != '"' && *part != '\'' &&
            *part != '(' && *part != ')' && *part != '|') {
        if (*part == '.')
            lastdotindex = (part - part_start);
        part++;
    }
    int recognizedfirstblock = 0;
    if (strlen(part) >= strlen("://") &&
            (memcmp(part, "://", strlen("://")) == 0 ||
             memcmp(part, ":\\\\", strlen(":\\\\")) == 0)) {
        // Extract path:
        result->protocol = malloc(part - part_start + 1);
        if (!result->protocol) {
            uri_Free(result);
            return NULL;
        }
        memcpy(result->protocol, part_start, part - part_start);
        result->protocol[part - part_start] = '\0';
        part += 3;
        lastdotindex = -1;
        part_start = part;
        if (strcasecmp(result->protocol, "file") == 0) {
            result->path = uri_ParsePath(
                part_start, 0
            );
            if (!result->path) {
                uri_Free(result);
                return NULL;
            }
            char *path_cleaned = filesys_Normalize(result->path);
            free(result->path);
            result->path = path_cleaned;
            return result;
        }
        recognizedfirstblock = 1;
    } else if (*part == ':' && (part - uri) == 1 &&
            ((uri[0] >= 'a' && uri[0] <= 'z') ||
             (uri[0] >= 'A' && uri[0] <= 'Z')) &&
            (*(part + 1) == '/' || *(part + 1) == '\\')) {
        // Looks like a Windows absolute path:
        result->protocol = strdup("file");
        if (!result->protocol) {
            uri_Free(result);
            return NULL;
        }
        result->path = filesys_Normalize(uri);
        if (!result->path) {
            uri_Free(result);
            return NULL;
        }
        return result;
    } else if (*part == '/' && (part - uri) == 0) {
        // Looks like a Unix absolute path:
        result->protocol = strdup("file");
        if (!result->protocol) {
            uri_Free(result);
            return NULL;
        }
        result->path = filesys_Normalize(uri);
        if (!result->path) {
            uri_Free(result);
            return NULL;
        }
        return result;
    } else {
        recognizedfirstblock = 0;
    }

    if (recognizedfirstblock) {
        while (*part != ' ' && *part != ';' &&
                *part != '/' && *part != '\\' && *part != '\0' &&
                *part != '\n' && *part != '\r' && *part != '\t' &&
                *part != '@' && *part != '*' && *part != '&' &&
                *part != '%' && *part != '#' && *part != '$' &&
                *part != '!' && *part != '"' && *part != '\'' &&
                *part != '(' && *part != ')' && *part != '|') {
            if (*part == '.')
                lastdotindex = (part - part_start);
            part++;
        }
    }

    if (*part == ':' &&
            (*(part + 1) >= '0' && *(part + 1) <= '9') &&
            lastdotindex > 0) {
        // Looks like we've had the host followed by port:
        if (!result->protocol) {
            if (default_remote_protocol) {
                result->protocol = strdup(
                    default_remote_protocol
                );
                if (!result->protocol) {
                    uri_Free(result);
                    return NULL;
                }
            } else {
                result->protocol = NULL;
            }
        }
        result->host = malloc(part - part_start + 1);
        if (!result->host) {
            uri_Free(result);
            return NULL;
        }
        memcpy(result->host, part_start, part - part_start);
        result->host[part - part_start] = '\0';
        part++;
        part_start = part;
        lastdotindex = -1;
        while (*part != '\0' &&
                (*part >= '0' && *part <= '9'))
            part++;
        char *portbuf = malloc(part - part_start + 1);
        if (!portbuf) {
            uri_Free(result);
            return NULL;
        }
        memcpy(portbuf, part_start, part - part_start);
        portbuf[part - part_start] = '\0';
        result->port = atoi(portbuf);
        free(portbuf);
        part_start = part;
        lastdotindex = -1;
    } else if ((*part == '/' || *part == '\0') &&
            result->protocol &&
            strcasecmp(result->protocol, "file") != 0) {
        result->host = malloc(part - part_start + 1);
        if (!result->host) {
            uri_Free(result);
            return NULL;
        }
        memcpy(result->host, part_start, part - part_start);
        result->host[part - part_start] = '\0';
        part_start = part;
        lastdotindex = -1;
    }

    if (!result->protocol && !result->host && result->port < 0) {
        result->protocol = strdup("file");
        if (!result->protocol) {
            uri_Free(result);
            return NULL;
        }
    }

    result->path = uri_ParsePath(
        part_start,
        (!result->protocol || strcasecmp(result->protocol, "file") != 0)
    );
    if (!result->path) {
        uri_Free(result);
        return NULL;
    }
    if (result->protocol && strcasecmp(result->protocol, "file") == 0) {
        char *path_cleaned = filesys_Normalize(result->path);
        free(result->path);
        result->path = path_cleaned;
    }
    return result;
}

uriinfo *uri_Parse(
        const char *uri
        ) {
    return uri_ParseEx(uri, "https");
}

char *uriencode(const char *path) {
    char *buf = malloc(strlen(path) * 3 + 1);
    if (!buf)
        return NULL;
    int buffill = 0;
    unsigned int i = 0;
    while (i < strlen(path)) {
        if (path[i] == '%' ||
                #if defined(_WIN32) || defined(_WIN64)
                path[i] == '\\' ||
                #endif
                path[i] <= 32 ||
                path[i] == ' ' || path[i] == '\t' ||
                path[i] == '[' || path[i] == ']' ||
                path[i] == ':' || path[i] == '?' ||
                path[i] == '&' || path[i] == '=' ||
                path[i] == '\'' || path[i] == '"' ||
                path[i] == '@' || path[i] == '#') {
            char hexval[4];
            snprintf(hexval, sizeof(hexval) - 1,
                "%x", (int)((uint8_t*)path)[i]);
            buf[buffill] = '%'; buffill++;
            unsigned int z = strlen(hexval);
            while (z < 2) {
                buf[buffill] = '0'; buffill++;
                z++;
            }
            z = 0;
            while (z < strlen(hexval)) {
                buf[buffill] = hexval[z]; buffill++;
                z++;
            }
        } else {
            buf[buffill] = path[i]; buffill++;
        }
        i++;
    }
    buf[buffill] = '\0';
    return buf;
}

char *uri_DumpEx(uriinfo *uinfo, int absolutefilepaths);

char *uri_Normalize(const char *uri, int absolutefilepaths) {
    uriinfo *uinfo = uri_Parse(uri);
    if (!uinfo) {
        return NULL;
    }
    return uri_DumpEx(uinfo, absolutefilepaths);
}

char *uri_Dump(uriinfo *uinfo) {
    return uri_DumpEx(uinfo, 0);
}

char *uri_DumpEx(uriinfo *uinfo, int absolutefilepaths) {
    char portbuf[128] = "";
    if (uinfo->port > 0) {
        snprintf(
            portbuf, sizeof(portbuf) - 1,
            ":%d", uinfo->port
        );
    }
    char *path = strdup((uinfo->path ? uinfo->path : ""));
    if (!path) {
        uri_Free(uinfo);
        return NULL;
    }
    if (uinfo->protocol && strcasecmp(uinfo->protocol, "file") == 0 &&
            !filesys_IsAbsolutePath(path) && absolutefilepaths &&
            uinfo->path) {
        char *newpath = filesys_ToAbsolutePath(path);
        if (!newpath) {
            free(path);
            uri_Free(uinfo);
            return NULL;
        }
        free(path);
        path = newpath;
    }
    char *encodedpath = uriencode(path);
    if (!encodedpath) {
        free(path);
        uri_Free(uinfo);
        return NULL;
    }
    free(path);
    path = NULL;

    int upperboundlen = (
        strlen((uinfo->protocol ? uinfo->protocol : "")) +
        strlen("://") +
        strlen((uinfo->host ? uinfo->host : "")) +
        strlen(portbuf) + strlen(encodedpath)
    ) + 10;
    char *buf = malloc(upperboundlen);
    if (!buf) {
        free(encodedpath);
        uri_Free(uinfo);
        return NULL;
    }
    snprintf(
        buf, upperboundlen - 1,
        "%s://%s%s%s%s",
        uinfo->protocol,
        (uinfo->host ? uinfo->host : ""), portbuf,
        ((strlen((uinfo->host ? uinfo->host : "")) > 0 &&
          strlen(encodedpath) > 0 &&
          encodedpath[0] != '/') ? "/" : ""),
        encodedpath
    );
    free(encodedpath);
    uri_Free(uinfo);
    char *shrunkbuf = strdup(buf);
    free(buf);
    return shrunkbuf;
}

void uri_Free(uriinfo *uri) {
    if (!uri)
        return;
    if (uri->protocol)
        free(uri->protocol);
    if (uri->host)
        free(uri->host);
    if (uri->path)
        free(uri->path);
    free(uri);
}
