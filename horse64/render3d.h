#ifndef HORSE3D_RENDER3D_H_
#define HORSE3D_RENDER3D_H_

#include <SDL2/SDL.h>
#include "glheaders.h"

typedef struct h3dtexture h3dtexture;
typedef struct h3dmesh h3dmesh;
typedef struct h3dobject h3dobject;

typedef struct h3dcamera {
    double pos[3];
    double fov;
    double look_quat[4];
} h3dcamera;

#define H3DRENDERREF_TYPE_H3DTEXTURE 0
#define H3DRENDERREF_TYPE_GLREF 1
typedef struct h3drendertexref {
    int type;
    union {
        h3dtexture *tex;
        GLuint glref;
    };
} h3drendertexref;


extern h3dcamera *maincam;
extern int render3d_projectionmatrix_cached;
extern double render3d_fps;
extern double render3d_framems;


int render3d_SetMode(
    int windowed
);

int render3d_IsWindowed();

void render3d_GetOutputSize(int *w, int *h);

void render3d_EndFrame();

int render3d_Draw3DLine(
    double x1, double y1, double z1,
    double x2, double y2, double z2,
    double r, double g, double b, double thickness
);

int render3d_RenderTriangle3D(
    h3drendertexref *tex,
    double x1, double y1, double z1,
    double x2, double y2, double z2,
    double x3, double y3, double z3,
    double tx1, double ty1,
    double tx2, double ty2,
    double tx3, double ty3,
    int unlit, int transparent,
    int additive, int worldtransform
);

int render3d_RenderTriangle2D(
    h3drendertexref *tex,
    double x1, double y1,
    double x2, double y2,
    double x3, double y3,
    double tx1, double ty1,
    double tx2, double ty2,
    double tx3, double ty3
);

int render3d_RenderMeshForObject(
    h3dmesh *mesh,
    int transparent_parts,
    h3dobject *obj
);

void render3d_UpdateViewport();

SDL_Window *render3d_GetOutputWindow();

double render3d_GetWindowDPI();

void render3d_CloseWindow();

int render3d_HaveGLOutput();

void render3d_MinimizeWindow();

#endif  // HORSE3D_RENDER3D_H_
