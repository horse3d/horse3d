#ifndef HORSE3D_UIOBJECT_H_
#define HORSE3D_UIOBJECT_H_

#include <SDL2/SDL.h>

#include "glheaders.h"
#include "texture.h"
#include "uiobject.h"
#include "uiplane.h"


#define UIOBJECT_TYPE_SPRITE 1
#define UIOBJECT_TYPE_TEXT 2

#define TEXTALIGN_LEFT 1
#define TEXTALIGN_CENTER 2
#define TEXTALIGN_RIGHT 3


typedef struct fontinfo {
    char *name;
    double ptsize;
} fontinfo;

typedef struct h3duiplane h3duiplane;

typedef struct h3duiobject {
    uint64_t id;
    h3duiplane *parentplane;

    double zindex;
    double x, y;
    double scale_x, scale_y;
    double rotation;
    int type;
    union {
        h3dtexture *sprite;
        struct {
            char *fontloadpath;
            fontinfo font;
            char *text;
            int textalign;
            double fixedwidth;
            SDL_Surface *rendered_text;
            int render_w, render_h;
        };
    };
    int _gluploaded;
    GLuint gluploadedid;

    h3duiplane *childrenplane;
} h3duiobject;


h3duiobject *uiobject_CreateText(
    h3duiplane *plane,
    const char *font_name_or_path, double ptsize, const char *text
);

int uiobject_ChangeObjectFont(
    h3duiobject *obj,
    const char *font_name_or_path, double ptsize
);

int uiobject_ChangeObjectText(
    h3duiobject *obj, const char *text
);

h3duiobject *uiobject_CreateSprite(
    h3duiplane *plane, const char *texture_path
);

void uiobject_GetUnscaledNaturalDimensions(
    h3duiobject *item, int *w, int *h
);

int uiobject_Draw(
    h3duiobject *uiobj, double additionaloffsetx, double additionaloffsety
);

void uiobject_DestroyObject(h3duiobject *uiobj);

h3duiobject *uiobject_GetObjectById(uint64_t id);

h3duiplane *uiobject_GetOrCreateChildPlane(
    h3duiobject *obj
);

int uiobject_AddChild(
    h3duiobject *uiobj, h3duiobject *newchild, char **error
);

void uiobject_RemoveFromParent(h3duiobject *uiobj);

h3duiobject *uiobject_GetParent(h3duiobject *uiobj);

#endif  // HORSE3D_UIOBJECT_H_
