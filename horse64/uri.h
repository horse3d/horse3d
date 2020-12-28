// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_URI_H_
#define HORSE64_URI_H_

#include "uri32.h"


typedef struct uriinfo {
    char *protocol;
    char *host;
    int port;
    char *path;
} uriinfo;

uriinfo *uri_ParseEx(
    const char *uri,
    const char *default_remote_protocol
);

uriinfo *uri_Parse(const char *uri);

char *uri_Normalize(const char *uri, int absolutefilepaths);

void uri_Free(uriinfo *uri);

char *uri_Dump(uriinfo *uri);

int uri_Compare(
    const char *uri1str, const char *uri2str,
    int converttoabsolutefilepaths,
    int assumecasesensitivefilepaths, int *result
);

int uri32info_to_uriinfo(
    const uri32info *input, uriinfo *output
);

int uriinfo_to_uri32info(
    const uriinfo *input, uri32info *output
);

#endif  // HORSE64_URI_H_
