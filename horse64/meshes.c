
#include <assert.h>
#include <mathc/mathc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "glheaders.h"
#include "hash.h"
#include "material.h"
#include "meshes.h"
#include "meshes_gltf.h"
#include "meshes_obj.h"
#include "texture.h"
#include "vfs.h"


//#define DEBUG_MESH_LOADER
//#define DEBUG_MESH_TEXTURE_FINDER


extern SDL_gles2funcs *_gl;
int _texture_LoadTextureGL(h3dtexture *tex, int alpha);
void mesh_DestroyMesh(h3dmesh *mesh);


hashmap *_seen_textures = NULL;
hashmap *mesh_cache = NULL;


void _clean_seen_textures() {
    hash_FreeMap(_seen_textures);
    _seen_textures = NULL;
}


int mesh_LocateModelReferencedFilePath(
        const char *model_path, const char *referenced_file_path,
        char **result_relative, char **result_absolute
        ) {
    if (!referenced_file_path || !model_path) {
        return 0;
    }
    char *modelfolder_path = filesys_ParentdirOfItem(model_path);
    if (!modelfolder_path)
        return 0;

    char *filepath_reltomodel_abs = filesys_Join(
        modelfolder_path, referenced_file_path
    );
    if (!filepath_reltomodel_abs) {
        free(modelfolder_path);
        return 0;
    }
    char *filepath_reltomodel = filesys_TurnIntoPathRelativeTo(
        filepath_reltomodel_abs, NULL
    );
    if (!filepath_reltomodel) {
        free(modelfolder_path);
        free(filepath_reltomodel_abs);
        return 0;
    }
    char *filepath_reltocwd = filesys_TurnIntoPathRelativeTo(
        referenced_file_path, NULL
    );
    if (!filepath_reltocwd) {
        free(filepath_reltomodel);
        free(filepath_reltomodel_abs);
        free(modelfolder_path);
        return 0;
    }
    char *filepath_reltocwd_abs = filesys_ToAbsolutePath(
        filepath_reltocwd
    );
    if (!filepath_reltocwd_abs) {
        free(filepath_reltocwd);
        free(filepath_reltomodel);
        free(filepath_reltomodel_abs);
        free(modelfolder_path);
        return 0;
    }
    int exists1 = 0;
    if (!vfs_Exists(filepath_reltomodel_abs, &exists1))
        goto existsfail;
    if (exists1) {
        free(modelfolder_path);
        free(filepath_reltocwd);
        free(filepath_reltocwd_abs);
        *result_absolute = filepath_reltomodel_abs;
        *result_relative = filepath_reltomodel;
        return 1;
    }
    int exists2 = 0;
    if (!vfs_Exists(filepath_reltocwd_abs, &exists2)) {
        existsfail:
        free(filepath_reltocwd);
        free(filepath_reltocwd_abs);
        free(filepath_reltomodel);
        free(filepath_reltomodel_abs);
        free(modelfolder_path);
        return 0;
    }
    if (exists2) {
        free(modelfolder_path);
        free(filepath_reltomodel);
        free(filepath_reltomodel_abs);
        *result_relative = filepath_reltocwd;
        *result_absolute = filepath_reltocwd_abs;
        return 1;
    }
    free(filepath_reltocwd);
    free(filepath_reltocwd_abs);
    free(filepath_reltomodel);
    free(filepath_reltomodel_abs);
    free(modelfolder_path);
    *result_relative = NULL;
    *result_absolute = NULL;
    return 1;
}

h3dtexture *mesh_LocateModelReferencedTexture(
        const char *model_path, const char *referenced_texture_path
        ) {
    if (!referenced_texture_path || !model_path) {
        return texture_GetBlankTexture();
    }

    if (!_seen_textures) {
        _seen_textures = hash_NewStringMap(64);
        if (!_seen_textures)
            return NULL;
    }
    int seen_before = 0;
    {
        uint64_t number = 0;
        hash_StringMapGet(
            _seen_textures, referenced_texture_path, &number
        );
        if (number > 0) {
            seen_before = 1;
        } else {
            hash_StringMapSet(
                _seen_textures, referenced_texture_path, 1
            );
        }
    }

    char *modelfolder_path = filesys_ParentdirOfItem(model_path);
    if (!modelfolder_path)
        return NULL;

    char *texpath_reltomodel_abs = filesys_Join(
        modelfolder_path, referenced_texture_path
    );
    if (!texpath_reltomodel_abs) {
        free(modelfolder_path);
        return NULL;
    }
    char *texpath_reltomodel = filesys_TurnIntoPathRelativeTo(
        texpath_reltomodel_abs, NULL
    );
    free(texpath_reltomodel_abs);
    texpath_reltomodel_abs = NULL;
    if (!texpath_reltomodel) {
        free(modelfolder_path);
        return NULL;
    }
    char *texpath_reltocwd = filesys_TurnIntoPathRelativeTo(
        referenced_texture_path, NULL
    );
    if (!texpath_reltocwd) {
        free(texpath_reltomodel);
        free(modelfolder_path);
        return NULL;
    }

    h3dtexture *tex = NULL;
    int resultExists = 0;
    vfs_Exists(texpath_reltomodel, &resultExists);
    int resultIsDir = 1;
    vfs_IsDirectory(texpath_reltomodel, &resultIsDir);
    if (resultExists && !resultIsDir) {
        #if defined(DEBUG_MESH_LOADER) || defined(DEBUG_MESH_TEXTURE_FINDER)
        if (!seen_before)
            printf("horse3d/meshes.c: debug: "
                   "mesh_LocateModelReferencedTexture "
                   "-> MODEL RELATIVE: %s\n", texpath_reltomodel);
        #endif
        tex = texture_GetTexture(texpath_reltomodel);
    }
    resultExists = 0;
    vfs_Exists(texpath_reltocwd, &resultExists);
    resultIsDir = 1;
    vfs_IsDirectory(texpath_reltocwd, &resultIsDir);
    if (!tex && resultExists && !resultIsDir) {
        #if defined(DEBUG_MESH_LOADER) || defined(DEBUG_MESH_TEXTURE_FINDER)
        if (!seen_before)
            printf("horse3d/meshes.c: debug: "
                   "mesh_LocateModelReferencedTexture "
                   "-> CWD RELATIVE: %s\n", texpath_reltocwd);
        #endif
        tex = texture_GetTexture(texpath_reltocwd);
    }
    if (!tex) {
        #if defined(DEBUG_MESH_LOADER) || defined(DEBUG_MESH_TEXTURE_FINDER)
        if (!seen_before)
            printf("horse3d/meshes.c: debug: "
                   "mesh_LocateModelReferencedTexture "
                   "-> FAILED TO FIND: %s, MODEL RELATIVE: %s, "
                   "CWD RELATIVE: %s\n",
                   referenced_texture_path, texpath_reltomodel,
                   texpath_reltocwd);
        #endif
        tex = texture_GetBlankTexture();
    }
    free(texpath_reltomodel);
    free(texpath_reltocwd);
    free(modelfolder_path);
    return tex;
}


int _mesh_UploadGLPart(
        h3dmesh *mesh, mesh3dtexturepart *part
        ) {
    if (part->_gluploaded || part->vertexcount <= 0 ||
            part->attribute_invisible || part->polycount <= 0)
        return 1;
    _gl->glGenBuffers(1, &part->_glvbo);

    GLfloat *vertices = malloc(
        sizeof(*vertices) * part->polycount * 9
    );
    if (!vertices) {
        _gl->glDeleteBuffers(1, &part->_glvbo);
        return 0;
    }
    int i = 0;
    while (i < part->polycount * 9) {
        vertices[i + 0] = part->vertex_x[part->polyvertex1[i / 9]];
        vertices[i + 1] = part->vertex_y[part->polyvertex1[i / 9]];
        vertices[i + 2] = part->vertex_z[part->polyvertex1[i / 9]];
        vertices[i + 3] = part->vertex_x[part->polyvertex2[i / 9]];
        vertices[i + 4] = part->vertex_y[part->polyvertex2[i / 9]];
        vertices[i + 5] = part->vertex_z[part->polyvertex2[i / 9]];
        vertices[i + 6] = part->vertex_x[part->polyvertex3[i / 9]];
        vertices[i + 7] = part->vertex_y[part->polyvertex3[i / 9]];
        vertices[i + 8] = part->vertex_z[part->polyvertex3[i / 9]];
        i += 9;
    }
    _gl->glBindBuffer(GL_ARRAY_BUFFER, part->_glvbo);
    _gl->glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(*vertices) * part->polycount * 9,
        vertices, GL_STATIC_DRAW
    );
    free(vertices);

    _gl->glGenBuffers(1, &part->_glvbonormals);

    GLfloat *normals = malloc(
        sizeof(*normals) * part->polycount * 9
    );
    if (!normals) {
        _gl->glDeleteBuffers(1, &part->_glvbonormals);
        return 0;
    }
    i = 0;
    while (i < part->polycount * 9) {
        normals[i + 0] = part->vertexnormal_x[part->polyvertex1[i / 9]];
        normals[i + 1] = part->vertexnormal_y[part->polyvertex1[i / 9]];
        normals[i + 2] = part->vertexnormal_z[part->polyvertex1[i / 9]];
        normals[i + 3] = part->vertexnormal_x[part->polyvertex2[i / 9]];
        normals[i + 4] = part->vertexnormal_y[part->polyvertex2[i / 9]];
        normals[i + 5] = part->vertexnormal_z[part->polyvertex2[i / 9]];
        normals[i + 6] = part->vertexnormal_x[part->polyvertex3[i / 9]];
        normals[i + 7] = part->vertexnormal_y[part->polyvertex3[i / 9]];
        normals[i + 8] = part->vertexnormal_z[part->polyvertex3[i / 9]];
        i += 9;
    }
    _gl->glBindBuffer(GL_ARRAY_BUFFER, part->_glvbonormals);
    _gl->glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(*normals) * part->polycount * 9,
        normals, GL_STATIC_DRAW
    );
    free(normals);

    _gl->glGenBuffers(1, &part->_glvbouv);

    GLfloat *uvcoords = malloc(
        sizeof(*uvcoords) * part->polycount * 6
    );
    if (!uvcoords) {
        _gl->glDeleteBuffers(1, &part->_glvbo);
        _gl->glDeleteBuffers(1, &part->_glvbouv);
        return 0;
    }
    i = 0;
    while (i < part->polycount * 6) {
        uvcoords[i + 0] = part->vertexuv_x[part->polyvertex1[i / 6]];
        uvcoords[i + 1] = part->vertexuv_y[part->polyvertex1[i / 6]];
        uvcoords[i + 2] = part->vertexuv_x[part->polyvertex2[i / 6]];
        uvcoords[i + 3] = part->vertexuv_y[part->polyvertex2[i / 6]];
        uvcoords[i + 4] = part->vertexuv_x[part->polyvertex3[i / 6]];
        uvcoords[i + 5] = part->vertexuv_y[part->polyvertex3[i / 6]];
        i += 6;
    }
    _gl->glBindBuffer(GL_ARRAY_BUFFER, part->_glvbouv);
    _gl->glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(*uvcoords) * part->polycount * 6,
        uvcoords, GL_STATIC_DRAW
    );
    free(uvcoords);

    part->_gluploaded = 1;
    return 1;
}


int _mesh_UploadToGL(h3dmesh *mesh, int forcereupload) {
    int actualuploads = 0;
    int i = 0;
    while (i < mesh->default_geometry.texturepart_count) {
        if (forcereupload &&
                mesh->default_geometry.texturepart[i]._gluploaded) {
            _gl->glDeleteBuffers(
                1, &mesh->default_geometry.texturepart[i]._glvbo
            );
            _gl->glDeleteBuffers(
                1, &mesh->default_geometry.texturepart[i]._glvbouv
            );
            mesh->default_geometry.texturepart[i]._gluploaded = 0;
        } else if (!mesh->default_geometry.texturepart[i]._gluploaded) {
            if (!_mesh_UploadGLPart(
                    mesh, &mesh->default_geometry.texturepart[i]
                    ))
                return 0;
            if (mesh->default_geometry.texturepart[i]._gluploaded)
                actualuploads++;
        }
        if (mesh->default_geometry.texturepart[i].tex &&
                !mesh->default_geometry.
                    texturepart[i].attribute_invisible) {
            if (!_texture_LoadTextureGL(
                    mesh->default_geometry.texturepart[i].tex,
                    mesh->default_geometry.texturepart[i].attribute_withalpha
                    ))
                return 0;
        }
        i++;
    }
    #ifdef DEBUG_MESH_LOADER
    if (actualuploads > 0) {
        printf("horse3d/meshes.c: debug: "
               "uploaded mesh with %d parts\n",
               actualuploads);
    }
    #endif
    return 1;
}


int meshgeopart_GetVertex(
        mesh3dtexturepart *part,
        double vertex_x, double vertex_y, double vertex_z,
        double vertexuv_x, double vertexuv_y,
        double vertexnormal_x, double vertexnormal_y,
        double vertexnormal_z
        ) {
    int i = 0;
    while (i < part->vertexcount) {
        if (fabs(part->vertex_x[i] - vertex_x) < 0.001 &&
                fabs(part->vertex_y[i] - vertex_y) < 0.001 &&
                fabs(part->vertex_z[i] - vertex_z) < 0.001 &&
                fabs(part->vertexuv_x[i] - vertexuv_x) < 0.001 &&
                fabs(part->vertexuv_y[i] - vertexuv_y) < 0.001 &&
                fabs(part->vertexnormal_x[i] -
                     vertexnormal_x) < 0.001 &&
                fabs(part->vertexnormal_y[i] -
                     vertexnormal_y) < 0.001 &&
                fabs(part->vertexnormal_z[i] -
                     vertexnormal_z) < 0.001) {
            return i;
        }
        i++;
    }
    if (meshgeopart_AddVertex(
            part, vertex_x, vertex_y, vertex_z,
            vertexuv_x, vertexuv_y, vertexnormal_x,
            vertexnormal_y, vertexnormal_z)) {
        return part->vertexcount - 1;
    }
    return -1;
}

int meshgeo_GetPartIndexForTex(
        h3dmesh_geometry *meshgeo,
        h3dtexture *tex,
        h3dmaterialprops *props
        ) {
    int i = 0;
    while (i < meshgeo->texturepart_count) {
        if (meshgeo->texturepart[i].tex == tex &&
                (meshgeo->texturepart[i].matprops.unlit != 0) ==
                    (props->unlit != 0))
            return i;
        i++;
    }
    return -1;
}

mesh3dtexturepart *meshgeo_GetPartForTex(
        h3dmesh_geometry *meshgeo,
        h3dtexture *tex,
        h3dmaterialprops *props
        ) {
    int id = meshgeo_GetPartIndexForTex(
        meshgeo, tex, props
    );
    if (id >= 0)
        return &meshgeo->texturepart[id];
    return NULL;
}

int meshgeo_AddPolygon(
        h3dmesh_geometry *meshgeo,
        h3dtexture *tex,
        h3dmaterialprops *matprops,
        double v1x, double v1y, double v1z,
        double vt1x, double vt1y,
        double vn1x, double vn1y, double vn1z,
        double v2x, double v2y, double v2z,
        double vt2x, double vt2y,
        double vn2x, double vn2y, double vn2z,
        double v3x, double v3y, double v3z,
        double vt3x, double vt3y,
        double vn3x, double vn3y, double vn3z,
        h3dmeshloadinfo *loadinfo,
        int *_error_invalidgeometry
        ) {
    assert(meshgeo != NULL);
    if (_error_invalidgeometry)
        *_error_invalidgeometry = 0;

    int texturepart_id = meshgeo_GetPartIndexForTex(
        meshgeo, tex, matprops
    );
    if (texturepart_id < 0) {
        texturepart_id = meshgeo->texturepart_count;
        mesh3dtexturepart *newparts = realloc(
            meshgeo->texturepart,
            sizeof(*newparts) * (meshgeo->texturepart_count + 1)
        );
        if (!newparts)
            return 0;
        meshgeo->texturepart = newparts;
        memset(&meshgeo->texturepart[meshgeo->texturepart_count],
               0, sizeof(*newparts));
        meshgeo->texturepart_count++;
        if (tex)
            meshgeo->texturepart[texturepart_id].attribute_withalpha = (
                texture_HasNotableTransparency(tex)
            );
        else
            meshgeo->texturepart[texturepart_id].attribute_withalpha = 0;
        meshgeo->texturepart[texturepart_id].tex = tex;
        memcpy(
            &meshgeo->texturepart[texturepart_id].matprops,
            matprops,
            sizeof(*matprops)
        );
        meshgeo->texturepart[texturepart_id].matprops.unlit = (
            meshgeo->texturepart[texturepart_id].matprops.unlit != 0
        );
        char *texfilename = NULL;
        if (tex && tex->filepath) {
            texfilename = filesys_Basename(tex->filepath);
            if (!texfilename) {
                return 0;
            }
        }
        int c = 0;
        while (c < loadinfo->invisible_keywords_count) {
            if (!loadinfo->invisible_keywords[c] ||
                    !texfilename) {
                c++;
                continue;
            }
            if (strstr(texfilename,
                       loadinfo->invisible_keywords[c])) {
                meshgeo->texturepart[texturepart_id].
                    attribute_invisible = 1;
            }
            c++;
        }
        c = 0;
        while (c < loadinfo->nocollision_keywords_count) {
            if (!loadinfo->nocollision_keywords[c] ||
                    !texfilename) {
                c++;
                continue;
            }
            if (strstr(texfilename,
                       loadinfo->nocollision_keywords[c])) {
                meshgeo->texturepart[texturepart_id].
                    attribute_nocollision = 1;
            }
            c++;
        }
        if (texfilename)
            free(texfilename);
    }
    mesh3dtexturepart *part = &meshgeo->texturepart[texturepart_id];

    int index1 = meshgeopart_GetVertex(
        part, v1x, v1y, v1z, vt1x, vt1y, vn1x, vn1y, vn1z
    );
    if (index1 < 0)
        return 0;
    int index2 = meshgeopart_GetVertex(
        part, v2x, v2y, v2z, vt2x, vt2y, vn2x, vn2y, vn2z
    );
    if (index2 < 0)
        return 0;
    int index3 = meshgeopart_GetVertex(
        part, v3x, v3y, v3z, vt3x, vt3y, vn3x, vn3y, vn3z
    );
    if (index3 < 0)
        return 0;

    if (index1 == index2 || index1 == index3 ||
            index2 == index3) {
        if (_error_invalidgeometry)
            *_error_invalidgeometry = 1;
        return 0;
    }

    int *polyvertex1_new = realloc(
        part->polyvertex1,
        sizeof(*polyvertex1_new) * (part->polycount + 1)
    );
    if (!polyvertex1_new)
        return 0;
    part->polyvertex1 = polyvertex1_new;
    int *polyvertex2_new = realloc(
        part->polyvertex2,
        sizeof(*polyvertex2_new) * (part->polycount + 1)
    );
    if (!polyvertex2_new)
        return 0;
    part->polyvertex2 = polyvertex2_new;
    int *polyvertex3_new = realloc(
        part->polyvertex3,
        sizeof(*polyvertex3_new) * (part->polycount + 1)
    );
    if (!polyvertex3_new)
        return 0;
    part->polyvertex3 = polyvertex3_new;

    int hadzeropolygons = 1;
    int k = 0;
    while (k < meshgeo->texturepart_count) {
        if (meshgeo->texturepart[k].polycount > 0) {
            hadzeropolygons = 0;
            break;
        }
        k++;
    }

    part->polyvertex1[part->polycount] = index1;
    part->polyvertex2[part->polycount] = index2;
    part->polyvertex3[part->polycount] = index3;
    part->polycount++;

    if (hadzeropolygons) {
        meshgeo->max_x = v1x;
        meshgeo->min_x = v1x;
        meshgeo->max_y = v1y;
        meshgeo->min_y = v1y;
        meshgeo->max_z = v1z;
        meshgeo->min_z = v1z;
    }
    _internal_meshgeo_UpdateOverallBoundaries(
        meshgeo, v1x, v1y, v1z
    );
    _internal_meshgeo_UpdateOverallBoundaries(
        meshgeo, v2x, v2y, v2z
    );
    _internal_meshgeo_UpdateOverallBoundaries(
        meshgeo, v3x, v3y, v3z
    );

    return 1;
}


int meshgeopart_AddVertex(
        mesh3dtexturepart *part,
        double vertex_x, double vertex_y, double vertex_z,
        double vertexuv_x, double vertexuv_y,
        double vertexnormal_x, double vertexnormal_y,
        double vertexnormal_z
        ) {
    int new_vcount = part->vertexcount + 1;
    double *vertex_x_new = realloc(
        part->vertex_x, sizeof(*vertex_x_new) * new_vcount
    );
    if (!vertex_x_new) {
        return 0;
    }
    part->vertex_x = vertex_x_new;
    double *vertex_y_new = realloc(
        part->vertex_y, sizeof(*vertex_y_new) * new_vcount
    );
    if (!vertex_y_new) {
        return 0;
    }
    part->vertex_y = vertex_y_new;
    double *vertex_z_new = realloc(
        part->vertex_z, sizeof(*vertex_z_new) * new_vcount
    );
    if (!vertex_z_new) {
        return 0;
    }
    part->vertex_z = vertex_z_new;

    double *vertexuv_x_new = realloc(
        part->vertexuv_x, sizeof(*vertexuv_x_new) * new_vcount
    );
    if (!vertexuv_x_new) {
        return 0;
    }
    part->vertexuv_x = vertexuv_x_new;
    double *vertexuv_y_new = realloc(
        part->vertexuv_y, sizeof(*vertexuv_y_new) * new_vcount
    );
    if (!vertexuv_y_new) {
        return 0;
    }
    part->vertexuv_y = vertexuv_y_new;

    double *vertexnormal_x_new = realloc(
        part->vertexnormal_x,
        sizeof(*vertexnormal_x_new) * new_vcount
    );
    if (!vertexnormal_x_new) {
        return 0;
    }
    part->vertexnormal_x = vertexnormal_x_new;
    double *vertexnormal_y_new = realloc(
        part->vertexnormal_y,
        sizeof(*vertexnormal_y_new) * new_vcount
    );
    if (!vertexnormal_y_new) {
        return 0;
    }
    part->vertexnormal_y = vertexnormal_y_new;
    double *vertexnormal_z_new = realloc(
        part->vertexnormal_z,
        sizeof(*vertexnormal_z_new) * new_vcount
    );
    if (!vertexnormal_z_new) {
        return 0;
    }
    part->vertexnormal_z = vertexnormal_z_new;

    part->vertex_x[part->vertexcount] = vertex_x;
    part->vertex_y[part->vertexcount] = vertex_y;
    part->vertex_z[part->vertexcount] = vertex_z;
    part->vertexuv_x[part->vertexcount] = vertexuv_x;
    part->vertexuv_y[part->vertexcount] = vertexuv_y;
    part->vertexnormal_x[part->vertexcount] = vertexnormal_x;
    part->vertexnormal_y[part->vertexcount] = vertexnormal_y;
    part->vertexnormal_z[part->vertexcount] = vertexnormal_z;

    part->vertexcount = new_vcount;
    return 1;
}


void _internal_meshgeo_UpdateOverallBoundaries(
        h3dmesh_geometry *meshgeo, double x, double y, double z
        ) {
    if (x > meshgeo->max_x)
        meshgeo->max_x = x;
    else if (x < meshgeo->min_x)
        meshgeo->min_x = x;
    if (y > meshgeo->max_y)
        meshgeo->max_y = y;
    else if (y < meshgeo->min_y)
        meshgeo->min_y = y;
    if (z > meshgeo->max_z)
        meshgeo->max_z = z;
    else if (z < meshgeo->min_z)
        meshgeo->min_z = z;
}


h3dmesh *mesh_GetFromFile(
        const char *path, h3dmeshloadinfo *loadinfo,
        char **error
        ) {
    if (error)
        *error = NULL;
    h3dmesh *result = NULL;
    int set_error_on_obj_fail = 1;
    if ((strlen(path) > strlen(".gltf") &&
            strcasecmp(path + strlen(path) - strlen(".gltf"),
                       ".gltf") == 0) ||
            (strlen(path) > strlen(".gltb") &&
             strcasecmp(path + strlen(path) - strlen(".gltb"),
                        ".gltb") == 0)) {
        set_error_on_obj_fail = 0;
        result = mesh_LoadFromGLTFEx(path, 1, loadinfo, error);
    }
    if (!result)
        result = mesh_LoadFromOBJEx(
            path, 1, loadinfo, (set_error_on_obj_fail ? error : NULL)
        );
    if (!result) {
        if (error && !*error)
            *error = strdup("unknown error");
        return NULL;
    }
    mesh_PrintLoadSummary(result);
    return result;
}

void mesh_PrintLoadSummary(h3dmesh *result) {
    #ifdef DEBUG_MESH_LOADER
    printf("horse3d/mesh.c: debug: loaded mesh with %d parts:\n",
           result->default_geometry.texturepart_count);
    int i = 0;
    while (i < result->default_geometry.texturepart_count) {
        printf(
            "horse3d/mesh.c: debug: "
            "part %d tex name: %s invis: %d alpha: %d polygons: %d\n",
            i,
            (result->default_geometry.texturepart[i].tex ?
             result->default_geometry.texturepart[i].tex->filepath :
             "(none)"),
            result->default_geometry.texturepart[i].attribute_invisible,
            result->default_geometry.texturepart[i].attribute_withalpha,
            result->default_geometry.texturepart[i].polycount
        );
        i++;
    }
    printf("horse3d/mesh.c: debug: mesh has transparent parts: %d\n",
           result->default_geometry.has_transparent_parts);
    #endif
}

void mesh_DestroyMesh(h3dmesh *mesh) {

}

void meshloadinfo_Free(h3dmeshloadinfo *loadinfo) {
    if (loadinfo) {
        if (loadinfo->nocollision_keywords) {
            int i = 0;
            while (i < loadinfo->nocollision_keywords_count) {
                free(loadinfo->nocollision_keywords[i]);
                i++;
            }
            free(loadinfo->nocollision_keywords);
        }
        if (loadinfo->invisible_keywords) {
            int i = 0;
            while (i < loadinfo->invisible_keywords_count) {
                free(loadinfo->invisible_keywords[i]);
                i++;
            }
            free(loadinfo->invisible_keywords);
        }
        free(loadinfo);
    }
}

void meshgeopart_AssignMaterial(mesh3dtexturepart *part) {
    if (!part || part->assignedmaterial)
        return;
    if (part->matprops.unlit) {
        part->assignedmaterial = material_GetDefaultUnlit();
    } else {
        part->assignedmaterial = material_GetDefault();
    }
}
