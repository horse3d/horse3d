#ifndef HORSE3D_GLHEADERS_H_
#define HORSE3D_GLHEADERS_H_

#include <string.h>

#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengles2.h>

typedef void (*_glDebugMessageCallbackPtr)(
    GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length,
    const GLchar* message, const void* userParam
);

typedef struct SDL_gles2funcs {
    #define SDL_PROC(ret,func,params) ret (APIENTRY *func) params;
    #include "../vendor/SDL_gles2funcs.h"
    #undef SDL_PROC

    void (*glDebugMessageCallback)(
        _glDebugMessageCallbackPtr callback, void* userParam
    );
    void (*glUniform3fv)(
        GLint location, GLsizei count, const GLfloat *value
    );
    void (*glUniform1fv)(
        GLint location, GLsizei count, const GLfloat *value
    );
    void (*glUniform1iv)(
        GLint location, GLsizei count, const GLint *value
    );
    void (*glBlendFunc)(GLenum sfactor, GLenum dfactor);
} SDL_gles2funcs;  // based on SDL2's test/testgles2.c

static SDL_gles2funcs *SDL_GL_LoadES2Funcs() {
    SDL_gles2funcs *data = SDL_malloc(sizeof(*data));
    if (!data) {
        return NULL;
    }
    memset(data, 0, sizeof(*data));

    #if SDL_VIDEO_DRIVER_UIKIT
    #define __SDL_NOGETPROCADDR__
    #elif SDL_VIDEO_DRIVER_ANDROID
    #define __SDL_NOGETPROCADDR__
    #elif SDL_VIDEO_DRIVER_PANDORA
    #define __SDL_NOGETPROCADDR__
    #endif

    #if defined __SDL_NOGETPROCADDR__
    #define SDL_PROC(ret,func,params) data->func=func;
    #else
    #define SDL_PROC(ret,func,params) \
        do { \
            data->func = SDL_GL_GetProcAddress(#func); \
            if ( ! data->func ) { \
                SDL_SetError("Couldn't load GLES2 function %s: %s", \
                             #func, SDL_GetError()); \
                SDL_free(data); \
                return NULL; \
            } \
        } while ( 0 );
    #endif /* __SDL_NOGETPROCADDR__ */

    #include "../vendor/SDL_gles2funcs.h"
    #undef SDL_PROC

    data->glDebugMessageCallback = SDL_GL_GetProcAddress(
        "glDebugMessageCallback"
    );
    data->glBlendFunc = SDL_GL_GetProcAddress(
        "glBlendFunc"
    );
    data->glUniform3fv = SDL_GL_GetProcAddress(
        "glUniform3fv"
    );
    data->glUniform1fv = SDL_GL_GetProcAddress(
        "glUniform1fv"
    );
    data->glUniform1iv = SDL_GL_GetProcAddress(
        "glUniform1iv"
    );

    return data;
}  // also based on SDL2's test/testgles2.c

#endif  // HORSE3D_GLHEADERS_H_
