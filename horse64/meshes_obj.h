#ifndef HORSE3D_MESHES_OBJ_H_
#define HORSE3D_MESHES_OBJ_H_

#include "meshes.h"


h3dmesh *mesh_LoadFromOBJEx(
    const char *path, int usecache,
    h3dmeshloadinfo *loadinfo,
    char **error
);


#endif  // HORSE3D_MESHES_OBJ_H_
