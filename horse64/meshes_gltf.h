#ifndef HORSE3D_MESHES_GLTF_H_
#define HORSE3D_MESHES_GLTF_H_

#include "meshes.h"
#include "skeleton.h"

h3dmesh *mesh_LoadFromGLTFEx(
    const char *path, int usecache,
    h3dmeshloadinfo *loadinfo, char **error
);

typedef struct cgltf_data cgltf_data;
typedef struct cgltf_options cgltf_options;
typedef struct cgltf_node cgltf_node;

int _mesh_IterateCGLTFNodes(
    cgltf_data *data,
    int (*cb)(cgltf_node *node, void *userdata),
    void *userdata
);

h3dmesh *mesh_LoadFromGLTFData(
    cgltf_data *data,
    const char *originalgltfpath,
    int geometry_from_all_nodes,
    h3dmeshloadinfo *loadinfo,
    char **error
);

int _cgltf_loaddata(
    cgltf_options *gltf_options,
    const char *path, cgltf_data **out_data,
    char **error
);

h3dskeleton *mesh_GetCGLTFNodeTransformSkeleton(
    cgltf_node *node, h3dskeleton_bone **node_bone,
    char **error
);

#endif  // HORSE3D_MESHES_GLTF_H_
