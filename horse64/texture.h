#ifndef HORSE3D_TEXTURE_H_
#define HORSE3D_TEXTURE_H_

#include "glheaders.h"

typedef struct h3dtexture {
    int id;
    char *filepath;
    int w, h;
    SDL_Surface *srf, *srfalpha;
    GLuint gltexture, gltexturealpha;
    int isglloaded, isglloadedalpha;
} h3dtexture;

h3dtexture *texture_GetMissingImageTexture();

h3dtexture *texture_GetBlankTexture();

h3dtexture *texture_GetTexture(const char *path);

h3dtexture *texture_GetById(int id);

int texture_HasNotableTransparency(h3dtexture *tex);

#endif  // HORSE3D_TEXTURE_H_
