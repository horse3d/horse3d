
#include <assert.h>
#if defined(__WIN32) || defined(__WIN64)
#include <windows.h>
#endif
#include <stdio.h>
#include <string.h>

#define SDL_PIXELFORMAT_XBGR8888 SDL_PIXELFORMAT_BGR888
//#define DEBUGLIGHTRENDER

#include <mathc/mathc.h>
#include <SDL2/SDL.h>

#include "datetime.h"
#include "filesys.h"
#include "glheaders.h"
#include "hash.h"
#include "lights.h"
#include "material.h"
#include "math3d.h"
#include "meshes.h"
#include "render3d.h"
#include "texture.h"
#include "uiplane.h"
#include "world.h"


h3dcamera *maincam = NULL;
static SDL_Window *mainwindow = NULL;
SDL_GLContext *_glcontext = NULL;
SDL_gles2funcs *_gl = NULL;
static int _in_windowed = -1;
static int _need_frame_start = 1;
static int FATALGLERROR = 0;

static int known_render_w = -1;
static int known_render_h = -1;
static int gl_viewport_ever_set = 0;
static h3dlight **active_lights = NULL;

int _texture_LoadTextureGL(h3dtexture *tex, int alpha);
int _mesh_UploadToGL(h3dmesh *mesh, int forcereupload);


__attribute__((constructor)) void _init_cam() {
    maincam = malloc(sizeof(*maincam));
    if (!maincam)
        return;
    memset(maincam, 0, sizeof(*maincam));
    maincam->fov = 80;
}

SDL_Window *render3d_GetOutputWindow() {
    return mainwindow;
}

int render3d_HaveGLOutput() {
    return (_glcontext != NULL);
}

void render3d_MinimizeWindow() {
    if (!mainwindow)
        return;
    SDL_MinimizeWindow(mainwindow);
}

void render3d_CloseWindow() {
    if (_glcontext) {
        SDL_GL_DeleteContext(_glcontext);
        _glcontext = NULL;
    }
    if (_gl) {
        free(_gl);
        _gl = NULL;
    }
    _in_windowed = -1;
    _need_frame_start = -1;
    known_render_w = -1;
    known_render_h = -1;
    gl_viewport_ever_set = 0;
    if (mainwindow) {
        SDL_DestroyWindow(mainwindow);
        mainwindow = NULL;
    }
}

void render3d_GetOutputSize(int *w, int *h) {
    if (_in_windowed == -1) {
        if (!render3d_SetMode(1)) {
            if (!mainwindow) {
                if (w) *w = 0;
                if (h) *h = 0;
                return;
            }
        }
    }
    if (!mainwindow) {
        if (w)
            *w = 0;
        if (h)
            *h = 0;
        return;
    }
    if (_glcontext && !FATALGLERROR) {
        int known_render_w_new = 0;
        int known_render_h_new = 0;
        SDL_GL_GetDrawableSize(
            mainwindow, &known_render_w_new, &known_render_h_new
        );
        if (known_render_w_new < 1)
            known_render_w_new = 1;
        if (known_render_h_new < 1)
            known_render_h_new = 1;
        if (known_render_w_new != known_render_w ||
                known_render_h_new != known_render_h ||
                !gl_viewport_ever_set) {
            gl_viewport_ever_set = 1;
            _gl->glViewport(
                0, 0,
                (GLsizei)known_render_w_new,
                (GLsizei)known_render_h_new
            );
            uiplane_ResizeScreenPlanes(known_render_w, known_render_h);
        }
        known_render_w = known_render_w_new;
        known_render_h = known_render_h_new;
    } else {
        double dpi = render3d_GetWindowDPI();
        int ww = 0;
        int wh = 0;
        SDL_GetWindowSize(mainwindow, &ww, &wh);
        known_render_w = (double)ww * dpi;
        known_render_h = (double)wh * dpi;
        uiplane_ResizeScreenPlanes(known_render_w, known_render_h);
    }
    if (w)
        *w = known_render_w;
    if (h)
        *h = known_render_h;
}

double render3d_GetWindowDPI() {
    // FIXME: use SDL_GetWindowDPI() or similar, once available
    if (!mainwindow || !_glcontext)
        return 1.0;

    int draww, drawh;
    SDL_GL_GetDrawableSize(
        mainwindow, &draww, &drawh
    );
    int ww, wh;
    SDL_GetWindowSize(mainwindow, &ww, &wh);

    return (((double)draww) / ((double)ww));
}

void render3d_UpdateViewport() {
    render3d_GetOutputSize(NULL, NULL);
}

static void glDebugMessageCallbackPtr(
        GLenum source, GLenum type, GLuint id,
        GLenum severity, GLsizei length,
        const GLchar* message, const void* userParam
        ) {
    fprintf(stderr,
            "horse3d/render3d.c: debug: "
            "GLdebug(severity=%d): \"%s\"\n", severity, message);
    fflush(stderr);
}

int render3d_SetMode(int windowed) {
    if (mainwindow && _in_windowed == (windowed != 0))
        return 1;
    if (!mainwindow && FATALGLERROR)
        return 0;
    if (mainwindow) {
        if (windowed) {
            SDL_SetWindowFullscreen(mainwindow, 0);
            _in_windowed = 1;
        } else {
            SDL_SetWindowFullscreen(
                mainwindow, SDL_WINDOW_FULLSCREEN_DESKTOP
            );
            _in_windowed = 0;
        }
        return 1;
    }

    #ifdef HORSE3D_GLDEBUG
    printf("horse3d/render3d.c: debug: calling SDL_CreateWindow()\n");
    fflush(stdout);
    #endif

    // Create new window:
    mainwindow = SDL_CreateWindow(
        "Horse3D",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 700, SDL_WINDOW_OPENGL |
        (windowed ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP) |
        SDL_WINDOW_RESIZABLE
    );
    if (!mainwindow)
        return 0;

    SDL_DisableScreenSaver();

    #if !defined(HORSE3D_DESKTOPGL)
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES
    );
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    #else
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE
    );
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    #endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    _glcontext = SDL_GL_CreateContext(mainwindow);
    if (!_glcontext) {
        FATALGLERROR = 1;
        fprintf(stderr, "horse3d/render3d: warning: "
                "failed to create GL context\n");
        fflush(stderr);
        return 0;
    }

    _gl = SDL_GL_LoadES2Funcs();
    if (!_gl) {
        FATALGLERROR = 1;
        fprintf(stderr, "horse3d/render3d: warning: "
                "failed to get GL functions\n");
        fflush(stderr);
        SDL_GL_DeleteContext(_glcontext);
        _glcontext = NULL;
        return 0;
    }

    int swapresult = 0;
    #if defined(__ANDROID__) || defined(ANDROID)
    swapresult = SDL_GL_SetSwapInterval(1);
    #else
    if ((swapresult = SDL_GL_SetSwapInterval(-1)) < 0)
        swapresult = SDL_GL_SetSwapInterval(1);
    #endif
    if (swapresult < 0) {
        fprintf(stderr, "horse3d/render3d: warning: "
                "failed to enable vsync: %s\n", SDL_GetError());
        fflush(stderr);
    }
    _in_windowed = (windowed != 0);

    #ifdef HORSE3D_GLDEBUG
    if (SDL_GL_ExtensionSupported("GL_KHR_debug") &&
            _gl->glDebugMessageCallback) {
        printf("horse3d/render3d.c: debug: glDebugMessageCallback set\n");
        fflush(stdout);
        _gl->glDebugMessageCallback(glDebugMessageCallbackPtr, NULL);
        _gl->glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
        int err;
        if ((err = _gl->glGetError()) != 0 && err != 1280) {
            fprintf(
                stderr,
                "horse3d/render3d.c: error: setting debug output (1): "
                "glGetError()=%d\n",
                err
            );
        }
        _gl->glEnable(GL_DEBUG_OUTPUT_KHR);
        if ((err = _gl->glGetError()) != 0) {
            fprintf(
                stderr,
                "horse3d/render3d.c: error: setting debug output (1): "
                "glGetError()=%d\n",
                err
            );
        }
    }
    #endif
    FATALGLERROR = 0;
    return 1;
}

void _render3d_StartFrame() {
    if (_in_windowed == -1) 
        if (!render3d_SetMode(1))
            return;
    if (!_gl)
        return;

    lights_UpdateFade();

    render3d_UpdateViewport();
    _gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    _gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    render3d_projectionmatrix_cached = 0;
    material_ResetRenderState();
    _need_frame_start = 0;

    double position[3];
    position[0] = maincam->pos[0];
    position[1] = maincam->pos[1];
    position[2] = maincam->pos[2];
    if (active_lights) {
        free(active_lights);
        active_lights = NULL;
    }
    active_lights = lights_EnableNthActiveLights(
        position, MAXLIGHTS
    );
    #if defined(DEBUGLIGHTRENDER) && !defined(NDEBUG)
    int k = 0;
    while (k < MAXLIGHTS) {
        if (active_lights[k]) {
            lights_GetPosition(active_lights[k], position);
            printf(
                "horse3d/render3d.c: verbose: "
                "render light: light slot %d at %f,%f,%f\n", k,
                position[0], position[1], position[2]
            );
        } else {
            printf(
                "horse3d/render3d.c: verbose: "
                "render light: light slot %d EMPTY\n", k
            );
        }
        k++;
    }
    #endif
}


static float matrixbuf[16];

static GLfloat *_matrixdbltofl(double *m) {
    unsigned int i = 0;
    assert(sizeof(matrixbuf) / sizeof(*matrixbuf) == 16);
    while (i < sizeof(matrixbuf) / sizeof(*matrixbuf)) {
        matrixbuf[i] = m[i];
        i++;
    }
    return matrixbuf;
}

int render3d_Draw3DLine(
        double x1, double y1, double z1,
        double x2, double y2, double z2,
        double r, double g, double b, double thickness
        ) {
    double dist1 = sqrt(
        pow(x1 - maincam->pos[0], 2) +
        pow(y1 - maincam->pos[1], 2) +
        pow(z1 - maincam->pos[2], 2)
    );
    double dist2 = sqrt(
        pow(x2 - maincam->pos[0], 2) +
        pow(y2 - maincam->pos[1], 2) +
        pow(z2 - maincam->pos[2], 2)
    );

    double viewport_pos1[3];
    viewport_pos1[0] = x1;
    viewport_pos1[1] = y1;
    viewport_pos1[2] = z1;
    double camreversequat[4];
    quat_inverse(camreversequat, maincam->look_quat);
    viewport_pos1[0] -= maincam->pos[0];
    viewport_pos1[1] -= maincam->pos[1];
    viewport_pos1[2] -= maincam->pos[2];
    vec3_rotate_quat(viewport_pos1, viewport_pos1, camreversequat);
    double viewport_pos2[3];
    viewport_pos2[0] = x2;
    viewport_pos2[1] = y2;
    viewport_pos2[2] = z2;
    viewport_pos2[0] -= maincam->pos[0];
    viewport_pos2[1] -= maincam->pos[1];
    viewport_pos2[2] -= maincam->pos[2];
    vec3_rotate_quat(viewport_pos2, viewport_pos2, camreversequat);
    double screenspace2dorthovec[2];
    screenspace2dorthovec[0] = -(viewport_pos2[0] - viewport_pos1[0]);
    screenspace2dorthovec[1] = -(viewport_pos2[2] - viewport_pos1[2]);
    double len = vec2_length(screenspace2dorthovec);
    if (len * 3.0 < fabs(viewport_pos2[1] - viewport_pos1[1])) {
        // Assume line is mostly along Y axis / into the screen.
        if (fabs(viewport_pos1[2]) < fabs(viewport_pos1[0])) {
            screenspace2dorthovec[0] = 1;
            screenspace2dorthovec[1] = 0;
        } else {
            screenspace2dorthovec[0] = 0;
            screenspace2dorthovec[1] = 1;
        }
    } else {
        screenspace2dorthovec[0] /= len;
        screenspace2dorthovec[1] /= len;
    }

    screenspace2dorthovec[0] *= thickness;
    screenspace2dorthovec[1] *= thickness;
    double pos1thickness = 0.1;
    double pos2thickness = 0.1;
    if (dist1 > 0.0001)
        pos1thickness = (dist1) * 0.01;
    if (dist2 > 0.0001)
        pos2thickness = (dist2) * 0.01;
    double _tmp = screenspace2dorthovec[0];
    screenspace2dorthovec[0] = screenspace2dorthovec[1];
    screenspace2dorthovec[1] = _tmp;
    double quatvertices[12];
    memcpy(&quatvertices[0], viewport_pos1, sizeof(viewport_pos1));
    quatvertices[0] -= screenspace2dorthovec[1] * pos1thickness;
    quatvertices[2] += screenspace2dorthovec[0] * pos1thickness;
    memcpy(&quatvertices[3], viewport_pos1, sizeof(viewport_pos1));
    quatvertices[3] += screenspace2dorthovec[0] * pos1thickness;
    quatvertices[5] -= screenspace2dorthovec[1] * pos1thickness;
    memcpy(&quatvertices[6], viewport_pos2, sizeof(viewport_pos2));
    quatvertices[6] -= screenspace2dorthovec[0] * pos2thickness;
    quatvertices[8] += screenspace2dorthovec[1] * pos2thickness;
    memcpy(&quatvertices[9], viewport_pos2, sizeof(viewport_pos2));
    quatvertices[9] += screenspace2dorthovec[0] * pos2thickness;
    quatvertices[11] -= screenspace2dorthovec[1] * pos2thickness;
    int i = 0;
    while (i < 12) {
        vec3_rotate_quat(
            &quatvertices[i], &quatvertices[i], maincam->look_quat
        );
        quatvertices[i] += maincam->pos[0];
        quatvertices[i + 1] += maincam->pos[1];
        quatvertices[i + 2] += maincam->pos[2];
        i += 3;
    }

    x1 = (quatvertices[0] + quatvertices[3]) / 2;
    y1 = (quatvertices[1] + quatvertices[4]) / 2;
    z1 = (quatvertices[2] + quatvertices[5]) / 2;
    x2 = (quatvertices[6] + quatvertices[9]) / 2;
    y2 = (quatvertices[7] + quatvertices[10]) / 2;
    z2 = (quatvertices[8] + quatvertices[11]) / 2;

    h3drendertexref ref;
    memset(&ref, 0, sizeof(ref));
    ref.tex = texture_GetBlankTexture();
    if (!render3d_RenderTriangle3D(
            &ref,
            quatvertices[0],
            quatvertices[1],
            quatvertices[2],
            quatvertices[3],
            quatvertices[4],
            quatvertices[5],
            quatvertices[9],
            quatvertices[10],
            quatvertices[11],
            0, 1,
            1, 1,
            1, 0,
            1, 0, 0, 1
            ))
        return 0;
    if (!render3d_RenderTriangle3D(
            &ref,
            quatvertices[0],
            quatvertices[1],
            quatvertices[2],
            quatvertices[6],
            quatvertices[7],
            quatvertices[8],
            quatvertices[9],
            quatvertices[10],
            quatvertices[11],
            0, 1,
            1, 1,
            1, 0,
            1, 0, 0, 1
            ))
        return 0;
    return 1;
}

int render3d_projectionmatrix_cached = 0;
double render3d_projectionmatrix[16];
double render3d_inversecamrotationmatrix[16];

static double myuptozup[16];
static double myuptozup_reverse[16];
static double midentity[16];

__attribute__((constructor)) static void _initmyztoup() {
    mat4_identity(myuptozup);
    mat4_rotation_x(myuptozup, -M_PI * 0.5);
    mat4_identity(myuptozup_reverse);
    mat4_rotation_x(myuptozup_reverse, M_PI * 0.5);
    mat4_identity(midentity);
}

static void render3d_ApplyProjectionToShader(
        GLuint shader_program_id,
        h3dcamera *cam,
        h3dobject *obj
        ) {
    float campos[3];
    campos[0] = cam->pos[0];
    campos[1] = cam->pos[1];
    campos[2] = cam->pos[2];
    if (!render3d_projectionmatrix_cached) {
        render3d_projectionmatrix_cached = 1;
        double w_to_h = (known_render_h / (double)known_render_w);
        double mixed_fov = (
            (cam->fov * w_to_h * 0.6 + cam->fov * 0.4)
        );
        if (mixed_fov > 120)
            mixed_fov = 120;

        double mcampersp[16];
        memset(mcampersp, 0, sizeof(*mcampersp) * 16);
        mat4_perspective_fov(
            mcampersp, (mixed_fov / 180.0) * M_PI,
            known_render_w, known_render_h,
            0.1, 500.0
        );

        double mcamtransl[16];
        memset(mcamtransl, 0, sizeof(*mcamtransl) * 16);
        mat4_identity(mcamtransl);
        double v[3];
        v[0] = -cam->pos[0];
        v[1] = -cam->pos[1];
        v[2] = -cam->pos[2];
        mat4_translate(
            mcamtransl, mcamtransl, v
        );

        double mcamrot[16];
        memset(mcamrot, 0, sizeof(*mcamrot) * 16);
        double quat[4];
        mat4_identity(mcamrot);
        memcpy(quat, cam->look_quat, sizeof(quat));
        quat_normalize(quat, quat);
        quat_inverse(quat, quat);
        quat_normalize(quat, quat);
        mat4_rotation_quat(
            mcamrot, quat
        );
        memcpy(render3d_inversecamrotationmatrix, mcamrot,
               sizeof(mcamrot));

        double mcam[16];
        mat4_identity(mcam);
        mat4_multiply(mcam, mcamtransl, mcam);
        mat4_multiply(mcam, mcamrot, mcam);
        mat4_multiply(mcam, myuptozup, mcam);
        mat4_multiply(mcam, mcampersp, mcam);
        memcpy(render3d_projectionmatrix, mcam, sizeof(mcam));
    }

    _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
        shader_program_id, "h3d_ProjectionMatrix"
    ), 1, GL_FALSE, _matrixdbltofl(render3d_projectionmatrix));
    _gl->glUniform3fv(_gl->glGetUniformLocation(
        shader_program_id, "h3d_CameraPosition"
    ), 1, campos);

    if (obj && !obj->objecttransformcached) {
        obj->objecttransformcached = 1;
        double m[16];
        memset(m, 0, sizeof(*m) * 16);
        mat4_identity(m);
        double v[3];
        v[0] = -obj->visual_offset_x;
        v[1] = -obj->visual_offset_y;
        v[2] = -obj->visual_offset_z;
        mat4_translate(m, m, v);
        if (obj->scale > 0.00001) {
            v[0] = obj->scale;
            v[1] = obj->scale;
            v[2] = obj->scale;
            double m2[16];
            mat4_identity(m2);
            mat4_scaling(m2, m2, v);
            mat4_multiply(m, m2, m);
        }
        {  // rotation
            double m2[16];
            mat4_identity(m2);
            double objectquat[4];
            memcpy(objectquat, obj->quat, sizeof(objectquat));
            quat_normalize(objectquat, objectquat);
            mat4_rotation_quat(m2, objectquat);
            memcpy(obj->objecttransform_rotationonly, m2, sizeof(m2));
            mat4_multiply(m, m2, m);
        }
        v[0] = obj->x;
        v[1] = obj->y;
        v[2] = obj->z;
        mat4_translate(
            m, m, v
        );
        memcpy(obj->objecttransform, m, sizeof(m));
    }
    if (obj) {
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            shader_program_id, "h3d_ModelMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(obj->objecttransform));
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            shader_program_id, "h3d_ModelRotationMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(obj->objecttransform_rotationonly));
    } else {
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            shader_program_id, "h3d_ModelMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(midentity));
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            shader_program_id, "h3d_ModelRotationMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(midentity));
    }
}


int render3d_IsWindowed() {
    if (!_in_windowed || _in_windowed < 0)
        return 0;
    return 1;
}


int render3d_RenderMeshForObject(
        h3dmesh *mesh,
        int transparent_parts,
        h3dobject *obj
        ) {
    if (_in_windowed == -1)
        if (!render3d_SetMode(1))
            return 0;
    if (!_gl)
        return 0;
    if (_need_frame_start)
        _render3d_StartFrame();

    if (!_mesh_UploadToGL(mesh, 0)) {
        fprintf(
            stderr,
            "horse3d/render3d.c: failed to upload mesh\n"
        );
        return 0;
    }

    h3dtexture *missingtex = texture_GetMissingImageTexture();
    if (!missingtex) {
        fprintf(
            stderr,
            "horse3d/render3d.c: failed to create 'missingtex' texture\n"
        );
        return 0;
    }

    int i = 0;
    while (i < mesh->default_geometry.texturepart_count) {
        mesh3dtexturepart *part = (
            &mesh->default_geometry.texturepart[i]
        );
        if ((!part->tex && !missingtex) ||
                part->attribute_invisible ||
                part->vertexcount <= 0 ||
                part->polycount <= 0 ||
                part->attribute_withalpha != transparent_parts) {
            i++;
            continue;
        }
        if (part->tex &&
                !_texture_LoadTextureGL(
                part->tex, part->attribute_withalpha)) {
            fprintf(
                stderr,
                "horse3d/render3d.c: failed to load tex\n"
            );
            i++;
            continue;
        }
        assert(part->_gluploaded);
        if (!part->tex && missingtex &&
                !_texture_LoadTextureGL(
                missingtex, 0
                )) {
            fprintf(
                stderr,
                "horse3d/render3d.c: failed to load tex\n"
            );
            i++;
            continue;
        }
        assert(part->tex || (missingtex && missingtex->isglloaded));

        _gl->glEnable(GL_DEPTH_TEST);
        if (!part->attribute_withalpha) {
            _gl->glCullFace(GL_BACK);
            _gl->glEnable(GL_CULL_FACE);
        } else {
            _gl->glDisable(GL_CULL_FACE);
        }
        meshgeopart_AssignMaterial(part);
        char *error = NULL;
        if (!part->assignedmaterial || !material_SetActive(
                part->assignedmaterial,
                part->_glvbo, part->_glvbouv, part->_glvbonormals,
                part->attribute_withalpha,
                active_lights, &error
                )) {
            if (!part->assignedmaterial) {
                if (error)
                    free(error);
                error = strdup(
                    "failed to determine material to be assigned"
                );
            }
            fprintf(
                stderr,
                "horse3d/render3d.c: failed to use material "
                "for mesh part (texname=\"%s\"): %s\n",
                (part->tex ? part->tex->filepath : "<none>"), error
            );
            if (error) free(error);
            i++;
            continue;
        }

        assert(
            (!part->tex && missingtex && missingtex->isglloaded) ||
            (part->tex && part->attribute_withalpha &&
             part->tex->isglloadedalpha) ||
            (part->tex && !part->attribute_withalpha &&
             part->tex->isglloaded)
        );
        _gl->glActiveTexture(GL_TEXTURE0);
        if (part->tex) {
            _gl->glBindTexture(GL_TEXTURE_2D, (
                part->attribute_withalpha ?
                part->tex->gltexturealpha :
                part->tex->gltexture
            ));
        } else {
            _gl->glBindTexture(GL_TEXTURE_2D, missingtex->gltexture);
        }
        if (part->attribute_withalpha || 1) {
            _gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            _gl->glEnable(GL_BLEND);
        }
        _gl->glUniform1i(_gl->glGetUniformLocation(
            part->assignedmaterial->glprogramid, "h3d_Texture"
        ), 0);
        {
            int err;
            if ((err = _gl->glGetError()) != GL_NO_ERROR) {
                printf(
                    "horse3d/render3d.c/render3d_RenderMesh(): "
                    "glUniform1i(getUniformLocation"
                        "(\"h3d_Texture\"), 0) -> "
                    "glGetError() = %d\n", err
                );
            }
        }

        render3d_ApplyProjectionToShader(
            part->assignedmaterial->glprogramid, maincam,
            obj
        );

        _gl->glDrawArrays(GL_TRIANGLES, 0, part->polycount * 3);
        {
            int err;
            if ((err = _gl->glGetError()) != GL_NO_ERROR) {
                printf(
                    "horse3d/render3d.c/render3d_RenderMesh(): "
                    "glDrawArrays() -> "
                    "glGetError() = %d\n", err
                );
            }
        }

        i++;
    }
    return 1;
}

static GLint _triangle_vbo_pos = -1;
static GLint _triangle_vbo_normals = -1;
static GLint _triangle_vbo_coords = -1;

int render3d_RenderTriangle3D(
        h3drendertexref *texref,
        double x1, double y1, double z1,
        double x2, double y2, double z2,
        double x3, double y3, double z3,
        double tx1, double ty1,
        double tx2, double ty2,
        double tx3, double ty3,
        int unlit, int withalpha,
        int additive, int worldtransform
        ) {
    if (_in_windowed == -1)
        if (!render3d_SetMode(1))
            return 0;
    if (!_gl)
        return 0;
    if (_need_frame_start)
        _render3d_StartFrame();

    if (texref->type == H3DRENDERREF_TYPE_H3DTEXTURE) {
        if (!_texture_LoadTextureGL(texref->tex, withalpha)) {
            fprintf(
                stderr,
                "horse3d/render3d.c: warning: "
                "failed to load tex\n"
            );
            return 0;
        }
    }

    if (worldtransform) {
        _gl->glEnable(GL_DEPTH_TEST);  
    } else {
        _gl->glDisable(GL_DEPTH_TEST);
    }
    _gl->glDisable(GL_CULL_FACE);

    if (_triangle_vbo_normals < 0) {
        _gl->glGenBuffers(1, (unsigned int *)&_triangle_vbo_normals);
        int err;
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/render3d.c/render3d_RenderTriangle3D(): "
                "error: glGenBuffers() -> "
                "glGetError() = %d\n",
                err
            );
            _triangle_vbo_normals = -1;
            return 0;
        }
    }
    assert(_triangle_vbo_normals >= 0);

    GLfloat normalsbuf[9];
    float normal[3];
    if (worldtransform) {
        double pos1[3];
        pos1[0] = x1;
        pos1[1] = y1;
        pos1[2] = z1;
        double pos2[3];
        pos2[0] = x1;
        pos2[1] = y1;
        pos2[2] = z1;
        double pos3[3];
        pos3[0] = x1;
        pos3[1] = y1;
        pos3[2] = z1;
        double normaldbl[3];
        polygon3d_normal(pos1, pos2, pos3, normaldbl);
        normal[0] = normaldbl[0];
        normal[1] = normaldbl[1];
        normal[2] = normaldbl[2];
    } else {
        normal[0] = 0;
        normal[1] = 0;
        normal[2] = 0;
    }
    memcpy(&normalsbuf[0], normal, sizeof(normal));
    memcpy(&normalsbuf[3], normal, sizeof(normal));
    memcpy(&normalsbuf[6], normal, sizeof(normal));
    _gl->glBindBuffer(GL_ARRAY_BUFFER, _triangle_vbo_normals);
    _gl->glBufferData(
        GL_ARRAY_BUFFER, sizeof(normalsbuf), normalsbuf, GL_STATIC_DRAW
    );

    if (_triangle_vbo_pos < 0) {
        _gl->glGenBuffers(1, (unsigned int *)&_triangle_vbo_pos);
        int err;
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/render3d.c/render3d_RenderTriangle3D(): "
                "error: glGenBuffers() -> "
                "glGetError() = %d\n",
                err
            );
            _triangle_vbo_pos = -1;
            return 0;
        }
    }
    assert(_triangle_vbo_pos >= 0);

    GLfloat vertices[9];
    if (worldtransform) {
        vertices[0] = x1;
        vertices[1] = y1;
        vertices[2] = z1;
        vertices[3] = x2;
        vertices[4] = y2;
        vertices[5] = z2;
        vertices[6] = x3;
        vertices[7] = y3;
        vertices[8] = z3;
    } else {
        vertices[0] = (-1.0 + (x1 / (double)known_render_w) * 2.0);
        vertices[1] = (1.0 - (y1 / (double)known_render_h) * 2.0);
        vertices[2] = 0;
        vertices[3] = (-1.0 + (x2 / (double)known_render_w) * 2.0);
        vertices[4] = (1.0 - (y2 / (double)known_render_h) * 2.0);
        vertices[5] = 0;
        vertices[6] = (-1.0 + (x3 / (double)known_render_w) * 2.0);
        vertices[7] = (1.0 - (y3 / (double)known_render_h) * 2.0);
        vertices[8] = 0;
    }
    _gl->glBindBuffer(GL_ARRAY_BUFFER, _triangle_vbo_pos);
    _gl->glBufferData(
        GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW
    );

    if (_triangle_vbo_coords < 0) {
        _gl->glGenBuffers(1, (unsigned int *)&_triangle_vbo_coords);
        int err;
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/render3d.c/render3d_RenderTriangle3D(): "
                "error: glGenBuffers() -> "
                "glGetError() = %d\n",
                err
            );
            _triangle_vbo_coords = -1;
            return 0;
        }
    }
    GLuint vbocoords;
    GLfloat texcoords[6];
    texcoords[0] = tx1;
    texcoords[1] = ty1;
    texcoords[2] = tx2;
    texcoords[3] = ty2;
    texcoords[4] = tx3;
    texcoords[5] = ty3;
    _gl->glBindBuffer(GL_ARRAY_BUFFER, _triangle_vbo_coords);
    _gl->glBufferData(
        GL_ARRAY_BUFFER, sizeof(texcoords), texcoords, GL_STATIC_DRAW
    );

    h3dmaterial *m = material_GetDefaultUnlit();
    char *error = NULL;
    if (!material_SetActive(
            m, _triangle_vbo_pos, _triangle_vbo_coords,
            _triangle_vbo_normals, 0,
            active_lights, &error
            )) {
        fprintf(
            stderr,
            "horse3d/render3d.c: error: "
            "failed to use material "
            "for %s triangle: %s\n",
            (worldtransform ? "3d" : "2d"), error
        );
        free(error);
        return 0;
    }

    if (texref->type == H3DRENDERREF_TYPE_H3DTEXTURE) {
        assert(
            (withalpha && texref->tex->isglloadedalpha) ||
            (!withalpha && texref->tex->isglloaded)
        );
    }
    _gl->glActiveTexture(GL_TEXTURE0);
    if (texref->type == H3DRENDERREF_TYPE_H3DTEXTURE) {
        _gl->glBindTexture(GL_TEXTURE_2D, (
            withalpha ? texref->tex->gltexturealpha :
            texref->tex->gltexture
        ));
    } else if (texref->type == H3DRENDERREF_TYPE_GLREF) {
        _gl->glBindTexture(GL_TEXTURE_2D, texref->glref);
    } else {
        fprintf(stderr,
            "horse3d/render3d.c: warning: unknown texref type "
            "%d", texref->type
        );
    }
    _gl->glUniform1i(_gl->glGetUniformLocation(
        m->glprogramid, "h3d_Texture"
    ), 0);

    if (worldtransform) {
        render3d_ApplyProjectionToShader(
            m->glprogramid, maincam, NULL
        );
    } else {
        double mt[16];
        memset(mt, 0, sizeof(*mt));
        mat4_identity(mt);
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            m->glprogramid, "h3d_ProjectionMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(mt));
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            m->glprogramid, "h3d_ModelMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(mt));
        _gl->glUniformMatrix4fv(_gl->glGetUniformLocation(
            m->glprogramid, "h3d_ModelRotationMatrix"
        ), 1, GL_FALSE, _matrixdbltofl(mt));
        float nullpos[3];
        memset(nullpos, 0, sizeof(*nullpos));
        _gl->glUniform3fv(_gl->glGetUniformLocation(
            m->glprogramid, "h3d_CameraPosition"
        ), 1, nullpos);
    }

    _gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    {
        int err;
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/render3d.c/render3d_RenderTriangle3D(): "
                "error: glDrawArrays() -> "
                "glGetError() = %d\n", err
            );
            return 0;
        }
    }
    return 1;
}

int render3d_RenderTriangle2D(
        h3drendertexref *tex,
        double x1, double y1,
        double x2, double y2,
        double x3, double y3,
        double tx1, double ty1,
        double tx2, double ty2,
        double tx3, double ty3
        ) {
    return render3d_RenderTriangle3D(
        tex,
        x1, y1, 1.0,
        x2, y2, 1.0,
        x3, y3, 1.0,
        tx1, ty1,
        tx2, ty2,
        tx3, ty3,
        1, 1, 0, 0
    );
}

static uint64_t endframe_bulk_startts = 0;
static int endframe_bulk_count = 0;
static const int endframe_bulk_sumlen = 10;
double render3d_fps = 30;
double render3d_framems = (1000.0 / 30);

void render3d_EndFrame() {
    if (_in_windowed == -1)
        if (!render3d_SetMode(1))
            return;
    if (!_gl)
        return;

    if (_need_frame_start) {
        _need_frame_start = 0;
        _render3d_StartFrame();
    }

    material_ResetRenderState();

    _need_frame_start = 1;
    SDL_GL_SwapWindow(mainwindow);

    if (active_lights) {
        free(active_lights);
        active_lights = NULL;
    }

    if (endframe_bulk_count <= 0) {
        endframe_bulk_startts = datetime_Ticks();
    }
    endframe_bulk_count++;
    if (endframe_bulk_count >= endframe_bulk_sumlen) {
        uint64_t tenframespan = (
            datetime_Ticks() - endframe_bulk_startts
        );
        double tenframespanfloat = tenframespan;
        double framems = (
            tenframespanfloat / (double)endframe_bulk_sumlen
        );
        render3d_framems = framems;
        render3d_fps = (1000.0 / framems);
        endframe_bulk_count = 0;
        endframe_bulk_startts = datetime_Ticks();
    }
}
