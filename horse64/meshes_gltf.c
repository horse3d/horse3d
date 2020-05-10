
#include <assert.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf/cgltf.h>
#include <inttypes.h>
#include <mathc/mathc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesys.h"
#include "glheaders.h"
#include "hash.h"
#include "math3d.h"
#include "meshes.h"
#include "meshes_gltf.h"
#include "texture.h"
#include "uri.h"
#include "vfs.h"


//#define DEBUG_MESH_LOADER
//#define DEBUG_MESH_TEXTURE_FINDER


extern SDL_gles2funcs *_gl;
int _texture_LoadTextureGL(h3dtexture *tex, int alpha);
extern hashmap *_seen_textures;
extern hashmap *mesh_cache;
void _clean_seen_textures();


int _loadcgltfbuffers(
        const char *mdlpath,
        const cgltf_options* options, cgltf_data* data, char **error
        ) {
	if (data->buffers_count > 0 &&
            data->buffers[0].data == NULL &&
            data->buffers[0].uri == NULL && data->bin) {
		data->buffers[0].data = (void*)data->bin;
	}
    char errbuf[512];

    size_t k = 0;
    while (k < data->buffers_count) {
        if (data->buffers[k].data) {
            k++;
            continue;
        }
        if (!data->buffers[k].uri) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "buffer %d lacks both data and uri", (int)k
                );
                *error = strdup(errbuf);
            }
            return 0;
        }

        const char *uristr = data->buffers[k].uri;
        if (strlen(uristr) > strlen("data:") &&
                memcmp(uristr, "data:", strlen("data:")) == 0) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "buffer %d uses unsupported data URI", (int)k
                );
                *error = strdup(errbuf);
            }
            return 0;
        }
        uriinfo *uri = NULL;
        if (!strstr(uristr, "://")) {
            char *uristr_with_protocol = malloc(
                strlen(uristr) + strlen("file://") + 1
            );
            if (!uristr_with_protocol) {
                if (error)
                    *error = strdup("alloc fail");
                return 0;
            }
            memcpy(uristr_with_protocol, "file://", strlen("file://"));
            memcpy(
                uristr_with_protocol + strlen("file://"),
                uristr, strlen(uristr) + 1
            );
            uri = uri_Parse(uristr_with_protocol);
            free(uristr_with_protocol);
        } else {
            uri = uri_Parse(uristr);
        }
        if (!uri || !uri->path || !uri->protocol ||
                strcasecmp(uri->protocol, "file") != 0) {
            uri_Free(uri);
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "buffer %d uri parsing failed: %s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            return 0;
        }

        char *relpath, *abspath;
        char *bufferdata = NULL;

        if (!mesh_LocateModelReferencedFilePath(
                mdlpath, uri->path, &relpath, &abspath)) {
            uri_Free(uri);
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "internal error resolving buffer %d uri: "
                    "%s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            return 0;
        }
        assert((relpath && abspath) || (!relpath && !abspath));
        if (!relpath || !abspath) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "cannot find file pointed at by buffer %d uri: "
                    "%s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            abortandfreestuff:
            if (relpath)
                free(relpath);
            if (abspath)
                free(abspath);
            if (uri)
                uri_Free(uri);
            if (bufferdata)
                free(bufferdata);
            return 0;
        }

        uint64_t buffersize = 0;
        if (!vfs_Size(abspath, &buffersize, 0)) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "vfs_Size() unexpectedly failed for buffer %d uri: "
                    "%s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            goto abortandfreestuff;
        }
        if (buffersize == 0) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "buffer %d with the following uri is empty file: "
                    "%s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            goto abortandfreestuff;
        }
        bufferdata = malloc(buffersize);
        if (!bufferdata) {
            if (error)
                *error = strdup("alloc fail, out of memory?");
            goto abortandfreestuff;
        }
        if (!vfs_GetBytes(abspath, 0, buffersize, bufferdata, 0)) {
            if (error) {
                snprintf(
                    errbuf, sizeof(errbuf) - 1,
                    "I/O fail in vfs_GetBytes() reading "
                    "buffer %d with uri: "
                    "%s", (int)k, uristr
                );
                *error = strdup(errbuf);
            }
            goto abortandfreestuff;
        }
        data->buffers[k].size = buffersize;
        data->buffers[k].data = bufferdata;

        free(abspath);
        free(relpath);
        uri_Free(uri);

        k++;
    }
    return 1;
}

int _findcgltfnodechildindex(
        cgltf_node *parent, cgltf_node *child
        ) {
    size_t i = 0;
    while (i < parent->children_count) {
        if (parent->children[i] == child)
            return i;
        i++;
    }
    return -1;
}

int _mesh_IterateCGLTFNodes(
        cgltf_data *data,
        int (*cb)(cgltf_node *node, void *userdata),
        void *userdata
        ) {
    if (!data)
        return 0;

    hashset *nodes_seen = hashset_New(64);

    cgltf_node *lookat_node = NULL;
    size_t k = 0;
    while (k < data->scenes_count) {
        size_t j = 0;
        while (j < data->scenes[k].nodes_count) {
            lookat_node = data->scenes[k].nodes[j];
            if (lookat_node->parent) {
                j++;
                continue;
            }
            if (hashset_Contains(
                    nodes_seen, &lookat_node, sizeof(lookat_node)
                    )) {
                j++;
                continue;
            }
            if (!hashset_Add(
                    nodes_seen, &lookat_node, sizeof(lookat_node)
                    ))
                return 0;
            int last_child_visit_index = -1;
            while (lookat_node) {
                if (!cb(lookat_node, userdata)) {
                    hashset_Free(nodes_seen);
                    return 1;
                }
                if (last_child_visit_index + 1 <
                        (int)lookat_node->children_count) {
                    // Go into next child:
                    int i = last_child_visit_index + 1;
                    last_child_visit_index = -1;
                    lookat_node = lookat_node->children[i];
                    continue;
                } else {
                    // Go up until we have a neighboring child to
                    // visit:
                    int changed_node = 0;
                    while (1) {
                        if (!lookat_node->parent)
                            break;
                        int i = _findcgltfnodechildindex(
                            lookat_node->parent, lookat_node
                        );
                        if (i < 1)
                            break;
                        changed_node = 1;
                        lookat_node = lookat_node->parent;
                        last_child_visit_index = i;
                        if (last_child_visit_index + 1 >=
                                (int)lookat_node->children_count)
                            continue;
                        i = last_child_visit_index + 1;
                        last_child_visit_index = -1;
                        lookat_node = lookat_node->children[i];
                        break;
                    }
                    if (!changed_node)
                        break;
                }
            }
            j++;
        }
        k++;
    }
    hashset_Free(nodes_seen);
    return 1;
}

h3dskeleton *mesh_GetCGLTFNodeTransformSkeleton(
        cgltf_node *node, h3dskeleton_bone **node_bone,
        char **error
        ) {
    h3dskeleton *sk = skeleton_New();
    if (!sk) {
        if (error) *error = strdup("allocation failure");
        return NULL;
    }

    h3dskeleton_bone *nodebone = NULL;
    h3dskeleton_bone *bonechild = NULL;
    while (node) {
        h3dskeleton_bone *bone = bone_New(
            sk, NULL
        );
        if (!bone) {
            skeleton_Destroy(sk);
            if (error) *error = strdup("bone creation failure");
            return NULL;
        }
        if (!nodebone)
            nodebone = bone;
        if (bonechild) {
            if (!bone_Reparent(bonechild, bone)) {
                skeleton_Destroy(sk);
                if (error) *error = strdup("reparent failure");
                return NULL;
            }
        }
        if (node->has_translation) {
            bone->local_position[0] = node->translation[0];
            bone->local_position[1] = -node->translation[2];
            bone->local_position[2] = node->translation[1];
        }
        if (node->has_rotation) {
            bone->local_rotation[0] = node->rotation[0]; // x
            bone->local_rotation[1] = -node->rotation[2]; // -z
            bone->local_rotation[2] = node->rotation[1]; // y
            bone->local_rotation[3] = node->rotation[3]; // w
        }
        if (node->has_scale) {
            bone->local_scale[0] = node->scale[0];
            bone->local_scale[1] = node->scale[1];
            bone->local_scale[2] = node->scale[2];
        }
        if (node->has_matrix &&
                !node->has_translation && !node->has_rotation) {
            if (error)
                *error = strdup(
                    "GLTF node relies on unsupported 'matrix' transform"
                );
            return NULL;
        }
        bonechild = bone;
        node = node->parent;
    }

    if (node_bone) *node_bone = nodebone;
    if (error) *error = NULL;
    return sk;
}

typedef struct findcgltfmeshnodeinfo {
    cgltf_node *result;
    int n;
} findcgltfmeshnodeinfo;

static int _findcgltfmeshnodecb(
        cgltf_node *node, void *userdata
        ) {
    findcgltfmeshnodeinfo *info = userdata;
    if (node->mesh) {
        info->n--;
        if (info->n < 0) {
            info->result = node;
            return 0;
        }
    }
    return 1;
}

cgltf_node *_findcgltfmeshnode(
        cgltf_data *data, int n
        ) {
    assert(n >= 0);
    findcgltfmeshnodeinfo info;
    memset(&info, 0, sizeof(info));
    info.n = n;
    _mesh_IterateCGLTFNodes(
        data, &_findcgltfmeshnodecb, &info
    );
    return info.result;
}

size_t _cgltfmeshnodecount(cgltf_data *data) {
    size_t c = 0;
    while (_findcgltfmeshnode(
            data, c
            ))
        c++;
    return c;
}

int _cgltf_loaddata(
        cgltf_options *gltf_options,
        const char *path, cgltf_data **out_data,
        char **error
        ) {
    cgltf_data *gltf_data = NULL;
    *out_data = NULL;
    int _existsresult = 0;
    if (!vfs_Exists(path, &_existsresult, 0)) {
        if (error)
            *error = strdup("vfs_Exists() failed, out of memory?");
        return 0;
    }
    if (!_existsresult) {
        if (error)
            *error = strdup("file not found");
        return 0;
    }
    uint64_t filedatasize = 0;
    if (!vfs_Size(path, &filedatasize, 0)) {
        if (error)
            *error = strdup("vfs_Size() failed, out of memory?");
        return 0;
    }
    if (filedatasize <= 0) {
        if (error)
            *error = strdup("file unexpectedly empty");
        return 0;
    }
    char *filedata = malloc(filedatasize);
    if (!filedata) {
        if (error)
            *error = strdup("file alloc failed, out of memory?");
        return 0;
    }
    if (!vfs_GetBytes(path, 0, filedatasize, filedata, 0)) {
        if (error)
            *error = strdup(
                "failed to read file, disk error or out of memory?"
            );
        free(filedata);
        return 0;
    }
    cgltf_result result = cgltf_parse(
        gltf_options, filedata, filedatasize, &gltf_data
    );
    if (result != cgltf_result_success) {
        if (error)
            *error = strdup(
                "parsing failed, file likely damaged or invalid"
            );
        free(filedata);
        return 0;
    }

    if (!_loadcgltfbuffers(
            path, gltf_options, gltf_data, error
            )) {
        if (error)
            *error = strdup(
                "failed to load buffers / external files, are "
                "they present?"
            );
        free(filedata);
        return 0;
    }
    if (cgltf_validate(gltf_data) != cgltf_result_success) {
        if (error)
            *error = strdup("invalid or damaged GLTF file, "
                "failed internal validation");
        free(gltf_data);
        free(filedata);
        return 0;
    }
    *out_data = gltf_data;
    free(filedata);
    return 1;
}

h3dmesh *mesh_LoadFromGLTFEx(
        const char *path, int usecache,
        h3dmeshloadinfo *loadinfo,
        char **error
        ) {
    if (error)
        *error = NULL;
    if (usecache) {
        int freeloadinfo = 0;
        if (!loadinfo) {
            freeloadinfo = 1;
            loadinfo = malloc(sizeof(*loadinfo));
            if (!loadinfo)
                return NULL;
            memset(loadinfo, 0, sizeof(*loadinfo));
        }
        char *p = filesys_Normalize(path);
        if (!p) {
            if (loadinfo && freeloadinfo)
                free(loadinfo);
            return NULL;
        }

        if (!mesh_cache) {
            mesh_cache = hash_NewBytesMap(128);
            if (!mesh_cache) {
                free(p);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
        }
        uintptr_t hashptrval = 0;
        if (!hash_BytesMapGet(
                mesh_cache, p, strlen(p),
                (uint64_t*)&hashptrval)) {
            h3dmesh *m = mesh_LoadFromGLTFEx(
                p, 0, loadinfo, error
            );
            if (!m) {
                free(p);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
            if (!hash_BytesMapSet(
                    mesh_cache, p, strlen(p),
                    (uint64_t)(uintptr_t)m)) {
                free(p);
                mesh_DestroyMesh(m);
                if (loadinfo && freeloadinfo)
                    free(loadinfo);
                return NULL;
            }
            free(p);
            if (loadinfo && freeloadinfo)
                free(loadinfo);
            return m;
        }
        free(p);
        if (loadinfo && freeloadinfo)
            free(loadinfo);
        return (h3dmesh*)(void*)hashptrval;
    }
    assert(loadinfo != NULL);
    _clean_seen_textures();

    #if defined(DEBUG_MESH_LOADER) && !defined(NDEBUG)
    printf("horse3d/meshes.c: debug: GLTF loading: \"%s\"\n", path);
    #endif

    cgltf_options gltf_options = {};
	cgltf_data *gltf_data = NULL;

    if (!_cgltf_loaddata(&gltf_options, path, &gltf_data, error))
        return NULL;

    h3dmesh *m = mesh_LoadFromGLTFData(
        gltf_data, path, 0, loadinfo, error
    );
    if (!m) {
        if (gltf_data) {
            cgltf_free(gltf_data);
        }
    }

    return m;
}

static void yuptozup(double pos[3]) {
    double result[3];
    result[0] = pos[0];
    result[1] = -pos[2];
    result[2] = pos[1];
    memcpy(pos, result, sizeof(result));
}

int mesh_AddFromCGLTFMeshNode(
        h3dmesh_geometry *builtmeshgeo, cgltf_node *mesh_node,
        h3dmeshloadinfo *loadinfo,
        const char *originalgltfpath, char **error
        ) {
    int64_t polycount = 0;

    h3dskeleton_bone *node_bone = NULL;
    if (error)
        *error = NULL;
    h3dskeleton *skeleton = mesh_GetCGLTFNodeTransformSkeleton(
        mesh_node, &node_bone, error
    );
    if (!skeleton) {
        if (error && !*error)
            *error = strdup("unknown internal error creating skeleton");
        return 0;
    }
    assert(node_bone != NULL);

    int i = 0;
    while (i < (int)mesh_node->mesh->primitives_count) {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes_gltf.c: debug: GLTF primitive %d/%d\n",
            i + 1, (int)mesh_node->mesh->primitives_count
        );
        #endif
        if (!mesh_node->mesh->primitives[i].indices ||
                mesh_node->mesh->primitives[i].type !=
                cgltf_primitive_type_triangles
                ) {
            if (mesh_node->mesh->primitives[i].type !=
                    cgltf_primitive_type_triangles)
                printf(
                    "horse3d/meshes_gltf.c: warning: skipped "
                    "non-triangle geometry\n"
                );
            i++;
            continue;
        }

        // Extract base texture of this primitive:
        cgltf_material *mat = (
            mesh_node->mesh->primitives[i].material
        );
        const char *texpath = NULL;
        if (!texpath && mat->has_pbr_metallic_roughness &&
                mat->pbr_metallic_roughness.base_color_texture.texture &&
                mat->pbr_metallic_roughness.base_color_texture.texture->image)
            texpath = mat->pbr_metallic_roughness.
                base_color_texture.texture->image->uri;
        if (!texpath && mat->has_pbr_specular_glossiness &&
                mat->pbr_specular_glossiness.
                diffuse_texture.texture &&
                mat->pbr_specular_glossiness.
                diffuse_texture.texture->image)
            texpath = mat->pbr_specular_glossiness.
                diffuse_texture.texture->image->uri;
        h3dtexture *tex = NULL;
        if (texpath)
            tex = mesh_LocateModelReferencedTexture(
                originalgltfpath, texpath
            );
        else
            tex = texture_GetBlankTexture();
        if (!tex) {
            if (error)
                *error = strdup("failed to allocate texture");
            skeleton_Destroy(skeleton);
            return 0;
        }

        // Extract polygon vertex positions & normal:
        int j = 0;
        while (j < (int)mesh_node->mesh->primitives[i].indices->count) {
            unsigned int indexes[3];
            assert(!mesh_node->mesh->primitives[i].indices->is_sparse);
            assert(mesh_node->mesh->primitives[i].indices->buffer_view->buffer->data);
            if (!cgltf_accessor_read_uint(
                    mesh_node->mesh->primitives[i].indices,
                    j, &indexes[0], sizeof(indexes[0]))) {
                if (error)
                    *error = strdup("failed to read index accessor of mesh");
                skeleton_Destroy(skeleton);
                return 0;
            }
            if (!cgltf_accessor_read_uint(
                    mesh_node->mesh->primitives[i].indices,
                    j + 1, &indexes[1], sizeof(indexes[1]))) {
                if (error)
                    *error = strdup("failed to read index accessor of mesh");
                skeleton_Destroy(skeleton);
                return 0;
            }
            if (!cgltf_accessor_read_uint(
                    mesh_node->mesh->primitives[i].indices,
                    j + 2, &indexes[2], sizeof(indexes[2]))) {
                if (error)
                    *error = strdup("failed to read index accessor of mesh");
                skeleton_Destroy(skeleton);
                return 0;
            }

            int positionset = 0;
            int normalset = 0;
            int uvset = 0;
            double positions[9];
            double normals[9];
            double uv[6];

            int k = 0;
            while (k < (int)mesh_node->mesh->primitives[i].attributes_count) {
                cgltf_attribute meshattr = (
                    mesh_node->mesh->primitives[i].attributes[k]
                );
                if (meshattr.type == cgltf_attribute_type_position) {
                    cgltf_float v[3] = {0};
                    if (!cgltf_accessor_read_float(
                            meshattr.data, indexes[0], v, sizeof(v))) {
                        if (error)
                            *error = strdup(
                                "failed to read position accessor of mesh"
                            );
                        skeleton_Destroy(skeleton);
                        return 0;
                    }
                    positions[0] = v[0];
                    positions[1] = v[1];
                    positions[2] = v[2];
                    if (!cgltf_accessor_read_float(
                            meshattr.data, indexes[1], v, sizeof(v))) {
                        if (error)
                            *error = strdup(
                                "failed to read position accessor of mesh"
                            );
                        skeleton_Destroy(skeleton);
                        return 0;
                    }
                    positions[3] = v[0];
                    positions[4] = v[1];
                    positions[5] = v[2];
                    if (!cgltf_accessor_read_float(
                            meshattr.data, indexes[2], v, sizeof(v))) {
                        if (error)
                            *error = strdup(
                                "failed to read position accessor of mesh"
                            );
                        skeleton_Destroy(skeleton);
                        return 0;
                    }
                    positions[6] = v[0];
                    positions[7] = v[1];
                    positions[8] = v[2];
                    positionset = 1;
                } else if (meshattr.type == cgltf_attribute_type_normal) {
                    cgltf_float v1[3];
                    cgltf_float v2[3];
                    cgltf_float v3[3];
                    if (cgltf_accessor_read_float(
                            meshattr.data, indexes[0], v1, sizeof(v1)) &&
                            cgltf_accessor_read_float(
                            meshattr.data, indexes[1], v2, sizeof(v2)) &&
                            cgltf_accessor_read_float(
                            meshattr.data, indexes[2], v3, sizeof(v3))) {
                        normals[0] = v1[0];
                        normals[1] = v1[1];
                        normals[2] = v1[2];
                        normals[3] = v2[0];
                        normals[4] = v2[1];
                        normals[5] = v2[2];
                        normals[6] = v3[0];
                        normals[7] = v3[1];
                        normals[8] = v3[2];
                        normalset = 1;
                    } else {
                        memset(normals, 0, sizeof(normals));
                    }
                } else if (meshattr.type == cgltf_attribute_type_texcoord) {
                    cgltf_float v1[2];
                    cgltf_float v2[2];
                    cgltf_float v3[2];
                    if (cgltf_accessor_read_float(
                            meshattr.data, indexes[0], v1, sizeof(v1)) &&
                            cgltf_accessor_read_float(
                            meshattr.data, indexes[1], v2, sizeof(v2)) &&
                            cgltf_accessor_read_float(
                            meshattr.data, indexes[2], v3, sizeof(v3))) {
                        uv[0] = v1[0];
                        uv[1] = v1[1];
                        uv[2] = v2[0];
                        uv[3] = v2[1];
                        uv[4] = v3[0];
                        uv[5] = v3[1];
                        uvset = 1;
                    } else {
                        memset(normals, 0, sizeof(normals));
                    }
                }
                k++;
            }
            if (!positionset) {
                if (error)
                    *error = strdup(
                        "failed to read position accessor of mesh"
                    );
                skeleton_Destroy(skeleton);
                return 0;
            }
            if (!normalset) {
                polygon3d_normal(
                    &positions[0], &positions[3], &positions[6],
                    &normals[0]
                );
                memcpy(&normals[3], &normals[0], sizeof(*normals) * 3);
                memcpy(&normals[6], &normals[0], sizeof(*normals) * 3);
            } else {
                yuptozup(&normals[0]);
                yuptozup(&normals[3]);
                yuptozup(&normals[6]);
            }
            if (!uvset) {
                uv[0] = 1.0;
                uv[1] = 0.0;
                uv[2] = 1.0;
                uv[3] = 1.0;
                uv[4] = 0.0;
                uv[5] = 0.0;
            }

            yuptozup(&positions[0]);
            yuptozup(&positions[3]);
            yuptozup(&positions[6]);
            bone_TransformPosition(
                node_bone, &positions[0], &positions[0]
            );
            bone_TransformPosition(
                node_bone, &positions[3], &positions[3]
            );
            bone_TransformPosition(
                node_bone, &positions[6], &positions[6]
            );
            bone_TransformDirectionVec(
                node_bone, &normals[0], &normals[0]
            );
            bone_TransformDirectionVec(
                node_bone, &normals[3], &normals[3]
            );
            bone_TransformDirectionVec(
                node_bone, &normals[6], &normals[6]
            );

            // Add polygon to model:
            h3dmaterialprops props;
            memset(&props, 0, sizeof(props));
            props.unlit = (mat->unlit != 0);
            int _error_invalidgeometry = 0;
            if (!meshgeo_AddPolygon(
                    builtmeshgeo, tex, &props,
                    positions[0], positions[1], positions[2],
                    uv[0], uv[1],
                    normals[0], normals[1], normals[2],
                    positions[3], positions[4], positions[5],
                    uv[2], uv[3],
                    normals[3], normals[4], normals[5],
                    positions[6], positions[7], positions[8],
                    uv[4], uv[5],
                    normals[6], normals[7], normals[8],
                    loadinfo,
                    &_error_invalidgeometry)) {
                if (!_error_invalidgeometry) {
                    if (error)
                        *error = strdup(
                            "failed to add polygon, out of memory?"
                        );
                    skeleton_Destroy(skeleton);
                    return 0;
                } else {
                    fprintf(stderr,
                        "horse3d/meshes.c: warning: GLTF "
                        "skipped invalid polygon in model %s\n",
                        originalgltfpath
                    );
                }
            } else {
                polycount++;
            }
            j += 3;
        }
        i++;
    }
    #ifdef DEBUG_MESH_LOADER
    printf(
        "horse3d/meshes_gltf.c: debug: GLTF added %" PRId64
        " polygons\n", polycount
    );
    #endif
    skeleton_Destroy(skeleton);
    return 1;
}

typedef struct meshgeometryaddallinfo {
    int failed_to_add_mesh;
    int nodes_added_count;
    h3dmesh_geometry *builtmeshgeo;
    h3dmeshloadinfo *loadinfo;
    const char *originalgltfpath;
    char **error;
} meshgeometryaddallinfo;

static int _addmeshfromallcgltfnodes(
        cgltf_node *node, void *userdata
        ) {
    meshgeometryaddallinfo *ainfo = userdata;
    if (node->mesh) {
        if (!mesh_AddFromCGLTFMeshNode(
                ainfo->builtmeshgeo,
                node,
                ainfo->loadinfo,
                ainfo->originalgltfpath,
                ainfo->error)) {
            ainfo->failed_to_add_mesh = 1;
            return 0;
        }
        ainfo->nodes_added_count++;
    }
    return 1;
}

h3dmesh *mesh_LoadFromGLTFData(
        cgltf_data *data,
        const char *originalgltfpath,
        int geometry_from_all_nodes,
        h3dmeshloadinfo *loadinfo,
        char **error
        ) {
    if (error)
        *error = NULL;
    if (!data) {
        return NULL;
    }

    h3dmesh *builtmesh = NULL;
    h3dmesh *_result = NULL;

    builtmesh = malloc(sizeof(*builtmesh));
    if (!builtmesh)
        goto errororquit;
    memset(builtmesh, 0, sizeof(*builtmesh));
    h3dmesh_geometry *builtmeshgeo =
        &(builtmesh->default_geometry);

    size_t mesh_node_count = _cgltfmeshnodecount(data);
    #ifdef DEBUG_MESH_LOADER
    printf(
        "horse3d/meshes.c: debug: GLTF "
        "amount of mesh nodes: %d\n",
        (int)mesh_node_count
    );
    #endif

    if (!geometry_from_all_nodes) {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: GLTF: "
            "add geometry of first mesh node...\n"
        );
        #endif
        cgltf_node *mesh_node = _findcgltfmeshnode(data, 0);

        if (!mesh_node) {
            if (error)
                *error = strdup("gltf file has zero mesh nodes");
            goto errororquit;
        }
        if (!mesh_AddFromCGLTFMeshNode(
                builtmeshgeo, mesh_node, loadinfo, originalgltfpath,
                error))
            goto errororquit;
    } else {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: GLTF "
            "add geometry of all mesh nodes...\n"
        );
        #endif

        meshgeometryaddallinfo ainfo;
        memset(&ainfo, 0, sizeof(ainfo));
        ainfo.failed_to_add_mesh = 0;
        ainfo.builtmeshgeo = builtmeshgeo;
        ainfo.loadinfo = loadinfo;
        ainfo.originalgltfpath = originalgltfpath;
        ainfo.error = error;

        if (!_mesh_IterateCGLTFNodes(
                data, &_addmeshfromallcgltfnodes, &ainfo
                ) || ainfo.failed_to_add_mesh)
            goto errororquit;
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: GLTF "
            "added geometry of %d mesh nodes\n",
            ainfo.nodes_added_count
        );
        #endif
    }

    if (builtmeshgeo->texturepart_count <= 0) {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: GLTF has not a "
            "single polygon, aborting.\n"
        );
        #endif
        if (error)
            *error = strdup("mesh has no polygons");
        goto errororquit;
    } else {
        #ifdef DEBUG_MESH_LOADER
        printf(
            "horse3d/meshes.c: debug: GLTF parsing done!\n"
        );
        #endif
    }

    builtmesh->min_x = builtmesh->default_geometry.min_x;
    builtmesh->max_x = builtmesh->default_geometry.max_x;
    builtmesh->min_y = builtmesh->default_geometry.min_y;
    builtmesh->max_y = builtmesh->default_geometry.max_y;
    builtmesh->min_z = builtmesh->default_geometry.min_z;
    builtmesh->max_z = builtmesh->default_geometry.max_z;
    int i = 0;
    while (i < builtmesh->default_geometry.texturepart_count) {
        if (builtmesh->default_geometry.
                texturepart[i].attribute_withalpha) {
            builtmesh->has_transparent_parts = 1;
            break;
        }
        i++;
    }
    _result = builtmesh;

    errororquit:
    if (!_result && builtmesh) {
        mesh_DestroyMesh(builtmesh);
    }

    return _result;
}
