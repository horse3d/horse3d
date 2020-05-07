
#include <assert.h>
#if defined(__WIN32) || defined(__WIN64)
#include <windows.h>
#endif

#define SDL_PIXELFORMAT_XBGR8888 SDL_PIXELFORMAT_BGR888

#include <mathc/mathc.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "filesys.h"
#include "glheaders.h"
#include "hash.h"
#include "render3d.h"
#include "texture.h"
#include "vfs.h"


h3dtexture **textures_all = NULL;
int textures_globalcount = 0;

extern SDL_GLContext *_glcontext;
extern SDL_gles2funcs *_gl;
static int _last_texid_used = -1;


static h3dtexture *missingtex_image = NULL;
static h3dtexture *blanktex_image = NULL;
static hashmap *texture_by_name_cache = NULL;
static hashmap *texture_by_id_cache = NULL;


static void _texture_RemoveGlobalTexture(h3dtexture *tex);


h3dtexture *texture_GetById(int id) {
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            texture_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3dtexture*)(void*)hashptrval;
}

int _texture_LoadTextureGL(h3dtexture *tex, int alpha) {
    if ((!alpha && tex->isglloaded) ||
            (alpha && tex->isglloadedalpha))
        return 1;

    char *pixels = malloc(tex->w * tex->h * (alpha ? 4 : 3));
    if (!pixels)
        return 0;

    int i = 0;
    SDL_Surface *srf = (alpha ? tex->srfalpha : tex->srf);
    SDL_LockSurface(srf);
    char *sp = srf->pixels;
    char *tp = pixels;
    while (i < tex->h) {
        if (alpha) {
            memcpy(tp, sp, tex->w * (alpha ? 4 : 3));
            sp += srf->pitch;
            tp += tex->w * 4;
        } else {
            int k = 0;
            while (k < tex->w) {
                memcpy(tp, sp, 3);
                sp += 4;
                tp += 3;
                k++;
            }
            sp += (srf->pitch - tex->w * 4);
        }
        i++;
    }
    SDL_UnlockSurface(srf);

    _gl->glGenTextures(1, (
        alpha ? &tex->gltexturealpha : &tex->gltexture
    ));
    _gl->glActiveTexture(GL_TEXTURE0);
    _gl->glBindTexture(GL_TEXTURE_2D, (
        alpha ? tex->gltexturealpha : tex->gltexture
    ));
    if (alpha)
        tex->isglloadedalpha = 1;
    else
        tex->isglloaded = 1;
    _gl->glTexImage2D(
        GL_TEXTURE_2D, 0, (alpha ? GL_RGBA : GL_RGB),
        tex->w, tex->h, 0,
        (alpha ? GL_RGBA : GL_RGB), GL_UNSIGNED_BYTE, pixels
    );
    _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    _gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    _gl->glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR
    );
    _gl->glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR
    );
    _gl->glBindTexture(GL_TEXTURE_2D, 0);

    free(pixels);
    return 1;
}

static void _texture_RemoveGlobalTexture(h3dtexture *tex) {
    if (!tex)
        return;
    assert(tex == (h3dtexture*)(
        textures_all[textures_globalcount - 1]
    ));
    if (tex->srf)
        SDL_FreeSurface(tex->srf);
    if (tex->srfalpha)
        SDL_FreeSurface(tex->srfalpha);
    if (tex->isglloadedalpha) {
        _gl->glDeleteTextures(
            1, &tex->gltexturealpha
        );
    }
    if (tex->isglloaded) {
        _gl->glDeleteTextures(
            1, &tex->gltexture
        );
    }
    free(textures_all[textures_globalcount - 1]);
    textures_all[textures_globalcount - 1] = NULL;
    textures_globalcount--;  // since this is the last entry.
}

static h3dtexture *texture_AllocateNewSlot() {
    h3dtexture **new_array = realloc(
        textures_all,
        sizeof(*new_array) * (textures_globalcount + 1)
    );
    if (!new_array)
        return 0;
    textures_all = new_array;
    textures_all[textures_globalcount] = malloc(
        sizeof(**new_array)
    );
    if (!textures_all[textures_globalcount]) {
        return 0;
    }
    memset(textures_all[textures_globalcount],
           0, sizeof(**new_array));
    textures_globalcount++;
    return textures_all[textures_globalcount - 1];
}

static h3dtexture *texture_CreateFromPixData(
        unsigned char *data, int width, int height
        ) {
    h3dtexture *tex = texture_AllocateNewSlot();

    if (!data || width <= 0 || height <= 0) {
        textures_globalcount--;
        free(tex);
        return NULL;
    }

    // Load data into surface to convert later:
    int linewidth = width * 4;
    SDL_Surface *srforig = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_ABGR8888
    );
    if (!srforig) {
        textures_globalcount--;
        free(tex);
        return NULL;
    }
    SDL_LockSurface(srforig);
    int line = 0;
    char *p = (char *)srforig->pixels;
    while (line < height) {
        memcpy(p, data + linewidth * line, linewidth);
        p += srforig->pitch;
        line++;
    }
    SDL_UnlockSurface(srforig);

    // Create converted surfaces:
    SDL_Surface *srf = SDL_ConvertSurfaceFormat(
        srforig, SDL_PIXELFORMAT_XBGR8888, 0
    );
    SDL_Surface *srfalpha = SDL_ConvertSurfaceFormat(
        srforig, SDL_PIXELFORMAT_ABGR8888, 0
    );
    SDL_FreeSurface(srforig);
    if (!srf || !srfalpha) {
        if (srf)
            SDL_FreeSurface(srf);
        if (srfalpha)
            SDL_FreeSurface(srfalpha);
        textures_globalcount--;
        free(tex);
        return NULL;
    }

    // Store as new global texture:
    memset(tex, 0, sizeof(*tex));
    _last_texid_used++;
    tex->id = _last_texid_used;
    tex->w = srf->w;
    tex->h = srf->h;
    tex->srf = srf;
    tex->srfalpha = srfalpha;
    return tex;
}

static h3dtexture *texture_CreateOneColorTexture(
        int r, int g, int b, int a, int w, int h
        ) {
    if (w <= 0)
        w = 16;
    if (h <= 0)
        h = 16;
    unsigned char *data = malloc(w * h * 4);
    if (!data)
        return NULL;
    int i = 0;
    while (i < w * h * 4) {
        data[i] = r;
        data[i + 1] = g;
        data[i + 2] = b;
        data[i + 3] = a;
        i += 4;
    }
    h3dtexture *tex = texture_CreateFromPixData(
        data, w, h
    );
    free(data);
    return tex;
}

h3dtexture *texture_GetMissingImageTexture() {
    if (missingtex_image)
        return missingtex_image;
    int w = 16;
    int h = 16;
    unsigned char *data = malloc(16 * 16 * 4);
    if (!data)
        return NULL;
    int x = 0;
    while (x < w) {
        int y = 0;
        while (y < h) {
            int i = (x + y * w) * 4;
            data[i] = 255;
            data[i + 1] = 0;
            data[i + 2] = (
                ((x < w / 2 && y < h / 2) ||
                (x >= w / 2 && y >= h / 2)) ? 255 : 0
            );
            data[i + 3] = 255;
            y++;
        }
        x++;
    }
    missingtex_image = texture_CreateFromPixData(
        data, w, h
    );
    free(data);
    return missingtex_image;
}

h3dtexture *texture_GetBlankTexture() {
    if (blanktex_image)
        return blanktex_image;
    blanktex_image = texture_CreateOneColorTexture(
        255, 255, 255, 255, 16, 16
    );
    return blanktex_image;
}

static h3dtexture *texture_LoadTextureEx(
        const char *path, int usecache
        ) {
    if (usecache) {
        char *p = filesys_Normalize(path);
        if (!p)
            return NULL;

        if (!texture_by_name_cache) {
            texture_by_name_cache = hash_NewBytesMap(128);
            if (!texture_by_name_cache) {
                free(p);
                return NULL;
            }
        }
        if (!texture_by_id_cache) {
            texture_by_id_cache = hash_NewBytesMap(128);
            if (!texture_by_id_cache) {
                free(p);
                return NULL;
            }
        }
        uintptr_t hashptrval = 0;
        if (!hash_BytesMapGet(
                texture_by_name_cache, p, strlen(p),
                (uint64_t*)&hashptrval)) {
            h3dtexture *t = texture_LoadTextureEx(
                path, 0
            );
            if (!t) {
                free(p);
                return NULL;
            }
            if (!hash_BytesMapSet(
                    texture_by_name_cache, p, strlen(p),
                    (uint64_t)(uintptr_t)t)) {
                free(p);
                _texture_RemoveGlobalTexture(t);
                return NULL;
            }
            if (!hash_BytesMapSet(
                    texture_by_id_cache,
                    (char*)&t->id, sizeof(t->id),
                    (uint64_t)(uintptr_t)t)) {
                hash_BytesMapUnset(
                    texture_by_name_cache, p, strlen(p)
                );
                free(p);
                _texture_RemoveGlobalTexture(t);
                return NULL;
            }
            free(p);
            return t;
        }
        free(p);
        return (h3dtexture*)(void*)hashptrval;
    }

    // Load raw image data with stb_image:
    int w = 0;
    int h = 0;
    int n = 0;
    char *imgencodedfile = NULL;
    int32_t size = 0;
    VFSFILE *f = vfs_fopen(path, "rb", 0);
    if (!f) {
        return NULL;
    }
    while (f && !vfs_feof(f)) {
        char buf[512];
        int result = vfs_fread(buf, 1, sizeof(buf), f);
        if (result <= 0) {
            if (vfs_feof(f))
                break;
            if (imgencodedfile)
                free(imgencodedfile);
            vfs_fclose(f);
            return NULL;
        }
        char *imgencodedfilenew = realloc(
            imgencodedfile, size + result
        );
        if (!imgencodedfilenew) {
            if (imgencodedfile)
                free(imgencodedfile);
            vfs_fclose(f);
            return NULL;
        }
        imgencodedfile = imgencodedfilenew;
        memcpy(imgencodedfilenew + size, buf, result);
        size += result;
    }
    if (f)
        vfs_fclose(f);
    unsigned char *data = stbi_load_from_memory(
        (unsigned char *)imgencodedfile, size, &w, &h, &n, 4
    );
    free(imgencodedfile);
    if (!data)
        return NULL;

    // Create texture:
    h3dtexture *tex = texture_CreateFromPixData(
        data, w, h
    );
    stbi_image_free(data);
    if (tex) {
        tex->filepath = strdup(path);
        if (!tex->filepath) {
            SDL_FreeSurface(tex->srf);
            SDL_FreeSurface(tex->srfalpha);
            free(tex);
            textures_globalcount--;
            return NULL;
        }
    }
    return tex;
}


int texture_HasNotableTransparency(h3dtexture *tex) {
    if (!tex || !tex->srfalpha)
        return 0;

    const int max = tex->w * tex->h * 4;
    SDL_LockSurface(tex->srfalpha);
    const uint8_t *pixels = ((uint8_t*)tex->srfalpha->pixels);
    int hasalpha = 0;
    int i = 0;
    while (i < max) {
        if (pixels[i + 3] < 127) {
            hasalpha = 1;
            break;
        }
        i += 4;
    }
    SDL_UnlockSurface(tex->srfalpha);
    return hasalpha;
}

h3dtexture *texture_GetTexture(
        const char *path
        ) {
    return texture_LoadTextureEx(path, 1);
}
