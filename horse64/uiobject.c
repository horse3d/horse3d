
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <mathc/mathc.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

#include "glheaders.h"
#include "hash.h"
#include "render3d.h"
#include "texture.h"
#include "uiobject.h"
#include "uiplane.h"
#include "vfs.h"

#define DEBUGLOADFONT


typedef struct cachedfont {
    char *path;
    TTF_Font *ttfref;
    char *ttffile_contents;
    int64_t ttffile_contentssize;
    SDL_RWops *ttffile_rwops;
    fontinfo finfo;
} cachedfont;

extern SDL_gles2funcs *_gl;
cachedfont **cached_fonts = NULL;
#define CACHEDFONTMAX 20
static uint64_t _last_uiobj_id = 0;
static hashmap *uiobject_by_id_cache = NULL;

static inline int ceilpow(int n) {
    int power = 2;
    while (power < n) power *= 2;
    return power;
}

static TTF_Font *load_font_from_vfs_file(
        const char *path, double ptsize,
        SDL_RWops **contents_rwops, char **contents, int64_t *size
        ) {
    int _existsresult = 0;
    if (!vfs_Exists(path, &_existsresult, 0) || !_existsresult) {
        #ifdef DEBUGLOADFONT
        fprintf(
            stderr, "horse3d/uiobject.c: warning: couldn't load "
            "font, vfs_Exists() didn't return success for: %s\n",
            path
        );
        #endif
        return NULL;
    }
    uint64_t memsize = 0;
    if (!vfs_Size(path, &memsize, 0) || memsize <= 0) {
        #ifdef DEBUGLOADFONT
        fprintf(
            stderr, "horse3d/uiobject.c: warning: couldn't load "
            "font, vfs_Size() didn't return >0 for: %s\n",
            path
        );
        #endif
        return NULL;
    }

    *contents = malloc(memsize);
    if (!*contents)
        return NULL;
    if (!vfs_GetBytes(path, 0, memsize, *contents, 0)) {
        free(*contents);
        #ifdef DEBUGLOADFONT
        fprintf(
            stderr, "horse3d/uiobject.c: warning: couldn't load "
            "font, vfs_GetBytes() failed for: %s\n",
            path
        );
        #endif
        return NULL;
    }

    *contents_rwops = SDL_RWFromMem(
        *contents, memsize
    );
    TTF_Font *font = TTF_OpenFontRW(
        *contents_rwops, 0, round(ptsize)
    );
    *size = memsize;
    #ifdef DEBUGLOADFONT
    fprintf(
        stderr, "horse3d/uiobject.c: debug: loaded "
        "font %p from: %s\n",
        font, path
    );
    #endif
    return font;
}

char *ttf_path_from_font_name(const char *name) {
    if (strstr(name, ".") || strstr(name, "/"))
        return strdup(name);
    char *loweredname = strdup(name);
    if (!loweredname)
        return NULL;
    int i = 0;
    while (i < strlen(loweredname)) {
        loweredname[i] = tolower(loweredname[i]);
        i++;
    }
    if (strstr(loweredname, "comic")) {
        return strdup("demo-assets/packaged-fonts/"
                      "comicneue-regular.ttf");
    } else if (strstr(loweredname, "mono") ||
               strstr(loweredname, "code") ||
               strstr(loweredname, "source")) {
        return strdup("demo-assets/packaged-fonts/"
                      "SourceCodePro-Regular.ttf");
    } else if (strstr(loweredname, "sans") ||
               strstr(loweredname, "arial") ||
               strstr(loweredname, "tahoa") ||
               strstr(loweredname, "segoe")) {
        return strdup("demo-assets/packaged-fonts/"
                      "texgyreheros-regular.ttf");
    } else if (strstr(loweredname, "serif") ||
               strstr(loweredname, "times")) {
        return strdup("demo-assets/packaged-fonts/"
                      "texgyrepagella-regular.ttf");
    }
    return strdup(name);
}

cachedfont *get_font(const char *name, double ptsize) {
    if (!name)
        return NULL;
    int roundedsize = round(ptsize);
    if (roundedsize < 1)
        roundedsize = 1;
    if (!cached_fonts) {
        cached_fonts = malloc(
            sizeof(*cached_fonts) * CACHEDFONTMAX
        );
        if (!cached_fonts)
            return NULL;
        memset(cached_fonts, 0, sizeof(*cached_fonts) * CACHEDFONTMAX);
    }
    int i = 0;
    while (i < CACHEDFONTMAX) {
        if (!cached_fonts[i]) {
            i++;
            continue;
        }
        if (strcmp(cached_fonts[i]->finfo.name, name) == 0 &&
                fabs(cached_fonts[i]->finfo.ptsize - 
                     (double)roundedsize) < 0.4) {
            return cached_fonts[i];
        }
        i++;
    }

    char *ttfpath = ttf_path_from_font_name(name);
    if (!ttfpath)
        return NULL;

    if (!TTF_WasInit() && TTF_Init() == -1) {
        fprintf(
            stderr, "horse3d/h3duiobject.c: error: "
            "TTF_Init failed: %s\n", TTF_GetError()
        );
        free(ttfpath);
        return NULL;
    }

    cachedfont *cfont = malloc(sizeof(*cfont));
    if (!cfont) {
        free(ttfpath);
        return NULL;
    }
    memset(cfont, 0, sizeof(*cfont));
    cfont->finfo.ptsize = roundedsize;
    cfont->finfo.name = strdup(name);
    if (!cfont->finfo.name) {
        free(cfont);
        return NULL;
    }
    cfont->path = ttfpath;
    cfont->ttfref = load_font_from_vfs_file(
        ttfpath, roundedsize,
        &cfont->ttffile_rwops, &cfont->ttffile_contents,
        &cfont->ttffile_contentssize
    );
    if (!cfont->ttfref) {
        free(cfont->finfo.name);
        free(cfont->path);
        free(cfont);
        return NULL;
    }

    int foundslot = -1;
    i = 0;
    while (i < CACHEDFONTMAX) {
        if (!cached_fonts[i]) {
            cached_fonts[i] = cfont;
            foundslot = i;
            break;
        }
        i++;
    }
    if (foundslot < 0) {
        assert(cached_fonts[0] != NULL);
        if (cached_fonts[0]->finfo.name)
            free(cached_fonts[0]->finfo.name);
        if (cached_fonts[0]->path)
            free(cached_fonts[0]->path);
        if (cached_fonts[0]->ttfref)
            TTF_CloseFont(cached_fonts[0]->ttfref);
        if (cached_fonts[0]->ttffile_rwops);
            SDL_RWclose(cached_fonts[0]->ttffile_rwops);
        if (cached_fonts[0]->ttffile_contents)
            free(cached_fonts[0]->ttffile_contents);
        free(cached_fonts[0]);
        memmove(
            &cached_fonts[0],
            &cached_fonts[1],
            sizeof(*cached_fonts) * (CACHEDFONTMAX - 1)
        );
        cached_fonts[CACHEDFONTMAX - 1] = cfont;
    }
    return cfont;
}

int uiobject_ChangeObjectFont(
        h3duiobject *uiobj,
        const char *font_name_or_path, double ptsize
        ) {
    if (!uiobj || uiobj->type != UIOBJECT_TYPE_TEXT)
        return 0;
    cachedfont *cfont = get_font(font_name_or_path, ptsize);
    if (!cfont)
        return 0;
    char *newfontloadpath = strdup(font_name_or_path);
    if (!newfontloadpath)
        return 0;
    char *newfontname = strdup(cfont->finfo.name);
    if (!newfontname) {
        free(newfontloadpath);
        return 0;
    }
    free(uiobj->fontloadpath);
    free(uiobj->font.name);
    uiobj->fontloadpath = newfontloadpath;
    uiobj->font.name = newfontname;
    uiobj->font.ptsize = ptsize;
    if (uiobj->rendered_text) {
        SDL_FreeSurface(uiobj->rendered_text);
        uiobj->rendered_text = NULL;
    }
    return 1;
}

int uiobject_ChangeObjectText(
        h3duiobject *uiobj, const char *text
        ) {
    if (!uiobj || uiobj->type != UIOBJECT_TYPE_TEXT)
        return 0;
    char *newtext = strdup(text);
    if (!newtext)
        return 0;
    free(uiobj->text);
    uiobj->text = newtext;
    if (uiobj->rendered_text) {
        SDL_FreeSurface(uiobj->rendered_text);
        uiobj->rendered_text = NULL;
    }
    return 1;
}

h3duiobject *uiobject_CreateText(
        h3duiplane *plane, const char *font_name_or_path,
        double ptsize, const char *text
        ) {
    if (!uiobject_by_id_cache) {
        uiobject_by_id_cache = hash_NewBytesMap(64);
        if (!uiobject_by_id_cache)
            return NULL;
    }
    if (!plane)
        plane = defaultplane;
    if (!plane)
        return NULL;

    h3duiobject *uiobj = malloc(sizeof(*uiobj));
    if (!uiobj)
        return NULL;
    memset(uiobj, 0, sizeof(*uiobj));
    _last_uiobj_id++;
    uiobj->id = _last_uiobj_id;
    uiobj->scale_x = 1;
    uiobj->scale_y = 1;
    cachedfont *cfont = get_font(font_name_or_path, ptsize);
    if (!cfont) {
        free(uiobj);
        return NULL;
    }
    uiobj->type = UIOBJECT_TYPE_TEXT;
    uiobj->fontloadpath = strdup(font_name_or_path);
    if (!uiobj->fontloadpath) {
        free(uiobj);
        return NULL;
    }
    uiobj->font.name = strdup(cfont->finfo.name);
    if (!uiobj->font.name) {
        free(uiobj);
        free(uiobj->fontloadpath);
        return NULL;
    }
    uiobj->font.ptsize = ptsize;
    uiobj->text = strdup(text);
    if (!uiobj->text) {
        free(uiobj);
        free(uiobj->font.name);
        free(uiobj->fontloadpath);
        return NULL;
    }
    uiobj->textalign = TEXTALIGN_LEFT;

    uint64_t id = uiobj->id;
    if (!hash_BytesMapSet(
            uiobject_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)uiobj)) {
        uiobject_DestroyObject(uiobj);
        return NULL;
    }

    if (!uiplane_AddObject(plane, uiobj)) {
        uiobject_DestroyObject(uiobj);
        return NULL;
    }
    uiplane_UpdateObjectSort(plane, uiobj);

    return uiobj;
}

SDL_Surface *uiobject_GetTextRender(h3duiobject *uiobj) {
    if (!uiobj || uiobj->type != UIOBJECT_TYPE_TEXT)
        return NULL;
    if (uiobj->rendered_text)
        return uiobj->rendered_text;
    assert(uiobj->fontloadpath != NULL);
    cachedfont *cfont = get_font(
        uiobj->fontloadpath, uiobj->font.ptsize
    );
    if (!cfont)
        return NULL;

    SDL_Color c = {255,255,255,255};
    int maxlinelen = 1024 * 1024;
    int maxlines = 1024;
    char *linebuf = malloc(maxlinelen + 1);
    if (!linebuf)
        return NULL;
    SDL_Surface **linerenders = malloc(sizeof(*linerenders) * maxlines);
    if (!linerenders) {
        free(linebuf);
        return NULL;
    }
    memset(linerenders, 0, sizeof(*linerenders) * maxlines);

    SDL_Surface *resultsrf = NULL;
    int totalseenheight = 0;
    int maxseenwidth = 0;
    int lines = 0;
    int linefill = 0;
    linebuf[linefill] = '\0';
    int i = -1;
    while (1) {
        i++;
        if (uiobj->text[i] == '\0' ||
                uiobj->text[i] == '\n' ||
                uiobj->text[i] == '\r') {
            int emptyline = 0;
            if (linefill == 0) {
                emptyline = 1;
                linebuf[linefill + 1] = '\0';
                linebuf[linefill] = ' ';
                linefill++;
            }
            if (lines < maxlines) {
                linerenders[lines] = TTF_RenderUTF8_Blended(
                    cfont->ttfref, linebuf, c
                );
                if (!linerenders[lines]) {
                    exitfailure: ;
                    int k = 0;
                    while (k < lines) {
                        SDL_FreeSurface(linerenders[lines]);
                        k++;
                    }
                    free(linerenders);
                    free(linebuf);
                    if (resultsrf)
                        SDL_FreeSurface(resultsrf);
                    return NULL;
                }
                if (linerenders[lines]->w > maxseenwidth &&
                        !emptyline)
                    maxseenwidth = linerenders[lines]->w;
                totalseenheight += linerenders[lines]->h;
                lines++;
                linefill = 0;
                linebuf[linefill] = '\0';
            } else {
                goto exitfailure;
            }
            if (uiobj->text[i] == '\0')
                break;
            if (uiobj->text[i] == '\r' &&
                    uiobj->text[i + 1] == '\n') {
                i++;
                continue;
            }
        } else {
            if (linefill >= maxlinelen)
                goto exitfailure;
            linebuf[linefill + 1] = '\0';
            linebuf[linefill] = uiobj->text[i];
            linefill++;
        }
    }

    // Create final surface to hold all lines in one:
    int result_srf_w = maxseenwidth;
    int result_srf_h = totalseenheight;
    if (result_srf_w < 16)
        result_srf_w = 16;
    if (result_srf_h < 16)
        result_srf_h = 16;
    result_srf_w = ceilpow(result_srf_w);
    result_srf_h = ceilpow(result_srf_h);
    resultsrf = SDL_CreateRGBSurfaceWithFormat(
        0, result_srf_w, result_srf_h, 32, SDL_PIXELFORMAT_ABGR8888
    );
    if (!resultsrf)
        goto exitfailure;
    SDL_FillRect(
        resultsrf, NULL,
        SDL_MapRGBA(resultsrf->format, 0, 0, 0, 0)
    );
    int rendery = 0;
    i = 0;
    while (i < lines) {
        SDL_Rect drect = {0};
        drect.x = 0;
        if (uiobj->textalign == TEXTALIGN_CENTER) {
            drect.x = (maxseenwidth - linerenders[i]->w) / 2;
        } else if (uiobj->textalign == TEXTALIGN_RIGHT) {
            drect.x = maxseenwidth - linerenders[i]->w;
        }
        assert(linerenders[i] != NULL);
        drect.y = rendery;
        SDL_BlitSurface(
            linerenders[i], NULL,
            resultsrf, &drect
        );
        rendery += linerenders[i]->h;
        SDL_FreeSurface(linerenders[i]);
        linerenders[i] = NULL;
        i++;
    }
    free(linerenders);
    free(linebuf);
    // Hack to fix weird subpixel behavior of SDL_TTF:
    {
        const int h = resultsrf->h;
        const int w = resultsrf->w;
        SDL_LockSurface(resultsrf);
        const int pitch = resultsrf->pitch;  // keep after LockSurface!
        char *p = resultsrf->pixels;
        int i = 0;
        while (i < h) {
            int x = 0;
            while (x < w * 4) {
                memset(p + x, 255, 3);
                x += 4;
            }
            p += pitch;
            i++;
        }
        SDL_UnlockSurface(resultsrf);
    }
    uiobj->rendered_text = resultsrf;
    uiobj->render_w = maxseenwidth;
    uiobj->render_h = totalseenheight;
    return uiobj->rendered_text;
}

h3duiobject *uiobject_CreateSprite(
        h3duiplane *plane, const char *texture_path
        ) {
    if (!uiobject_by_id_cache) {
        uiobject_by_id_cache = hash_NewBytesMap(64);
        if (!uiobject_by_id_cache)
            return NULL;
    }
    if (!plane)
        plane = defaultplane;
    if (!plane)
        return NULL;

    h3dtexture *tex = texture_GetTexture(texture_path);
    if (!tex)
        return NULL;
    h3duiobject *uiobj = malloc(sizeof(*uiobj));
    if (!uiobj)
        return NULL;
    memset(uiobj, 0, sizeof(*uiobj));
    _last_uiobj_id++;
    uiobj->id = _last_uiobj_id;
    uiobj->type = UIOBJECT_TYPE_SPRITE;
    uiobj->scale_x = 1;
    uiobj->scale_y = 1;
    uiobj->sprite = tex;

    uint64_t id = uiobj->id;
    if (!hash_BytesMapSet(
            uiobject_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)uiobj)) {
        uiobject_DestroyObject(uiobj);
        return NULL;
    }

    if (!uiplane_AddObject(plane, uiobj)) {
        uiobject_DestroyObject(uiobj);
        return NULL;
    }
    uiplane_UpdateObjectSort(plane, uiobj);

    return uiobj;
}

void uiobject_GetUnscaledNaturalDimensions(
        h3duiobject *uiobj, int *w, int *h
        ) {
    if (!uiobj) {
        *w = 0;
        *h = 0;
        return;
    }
    if (uiobj->type == UIOBJECT_TYPE_TEXT) {
        SDL_Surface *render = uiobject_GetTextRender(uiobj);
        if (!render) {
            fprintf(
                stderr, "horse3d/uiobject.c: warning: "
                "unexpectedly failed to render text of h3duiobject\n"
            );
            *w = 0;
            *h = 0;
            return;
        }
        *w = uiobj->render_w;
        *h = uiobj->render_h;
        return;
    } else if (uiobj->type == UIOBJECT_TYPE_SPRITE) {
        assert(uiobj->sprite);
        *w = ((double)uiobj->sprite->w);
        *h = ((double)uiobj->sprite->h);
        return;
    } else {
        fprintf(
            stderr, "horse3d/uiobject.c: warning: "
            "unhandled uiobj type %d for dimensions",
            uiobj->type
        );
        *w = 0;
        *h = 0;
    }
}

int uiobject_Draw(
        h3duiobject *uiobj,
        double additionaloffsetx, double additionaloffsety
        ) {
    h3drendertexref ref;
    memset(&ref, 0, sizeof(ref));
    if (uiobj->childrenplane)
        uiplane_Draw(
            uiobj->childrenplane,
            additionaloffsetx + uiobj->x,
            additionaloffsety + uiobj->y
        );
    if (uiobj->type == UIOBJECT_TYPE_TEXT) {
        if (!uiobj->_gluploaded) {
            SDL_Surface *render = uiobject_GetTextRender(uiobj);
            if (!render) {
                fprintf(
                    stderr, "horse3d/uiobject.c: warning: "
                    "unexpectedly failed to render text of h3duiobject\n"
                );
                return 0;
            }
            char *pixels = malloc(render->w * render->h * 4);
            if (!pixels)
                return 0;

            int i = 0;
            SDL_LockSurface(render);
            char *sp = render->pixels;
            char *tp = pixels;
            const int linelen = render->w * 4;
            while (i < render->h) {
                memcpy(tp, sp, linelen);
                sp += render->pitch;
                tp += linelen;
                i++;
            }
            SDL_UnlockSurface(render);

            _gl->glGenTextures(1, &uiobj->gluploadedid);
            _gl->glActiveTexture(GL_TEXTURE0);
            _gl->glBindTexture(GL_TEXTURE_2D, uiobj->gluploadedid);
            _gl->glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA, render->w, render->h, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels
            );
            _gl->glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE
            );
            _gl->glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE
            );
            _gl->glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR
            );
            _gl->glTexParameteri(
                GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR
            );
            _gl->glBindTexture(GL_TEXTURE_2D, 0);
            free(pixels);
            uiobj->_gluploaded = 1;
        }
        assert(uiobj->_gluploaded);
        ref.type = H3DRENDERREF_TYPE_GLREF;
        ref.glref = uiobj->gluploadedid;
    } else if (uiobj->type == UIOBJECT_TYPE_SPRITE) {
        ref.type = H3DRENDERREF_TYPE_H3DTEXTURE;
        ref.tex = uiobj->sprite;
    } else {
        fprintf(
            stderr, "horse3d/uiobject.c: warning: "
            "unhandled uiobj type %d for drawing",
            uiobj->type
        );
        return 0;
    }

    int w = 0;
    int h = 0;
    uiobject_GetUnscaledNaturalDimensions(uiobj, &w, &h);
    int render_w = w;
    int render_h = h;
    if (uiobj->type == UIOBJECT_TYPE_TEXT) {
        render_w = uiobj->render_w;
        render_h = uiobj->render_h;
    }
    double postopleft[2];
    postopleft[0] = -((double)w) * 0.5 * uiobj->scale_x;
    postopleft[1] = -((double)h) * 0.5 * uiobj->scale_y;
    double postopright[2];
    postopright[0] = (((double)w) * 0.5 + (
        render_w - w
    )) * uiobj->scale_x;
    postopright[1] = -(((double)h) * 0.5 * uiobj->scale_y);
    double posbottomleft[2];
    posbottomleft[0] = -((double)w) * 0.5 * uiobj->scale_x;
    posbottomleft[1] = (((double)h) * 0.5 + (
        render_h - h
    )) * uiobj->scale_y;
    double posbottomright[2];
    posbottomright[0] = (((double)w) * 0.5 + (
        render_w - w
    )) * uiobj->scale_x;
    posbottomright[1] = (((double)h) * 0.5 + (
        render_h - h
    )) * uiobj->scale_y;

    vec2_rotate(
        postopleft, postopleft, (uiobj->rotation / 180.0) * M_PI
    );
    vec2_rotate(
        postopright, postopright, (uiobj->rotation / 180.0) * M_PI
    );
    vec2_rotate(
        posbottomleft, posbottomleft, (uiobj->rotation / 180.0) * M_PI
    );
    vec2_rotate(
        posbottomright, posbottomright, (uiobj->rotation / 180.0) * M_PI
    );

    postopleft[0] += uiobj->x + ((double)w) * 0.5 * uiobj->scale_x;
    postopleft[1] += uiobj->y + ((double)h) * 0.5 * uiobj->scale_y;
    postopright[0] += uiobj->x + ((double)w) * 0.5 * uiobj->scale_x;
    postopright[1] += uiobj->y + ((double)h) * 0.5 * uiobj->scale_y;
    posbottomleft[0] += uiobj->x + ((double)w) * 0.5 * uiobj->scale_x;
    posbottomleft[1] += uiobj->y + ((double)h) * 0.5 * uiobj->scale_y;
    posbottomright[0] += uiobj->x + ((double)w) * 0.5 * uiobj->scale_x;
    posbottomright[1] += uiobj->y + ((double)h) * 0.5 * uiobj->scale_y;

    if (!render3d_RenderTriangle2D(
            &ref,
            postopleft[0], postopleft[1],
            postopright[0], postopright[1],
            posbottomleft[0], posbottomleft[1],
            0.0, 0.0, 1.0, 0.0, 0.0, 1.0
            ))
        return 0;
    if (!render3d_RenderTriangle2D(
            &ref,
            postopright[0], postopright[1],
            posbottomright[0], posbottomright[1],
            posbottomleft[0], posbottomleft[1],
            1.0, 0.0, 1.0, 1.0, 0.0, 1.0
            ))
        return 0;
    return 1;
}

h3duiobject *uiobject_GetObjectById(uint64_t id) {
    if (!uiobject_by_id_cache)
        return NULL;
    uint64_t queryid = id;
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            uiobject_by_id_cache,
            (char*)&queryid, sizeof(queryid),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3duiobject*)(void*)hashptrval;
}

void uiobject_DestroyObject(h3duiobject *uiobj) {
    if (!uiobj)
        return;
    if (uiobj->childrenplane) {
        uiplane_Destroy(uiobj->childrenplane);
        return;
    }
    if (uiobj->parentplane)
        uiplane_RemoveObject(uiobj->parentplane, uiobj);
    uint64_t queryid = uiobj->id;
    hash_BytesMapUnset(
        uiobject_by_id_cache,
        (char*)&queryid, sizeof(queryid)
    );
    if (uiobj->type == UIOBJECT_TYPE_TEXT) {
        if (uiobj->text)
            free(uiobj->text);
        if (uiobj->rendered_text)
            SDL_FreeSurface(uiobj->rendered_text);
    } else if (uiobj->type == UIOBJECT_TYPE_SPRITE) {
    }
    if (uiobj->_gluploaded) {
        _gl->glDeleteTextures(
            1, &uiobj->gluploadedid
        );
    }
    free(uiobj);
}

h3duiplane *uiobject_GetOrCreateChildPlane(
        h3duiobject *obj
        ) {
    if (obj->childrenplane)
        return obj->childrenplane;

    h3duiplane *p = uiplane_New();
    if (!p)
        return NULL;
    if (!uiplane_MarkAsParented(p)) {
        uiplane_Destroy(p);
        return NULL;
    }

    obj->childrenplane = p;
    p->parentobject = obj;
    return p;
}

h3duiobject *uiobject_GetParent(h3duiobject *uiobj) {
    if (!uiobj->parentplane ||
            !uiobj->parentplane->parentobject)
        return NULL;

    return uiobj->parentplane->parentobject;
}

void uiobject_RemoveFromParent(h3duiobject *uiobj) {
    if (!uiobj)
        return;

    if (uiobj->parentplane) {
        uiplane_RemoveObject(uiobj->parentplane, uiobj);
        uiobj->parentplane = NULL;
    }
}

int uiobject_AddChild(
        h3duiobject *uiobj, h3duiobject *newchild, char **error
        ) {
    if (!uiobj || !newchild) {
        if (error)
            *error = strdup("empty pointer was supplied");
        return 0;
    }

    h3duiplane *plane = uiobject_GetOrCreateChildPlane(uiobj);
    if (!plane) {
        if (error)
            *error = strdup(
                "internal error: failed to create child plane"
            );
        return 0;
    }

    if (newchild->parentplane && newchild->parentplane == plane)
        return 1;

    // Check if to-be-added child is already in our parent chain:
    h3duiobject *parent = uiobject_GetParent(uiobj);
    while (parent) {
        if (parent == newchild) {
            if (error)
                *error = strdup("adding this child would introduce cycle");
            return 1;
        }
        parent = uiobject_GetParent(parent);
    }

    return (uiplane_AddObject(plane, uiobj) != 0);
}
