#ifndef HORSE3D_MATERIAL_H_
#define HORSE3D_MATERIAL_H_

#include "glheaders.h"

#define H3DVATTRIBUTES_NONE 0
#define H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV 1

typedef struct h3dlight h3dlight;

typedef struct h3dmaterial {
    int vshaderset, fshaderset;
    GLuint vshaderid, fshaderid;

    int haslightinput;

    int vertexattribslayout;

    int glprogramset;
    int glprogramid;

    char *name;

    char *vshader_text, *fshader_text;
} h3dmaterial;


void material_ResetRenderState();

h3dmaterial *material_GetDefault();

h3dmaterial *material_GetDefaultUnlit();

h3dmaterial *material_New(const char *name);

h3dmaterial *material_ByName(const char *name);

void material_DisableVertexLayout();

void material_ChangeVertexLayoutTo(int layout);

int material_SetActive(
    h3dmaterial *m, GLuint posvbo, GLuint uvvbo,
    GLuint normalsvbo, int litbothsided,
    h3dlight **active_lights,
    char **error
);

int material_CreateGLProgram(
    h3dmaterial *m, char **error
);

char *material_glsl_cleanshader(
    const char *s, int isfragment, int haslightinput
);

int material_SetVertexShader(
    h3dmaterial *m, const char *vshader, char **error
);

int material_SetFragmentShader(
    h3dmaterial *m, const char *fshader, char **error
);

#endif  // HORSE3D_MATERIAL_H_
