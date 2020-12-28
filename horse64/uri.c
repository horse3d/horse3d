// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <winuser.h>
#endif

#include "filesys.h"
#include "nonlocale.h"
#include "uri.h"
#include "uri32.h"
#include "widechar.h"


int uri_Compare(
        const char *uri1str, const char *uri2str,
        int converttoabsolutefilepaths,
        int assumecasesensitivefilepaths, int *result
        ) {
    int wasinvalid, wasoom;
    int64_t uri1_32len = 0;
    h64wchar *uri1_32 = NULL;
    int64_t uri2_32len = 0;
    h64wchar *uri2_32 = NULL;
    uri1_32 = utf8_to_utf32_ex(
        uri1str, strlen(uri1str), 
        NULL, 0, NULL, NULL, &uri1_32len,
        1, 0, &wasinvalid, &wasoom
    );
    uri2_32 = utf8_to_utf32_ex(
        uri2str, strlen(uri2str), 
        NULL, 0, NULL, NULL, &uri2_32len,
        1, 0, &wasinvalid, &wasoom
    );
    if (!uri1_32 || !uri2_32) {
        free(uri1_32);
        free(uri2_32);
        return 0;
    }
    int cmp = uri32_Compare(
        uri1_32, uri1_32len,
        uri2_32, uri2_32len,
        converttoabsolutefilepaths,
        assumecasesensitivefilepaths,
        result
    );
    free(uri1_32);
    free(uri2_32);
    return cmp;
}

int uri32info_to_uriinfo(
        const uri32info *input, uriinfo *output
        ) {
    memset(output, 0, sizeof(*output));
    if (!input)
        return 0;

    output->port = input->port;
    if (input->host) {
        char *outbuf = malloc(input->hostlen * 5 + 1);
        if (!outbuf) {
            oom: ;
            if (output->host) free(output->host);
            if (output->path) free(output->path);
            if (output->protocol) free(output->protocol);
            return 0;
        }
        int64_t outlen = 0;
        int result = utf32_to_utf8(
            input->host, input->hostlen,
            outbuf, input->hostlen * 5, &outlen,
            1, 0
        );
        if (!result || outlen >= input->hostlen * 5 + 1) {
            free(outbuf);
            goto oom;
        }
        outbuf[outlen] = '\0';
        output->host = outbuf;
    }
    if (input->path) {
        char *outbuf = malloc(input->pathlen * 5 + 1);
        if (!outbuf)
            goto oom;
        int64_t outlen = 0;
        int result = utf32_to_utf8(
            input->path, input->pathlen,
            outbuf, input->pathlen * 5, &outlen,
            1, 0
        );
        if (!result || outlen >= input->pathlen * 5 + 1) {
            free(outbuf);
            goto oom;
        }
        outbuf[outlen] = '\0';
        output->path = outbuf;
    }
    if (input->protocol) {
        char *outbuf = malloc(input->protocollen * 5 + 1);
        if (!outbuf)
            goto oom;
        int64_t outlen = 0;
        int result = utf32_to_utf8(
            input->protocol, input->protocollen,
            outbuf, input->protocollen * 5, &outlen,
            1, 0
        );
        if (!result || outlen >= input->protocollen * 5 + 1) {
            free(outbuf);
            goto oom;
        }
        outbuf[outlen] = '\0';
        output->protocol = outbuf;
    }
    return 1;
}

uriinfo *uri_ParseEx(
        const char *uri,
        const char *default_remote_protocol
        ) {
    if (!uri)
        return NULL;

    int wasinvalid, wasoom;
    int64_t uriu32len = 0;
    h64wchar *uriu32 = utf8_to_utf32_ex(
        uri, strlen(uri),
        NULL, 0, NULL, NULL, &uriu32len,
        1, 0, &wasinvalid, &wasoom
    );
    if (!uriu32)
        return NULL;

    uri32info *result32 = uri32_ParseEx(
        uriu32, uriu32len, default_remote_protocol
    );
    free(uriu32);
    uriu32 = NULL;

    uriinfo *result = malloc(sizeof(*result));
    if (!result) {
        uri32_Free(result32);
        return NULL;
    }

    int cvresult = uri32info_to_uriinfo(
        result32, result
    );
    uri32_Free(result32);
    if (!cvresult) {
        free(result);  // NOT uri_Free(), contains garbage!
        return NULL;
    }

    return result;
}

uriinfo *uri_Parse(
        const char *uri
        ) {
    return uri_ParseEx(uri, "https");
}

char *uri_DumpEx(uriinfo *uinfo, int absolutefilepaths);

char *uri_Normalize(const char *uri, int absolutefilepaths) {
    int64_t uri32len = 0;
    h64wchar *uri32 = NULL;
    int wasinvalid, wasoom;
    uri32 = utf8_to_utf32_ex(
        uri, strlen(uri), 
        NULL, 0, NULL, NULL, &uri32len,
        1, 0, &wasinvalid, &wasoom
    );
    if (!uri32)
        return NULL;
    int64_t resultlen = 0;
    h64wchar *result = uri32_Normalize(
        uri32, uri32len, absolutefilepaths, &resultlen
    );
    free(uri32);
    if (!result)
        return NULL;
    int64_t u8resultlen = 0;
    char *u8result = malloc(resultlen * 5 + 1);
    if (!u8result) {
        free(result);
        return NULL;
    }
    int conversionworked = utf32_to_utf8(
        result, resultlen,
        u8result, resultlen * 5 + 1, &u8resultlen,
        1, 1
    );
    free(result);
    if (!conversionworked) {
        return NULL;
    }
    u8result[u8resultlen] = '\0';
    return u8result;
}

char *uri_Dump(uriinfo *uinfo) {
    return uri_DumpEx(uinfo, 0);
}

int uriinfo_to_uri32info(
        const uriinfo *input, uri32info *output
        ) {
    if (!input)
        return 0;
    memset(output, 0, sizeof(*output));

    if (input->host) {
        int wasinvalid, wasoom;
        output->host = utf8_to_utf32_ex(
            input->host, strlen(input->host),
            NULL, 0, NULL, NULL,
            &output->hostlen, 1, 0,
            &wasinvalid, &wasoom
        );
        if (!output->host) {
            oom:
            free(output->host);
            free(output->protocol);
            free(output->path);
            return 0;
        }
    }
    if (input->path) {
        int wasinvalid, wasoom;
        output->path = utf8_to_utf32_ex(
            input->path, strlen(input->path),
            NULL, 0, NULL, NULL,
            &output->pathlen, 1, 0,
            &wasinvalid, &wasoom
        );
        if (!output->path)
            goto oom;
    }
    if (input->protocol) {
        int wasinvalid, wasoom;
        output->protocol = utf8_to_utf32_ex(
            input->protocol, strlen(input->protocol),
            NULL, 0, NULL, NULL,
            &output->protocollen, 1, 0,
            &wasinvalid, &wasoom
        );
        if (!output->protocol)
            goto oom;
    }
    return 1;
}

h64wchar *uri32_DumpEx(
    const uri32info *uinfo, int absolutefilepaths,
    int64_t *out_len
);

char *uri_DumpEx(uriinfo *uinfo, int absolutefilepaths) {
    uri32info *converted = malloc(sizeof(*converted));
    if (!converted)
        return NULL;
    if (!uriinfo_to_uri32info(
            uinfo, converted
            )) {
        free(converted);  // NOT uri32_Free(), has garbage!
        return NULL;
    }
    int64_t resultlen = 0;
    h64wchar *result = uri32_DumpEx(
        converted, absolutefilepaths, &resultlen
    );
    uri32_Free(converted);
    converted = NULL;
    if (!result)
        return NULL;
    int64_t u8resultlen = 0;
    char *u8result = malloc(resultlen * 5 + 1);
    if (!u8result) {
        free(result);
        return NULL;
    }
    int conversionworked = utf32_to_utf8(
        result, resultlen,
        u8result, resultlen * 5 + 1, &u8resultlen,
        1, 1
    );
    free(result);
    if (!conversionworked) {
        return NULL;
    }
    u8result[u8resultlen] = '\0';
    return u8result;
}

void uri_Free(uriinfo *uri) {
    if (!uri)
        return;
    free(uri->host);
    free(uri->path);
    free(uri->protocol);
    free(uri);
}
