#ifndef HORSE3D_URI_H_
#define HORSE3D_URI_H_


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

#endif  // HORSE3D_URI_H_
