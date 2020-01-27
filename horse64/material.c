
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glheaders.h"
#include "hash.h"
#include "lights.h"
#include "material.h"
#include "render3d.h"


h3dmaterial **global_materials = NULL;
int global_materials_count = 0;
hashmap *material_by_name = NULL;
extern SDL_gles2funcs *_gl;
static h3dmaterial *_defaultmaterial = NULL;
static h3dmaterial *_defaultmaterialunlit = NULL;
static h3dmaterial *_activerendermaterial = NULL;
static int _activeh3dvattributes = H3DVATTRIBUTES_NONE;
h3dmaterial *_material_NewEx(
    const char *name, int haslightinput
);
// H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV locations:
const GLuint h3dvattribs_pos_loc = 0;
const GLuint h3dvattribs_normal_loc = 3;
const GLuint h3dvattribs_uv_loc = 6;


static void _createdefaultmaterials() {
    if (!_defaultmaterial)
        _defaultmaterial = _material_NewEx(
            "default", 1
        );
    if (!_defaultmaterialunlit)
        _defaultmaterialunlit = _material_NewEx(
            "defaultunlit", 0
        );
}

void material_ResetRenderState() {
    material_DisableVertexLayout();
    _activerendermaterial = NULL;
}

h3dmaterial *material_GetDefault() {
    _createdefaultmaterials();
    return _defaultmaterial;
}

h3dmaterial *material_GetDefaultUnlit() {
    _createdefaultmaterials();
    return _defaultmaterialunlit;
}

char *_glsl_without_version(const char *s) {
    char *result = strdup(s);
    if (!result)
        return NULL;
    int lastwaslinebreak = 1;
    int i = 0;
    while (i < strlen(result)) {
        if (lastwaslinebreak) {
            char *p = &result[i];
            if (strcasecmp(p, "#version ") == 0) {
                int k = i + 1;
                while (result[k] != '\0' &&
                       result[k] != '\r' && result[k] != '\n')
                    k++;
                memmove(p, p + k, strlen(p) - k + 1);
                i++;
                continue;
            }
        }
        i++;
    }
    return result;
}

char *material_glsl_cleanshader(
        const char *s, int isfragment, int haslightinputs
        ) {
    if (!s)
        return NULL;
    char *shader = _glsl_without_version(s);
    if (!shader)
        return NULL;

    char lightinputs[4096] = "";
    if (isfragment && haslightinputs) {
        assert(MAXLIGHTS > 0);
        snprintf(
            lightinputs, sizeof(lightinputs) - 1,
            "uniform vec3 h3d_LightPosition[%d];\n"
            "uniform vec3 h3d_LightColor[%d];\n"
            "uniform float h3d_LightRange[%d];\n"
            "uniform int h3d_LightFalloffType[%d];\n"
            "uniform int h3d_BothsidedLight;\n"
            "#define H3D_LIGHTCOUNT %d\n"
            "vec3 h3d_ComputeLight() {\n"
            "    vec3 light = vec3(0.0, 0.0, 0.0);\n"
            "    vec3 into_screen_vec = vec3(0.0, -1.0, 0.0);\n"
            "    int i = 0;\n"
            "    while (i < %d) {\n"
            "        float distance = max(0.001, distance(\n"
            "            h3d_Position, h3d_LightPosition[i]\n"
            "        ));\n"
            "        vec3 lightnormal = normalize(\n"
            "            h3d_Position - h3d_LightPosition[i]\n"
            "        );\n"
            "        float angle_to_normal = acos(\n"
            "            dot(lightnormal, normalize(h3d_Normal))\n"
            "        );\n"
            "        float anglefac = max(\n"
            "            0.0, (angle_to_normal / M_PI) - 0.5\n"
            "        );\n"
            "        float anglefac2 = max(\n"
            "            0.0, 1.0 - (angle_to_normal / M_PI) * 2.0\n"
            "        );\n"
            "        if (h3d_BothsidedLight == 1 &&\n"
            "                acos(dot(normalize(h3d_Position - h3d_CameraPosition),\n"
            "                         h3d_Normal)) < M_PI * 0.5) {\n"
            "            anglefac = anglefac2;\n"
            "        }\n"
            "        float range = h3d_LightRange[i];\n"
            "        float diststrength = 0.0;\n"
            "        if (h3d_LightFalloffType[i] == 0)\n"
            "            diststrength = (\n"
            "                (2.0 - min(0.5, distance / range) * 1.5\n"
            "                 - max(0.0, (distance / range) - 0.5) * 1.0)\n"
            "            );\n"
            "        if (h3d_LightFalloffType[i] == 1)\n"
            "            diststrength += (1.0 / max(0.01, (distance / range) + 0.2)) * 0.8 +\n"
            "                (\n"
            "                    (2.0 - min(0.5, distance / range) * 1.5\n"
            "                     - max(0.0, (distance / range) - 0.5) * 1.0)\n"
            "                ) * 0.2;\n"
            "        if (diststrength < 0.0) {diststrength = 0.0;}\n"
            "        light.r += anglefac * diststrength * h3d_LightColor[i].r * 10.0;\n"
            "        light.g += anglefac * diststrength * h3d_LightColor[i].g * 10.0;\n"
            "        light.b += anglefac * diststrength * h3d_LightColor[i].b * 10.0;\n"
            "        i++;\n"
            "    }\n"
            "    return light;"
            "}\n",
            MAXLIGHTS, MAXLIGHTS, MAXLIGHTS, MAXLIGHTS,
            MAXLIGHTS, MAXLIGHTS
        );
    }

    char prepend[4096] = "";
    snprintf(
        prepend, sizeof(prepend) - 1,
        "#version %s\n%s\n%s\n"
        "#if __VERSION__ > 120\n#define texture2D texture\n#endif\n"
        "%s\n%s\n",
        #if !defined(HORSE3D_DESKTOPGL)
        "100",  // GLSL 1.0 ES
        #else
        "110",  // GLSL 1.1 (desktop)
        #endif
        "#define M_PI 3.1415926538",
        #if !defined(HORSE3D_DESKTOPGL)
        "#ifndef H3D_MOBILE\n#define H3D_MOBILE 1\nprecision mediump float;\n#endif\n",
        #else
        "#ifdef H3D_MOBILE\n#define H3D_MOBILE 0\n#endif\n",
        #endif
        (isfragment ?
            "uniform sampler2D h3d_Texture;\n"
            "varying vec3 h3d_Normal;\n"
            "varying vec2 h3d_TexCoords;\n"
            "varying vec3 h3d_Position;\n"
            "uniform vec3 h3d_CameraPosition;\n"
            "varying vec3 h3d_ScreenSpacePosition;\n"
            "varying vec3 h3d_ScreenSpaceNormal;\n"
            :
            "attribute vec3 h3d_Normal_input;\n"
            "attribute vec4 h3d_LocalPosition_input;\n"
            "attribute vec2 h3d_TexCoords_input;\n"
            "uniform vec3 h3d_CameraPosition;\n"
            "varying vec3 h3d_Normal;\n"
            "varying vec3 h3d_ScreenSpaceNormal;\n"
            "varying vec2 h3d_TexCoords;\n"
            "varying vec3 h3d_Position;\n"
            "varying vec3 h3d_ScreenSpacePosition;\n"
            "uniform mat4 h3d_ModelMatrix;\n"
            "uniform mat4 h3d_ModelRotationMatrix;\n"
            "uniform mat4 h3d_ProjectionMatrix;\n"
        ),
        lightinputs
    );
    char *transformed = malloc(
        strlen(prepend) + 1 +
        strlen(shader) + 1
    );
    if (!transformed) {
        free(shader);
        return strdup("horse3d/material.c: shader alloc failed\n");
    }
    memcpy(
        transformed, prepend, strlen(prepend)
    );
    transformed[strlen(prepend)] = '\n';
    memcpy(
        transformed + strlen(prepend) + 1, shader,
        strlen(shader) + 1
    );
    free(shader);
    return transformed;
}

void material_DisableVertexLayout() {
    if (_activeh3dvattributes == H3DVATTRIBUTES_NONE)
        return;
    int oldlayout = _activeh3dvattributes;
    _activeh3dvattributes = H3DVATTRIBUTES_NONE;
    if (oldlayout == H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV) {
        _gl->glDisableVertexAttribArray(h3dvattribs_normal_loc);
        _gl->glDisableVertexAttribArray(h3dvattribs_pos_loc);
        _gl->glDisableVertexAttribArray(h3dvattribs_uv_loc);
    }
}

void material_ChangeVertexLayoutTo(int layout) {
    if (_activeh3dvattributes == layout)
        return;
    _activeh3dvattributes = layout;
    if (layout == H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV) {
        _gl->glEnableVertexAttribArray(h3dvattribs_normal_loc);
        _gl->glEnableVertexAttribArray(h3dvattribs_pos_loc);
        _gl->glEnableVertexAttribArray(h3dvattribs_uv_loc);
    }
}

int material_SetActive(
        h3dmaterial *m, GLuint posvbo, GLuint uvvbo,
        GLuint normalsvbo, int litbothsided,
        h3dlight **active_lights,
        char **error
        ) {
    if (!m) {
        if (error)
            *error = strdup("NULL supplied as material");
        return 0;
    }
    if (!material_CreateGLProgram(m, error) ||
            m->vertexattribslayout !=
                H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV
            )
        return 0;

    int programchanged = 0;
    if (_activerendermaterial != m) {
        if (_activerendermaterial) {
            if (_activerendermaterial->vertexattribslayout !=
                    m->vertexattribslayout)
                material_DisableVertexLayout();
            _activerendermaterial = NULL;
        }
        programchanged = 1;
        _gl->glUseProgram(
            m->glprogramid
        );
    }
    _activerendermaterial = m;

    if (m->vertexattribslayout ==
            H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV) {
        _gl->glBindBuffer(GL_ARRAY_BUFFER, normalsvbo);
        _gl->glVertexAttribPointer(
            h3dvattribs_normal_loc, 3, GL_FLOAT, GL_FALSE, 0, 0
        );
        _gl->glBindBuffer(GL_ARRAY_BUFFER, posvbo);
        _gl->glVertexAttribPointer(
            h3dvattribs_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, 0
        );
        _gl->glBindBuffer(GL_ARRAY_BUFFER, uvvbo);
        _gl->glVertexAttribPointer(
            h3dvattribs_uv_loc, 2, GL_FLOAT, GL_FALSE, 0, 0
        );
    } else {
        fprintf(stderr, "horse3d/material.c: warning "
                "unknown vertexattribslayout %d\n",
                m->vertexattribslayout);
    }

    material_ChangeVertexLayoutTo(
        m->vertexattribslayout
    );
    if (m->haslightinput) {
        _gl->glUniform1i(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_BothsidedLight"
            ),
            (litbothsided != 0)
        );
        float lightpositions[MAXLIGHTS * 3];
        float lightcolors[MAXLIGHTS * 3];
        float lightrange[MAXLIGHTS];
        int lightfallofftypes[MAXLIGHTS];
        int k = 0;
        while (k < MAXLIGHTS) {
            if (!active_lights[k]) {
                memset(
                    &lightpositions[k * 3], 0,
                    sizeof(*lightpositions) * 3
                );
                memset(
                    &lightcolors[k * 3], 0,
                    sizeof(*lightcolors) * 3
                );
                lightrange[k] = 0;
                lightfallofftypes[k] = 0;
                k++;
                continue;
            }
            double pos[3];
            lights_GetPosition(active_lights[k], pos);
            lightpositions[k * 3 + 0] = pos[0];
            lightpositions[k * 3 + 1] = pos[1];
            lightpositions[k * 3 + 2] = pos[2];
            h3dlightsettings *lsettings = (
                lights_GetLightSettings(active_lights[k])
            );
            lightfallofftypes[k] = lsettings->fallofftype;
            double f = lights_GetFadeFactor(active_lights[k]);
            lightcolors[k * 3 + 0] = lsettings->r * f;
            lightcolors[k * 3 + 1] = lsettings->g * f;
            lightcolors[k * 3 + 2] = lsettings->b * f;
            lightrange[k] = lsettings->range;
            k++;
        }
        _gl->glUniform3fv(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_LightPosition"
            ), MAXLIGHTS * 3, lightpositions
        );
        int err;
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/material.c/material_SetActive(): "
                "glUniform3fv() -> "
                "glGetError() = %d\n",
                err
            );
        }
        _gl->glUniform3fv(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_LightColor"
            ), MAXLIGHTS, lightcolors
        );
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/material.c/material_SetActive(): "
                "glUniform3fv() -> "
                "glGetError() = %d\n",
                err
            );
        }
        _gl->glUniform1iv(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_LightFalloffType"
            ), MAXLIGHTS, lightfallofftypes
        );
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/material.c/material_SetActive(): "
                "glUniform3fv() -> "
                "glGetError() = %d\n",
                err
            );
        }
        _gl->glUniform3fv(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_LightPosition"
            ), MAXLIGHTS * 3, lightpositions
        );
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/material.c/material_SetActive(): "
                "glUniform3fv() -> "
                "glGetError() = %d\n",
                err
            );
        }
        _gl->glUniform1fv(
            _gl->glGetUniformLocation(
                m->glprogramid, "h3d_LightRange"
            ), MAXLIGHTS, lightrange
        );
        if ((err = _gl->glGetError()) != GL_NO_ERROR) {
            printf(
                "horse3d/material.c/material_SetActive(): "
                "glUniform3fv() -> "
                "glGetError() = %d\n",
                err
            );
        }
    }

    return 1;
}

h3dmaterial *material_ByName(const char *name) {
    if (!material_by_name) {
        material_by_name = hash_NewStringMap(32);
        if (!material_by_name)
            return NULL;
    }
    uintptr_t p = 0;
    if (!hash_StringMapGet(material_by_name, name, &p))
        return NULL;
    return (h3dmaterial *)(void *)p;
}

h3dmaterial *material_New(
        const char *name
        ) {
    return _material_NewEx(name, 1);
}

h3dmaterial *_material_NewEx(
        const char *name, int haslightinput
        ) {
    // Check name being valid:
    if (!name)
        return NULL;
    if (!material_by_name) {
        material_by_name = hash_NewStringMap(32);
        if (!material_by_name)
            return NULL;
    }
    uintptr_t p;
    if (hash_StringMapGet(material_by_name, name, &p)) {
        if (p != 0)
            return NULL;  // name already taken.
    }

    // Add global material slot:
    if (global_materials_count <= 0 ||
            global_materials[global_materials_count - 1] != NULL) {
        h3dmaterial **global_materials_new = realloc(
            global_materials,
            sizeof(*global_materials) * (global_materials_count + 1)
        );
        if (!global_materials_new)
            return NULL;
        global_materials = global_materials_new;
        global_materials[global_materials_count] = NULL;
        global_materials_count++;
    }

    // Create material:
    h3dmaterial *m = malloc(sizeof(*m));
    if (!m)
        return NULL;
    memset(m, 0, sizeof(*m));
    m->haslightinput = (haslightinput != 0);
    m->name = strdup(name);
    if (!m->name) {
        free(m);
        return NULL;
    }
    m->vertexattribslayout = H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV;
    if (!hash_StringMapSet(
            material_by_name, m->name, (uintptr_t)m
            )) {
        free(m->name);
        free(m);
        return NULL;
    }

    // Set shaders:
    char *shadererror = NULL;
    if (!material_SetVertexShader(m,
            "void main() {\n"
            "    h3d_Position = (\n"
            "        h3d_ModelMatrix * h3d_LocalPosition_input\n"
            "    ).xyz;\n"
            "    vec4 h3d_ScreenSpacePos4 = (h3d_ProjectionMatrix * (\n"
            "        vec4(h3d_Position, 1.0)));\n"
            "    h3d_Normal = normalize(\n"
            "        (h3d_ModelRotationMatrix *\n"
            "         vec4(normalize(h3d_Normal_input), 1.0)).xyz\n"
            "    );\n"
            "    h3d_ScreenSpaceNormal = normalize(\n"
            "        (h3d_ProjectionMatrix *\n"
            "         vec4(h3d_Normal + h3d_Position, 1.0)).xyz -\n"
            "         h3d_ScreenSpacePos4.xyz\n"
            "    );\n"
            "    h3d_ScreenSpacePosition = h3d_ScreenSpacePos4.xyz;\n"
            "    h3d_TexCoords = h3d_TexCoords_input;\n"
            "    gl_Position = h3d_ScreenSpacePos4;\n"
            "}\n", &shadererror)) {
        fprintf(
            stderr, "horse3d/material.c: warning: "
            "failed to set default vertex shader: %s\n",
            shadererror
        );
        free(shadererror);
    }
    shadererror = NULL;
    if (haslightinput) {
        if (!material_SetFragmentShader(m,
                "void main() {\n"
                "    vec4 texcolor = texture2D(\n"
                "        h3d_Texture, h3d_TexCoords);\n"
                "    if (texcolor[3] < 0.2)\n"
                "        discard;\n"
                "    vec4 light = vec4(h3d_ComputeLight(), 1.0);\n"
                "    texcolor.r *= light.r;\n"
                "    texcolor.g *= light.g;\n"
                "    texcolor.b *= light.b;\n"
                "    gl_FragColor = texcolor;\n"
                "}\n", &shadererror)) {
            fprintf(
                stderr, "horse3d/material.c: warning: "
                "failed to set default fragment shader: %s\n",
                shadererror
            );
            free(shadererror);
        }
    } else {
        if (!material_SetFragmentShader(m,
                "void main() {\n"
                "    vec4 texcolor = texture2D(\n"
                "        h3d_Texture, h3d_TexCoords);\n"
                "    if (texcolor[3] < 0.2)\n"
                "        discard;\n"
                "    gl_FragColor = texcolor;\n"
                "}\n", &shadererror)) {
            fprintf(
                stderr, "horse3d/material.c: warning: "
                "failed to set default fragment shader: %s\n",
                shadererror
            );
            free(shadererror);
        }
    }

    shadererror = NULL;
    if (!material_CreateGLProgram(m, &shadererror)) {
        fprintf(
            stderr, "horse3d/material.c: warning: "
            "failed to create GL program: %s\n",
            shadererror
        );
        free(shadererror);
    }
    return m;
}

static char *_getshadercompileerror(GLuint shaderid, int *goterror) {
    char buf[512];
    GLenum err;
    if ((err = _gl->glGetError()) != GL_NO_ERROR) {
        snprintf(
            buf, sizeof(buf) - 1,
            "glGetError() = %d", err
        );
        *goterror = 1;
        return strdup(buf);
    }
    GLint success = 0;
    _gl->glGetShaderiv(shaderid, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        snprintf(
            buf, sizeof(buf) - 1,
            "shader id %d compile failed: ",
            (int)shaderid
        );
        GLint logSize = 0;
        _gl->glGetShaderiv(shaderid, GL_INFO_LOG_LENGTH, &logSize);
        if (logSize <= 0) {
            snprintf(
                buf, sizeof(buf) - 1,
                "shader id %d compile failed: GL_INFO_LOG_LENGTH is 0 !!",
                (int)shaderid
            );
            *goterror = 1;
            return strdup(buf);
        }
        char *finalmsg = malloc(strlen(buf) + logSize + 1);
        if (!finalmsg) {
            snprintf(buf, sizeof(buf) - 1, "failed to allocate error buffer");
            *goterror = 1;
            return strdup(buf);
        }
        memcpy(finalmsg, buf, strlen(buf));
        char *logp = finalmsg + strlen(buf);
        _gl->glGetShaderInfoLog(
            shaderid, logSize, &logSize,
            logp
        );
        logp[logSize] = '\0';
        while (strlen(logp) > 0 &&
               (logp[strlen(logp) - 1] == '\n' ||
                logp[strlen(logp) - 1] == '\r'))
            logp[strlen(logp) - 1] = '\0';
        *goterror = 1;
        return finalmsg;
    }
    *goterror = 0;
    return NULL;
}

int material_CreateGLProgram(h3dmaterial *m, char **error) {
    if (!m->fshaderset || !m->vshaderset) {
        if (error)
            *error = strdup("no shaders (compile fail?)");
        return 0;
    }
    if (m->glprogramset)
        return 1;
    m->glprogramset = 1;
    m->glprogramid = _gl->glCreateProgram();
    _gl->glAttachShader(
        m->glprogramid, m->vshaderid
    );
    int err = 0;
    if ((err = _gl->glGetError()) != 0) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1, "failed to attach vertex "
            "shader, glGetError()=%d",
            err
        );
        if (error)
            *error = strdup(buf);
        m->glprogramset = 0;
        return 0;
    }
    _gl->glAttachShader(
        m->glprogramid, m->fshaderid
    );
    err = 0;
    if ((err = _gl->glGetError()) != 0) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1, "failed to attach fragment "
            "shader, glGetError()=%d",
            err
        );
        if (error)
            *error = strdup(buf);
        m->glprogramset = 0;
        return 0;
    }

    if (m->vertexattribslayout ==
            H3DVATTRIBUTES_3FLOATPOS_3FLOATNORM_2FLOATUV
            ) {
        _gl->glBindAttribLocation(
            m->glprogramid, h3dvattribs_normal_loc, "h3d_Normal_input"
        );
        _gl->glBindAttribLocation(
            m->glprogramid, h3dvattribs_pos_loc, "h3d_LocalPosition_input"
        );
        _gl->glBindAttribLocation(
            m->glprogramid, h3dvattribs_uv_loc, "h3d_TexCoords_input"
        );
    } else {
        fprintf(stderr, "horse3d/material.c: internal error, "
                "unknown attribs layout\n");
    }

    _gl->glLinkProgram(
        m->glprogramid
    );
    err = 0;
    if ((err = _gl->glGetError()) != 0) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1, "failed to link shader program "
            "shader, glGetError()=%d",
            err
        );
        if (error)
            *error = strdup(buf);
        m->glprogramset = 0;
        return 0;
    }
    return 1;
}

int material_SetVertexShader(
        h3dmaterial *m, const char *vshader, char **error
        ) {
    if (m->glprogramset) {
        _gl->glDeleteProgram(m->glprogramid);
        m->glprogramset = 0;
    }

    char *s = material_glsl_cleanshader(vshader, 0, m->haslightinput);
    if (!s) {
        if (error)
            *error = strdup("string operations for cleanup failed");
        return 0;
    }
    #ifdef HORSE3D_GLDEBUG
    fprintf(
        stderr, "horse3d/material.c: debug: "
        "compiling this vertex shader:\n\n%s\n\n",
        s
    );
    #endif
    if (m->vshaderset) {
        _gl->glDeleteShader(m->vshaderid);
        m->vshaderset = 0;
    }
    int _clearerror = _gl->glGetError();
    m->vshaderid = _gl->glCreateShader(GL_VERTEX_SHADER);
    _gl->glShaderSource(
        m->vshaderid, 1, (const GLchar**)&s, NULL
    );
    free(s);

    _gl->glCompileShader(m->vshaderid);
    int goterror = 0;
    char *err = _getshadercompileerror(m->vshaderid, &goterror);
    if (goterror) {
        _gl->glDeleteShader(m->vshaderid);
        m->vshaderset = 0;
        if (err && error)
            *error = err;
        else if (error)
            *error = strdup(
                "failed to compile shader but got no error"
            );
        return 0;
    }
    m->vshaderset = 1;
    return 1;
}

int material_SetFragmentShader(
        h3dmaterial *m, const char *fshader, char **error
        ) {
    if (m->glprogramset) {
        _gl->glDeleteProgram(m->glprogramid);
        m->glprogramset = 0;
    }

    char *s = material_glsl_cleanshader(fshader, 1, m->haslightinput);
    if (!s) {
        if (error)
            *error = strdup("string operations for cleanup failed");
        return 0;
    }
    #ifdef HORSE3D_GLDEBUG
    fprintf(
        stderr, "horse3d/material.c: debug: "
        "compiling this fragment shader:\n\n%s\n\n",
        s
    );
    #endif
    if (m->fshaderset) {
        _gl->glDeleteShader(m->fshaderid);
        m->fshaderset = 0;
    }
    int _clearerror = _gl->glGetError();
    m->fshaderid = _gl->glCreateShader(GL_FRAGMENT_SHADER);
    _gl->glShaderSource(
        m->fshaderid, 1, (const GLchar**)&s, NULL
    );
    free(s);

    _gl->glCompileShader(m->fshaderid);
    int goterror = 0;
    char *err = _getshadercompileerror(m->fshaderid, &goterror);
    if (goterror) {
        _gl->glDeleteShader(m->fshaderid);
        m->fshaderset = 0;
        if (err && error)
            *error = err;
        else
            *error = strdup(
                "failed to compile shader but got no error"
            );
        return 0;
    }
    m->fshaderset = 1;
    return 1;
}
