#ifndef HORSE3D_MESHES_H_
#define HORSE3D_MESHES_H_

#include "glheaders.h"
#include "render3d.h"
#include "skeleton.h"
#include "texture.h"

typedef struct h3dmaterial h3dmaterial;

typedef struct h3dmaterialprops {
    int unlit;
} h3dmaterialprops;

typedef struct mesh3dtexturepart {
    h3dtexture *tex;
    h3dmaterialprops matprops;
    h3dmaterial *assignedmaterial;

    int vertexcount;
    double *vertex_x, *vertex_y, *vertex_z;
    double *vertexuv_x, *vertexuv_y;
    double *vertexnormal_x, *vertexnormal_y, *vertexnormal_z;
    int affected_by_bone_id1,
        affected_by_bone_id2,
        affected_by_bone_id3;

    int polycount;
    int *polyvertex1, *polyvertex2, *polyvertex3;

    int attribute_invisible, attribute_nocollision,
        attribute_withalpha;

    int _gluploaded;
    GLuint _glvbo, _glvbouv, _glvbonormals;
} mesh3dtexturepart;

typedef struct h3dmesh_geometry {
    int texturepart_count;
    mesh3dtexturepart *texturepart;

    double max_x, min_x, max_y, min_y, max_z, min_z;
    double maxcol_x, mincol_x, maxcol_y, mincol_y, maxcol_z, mincol_z;
} h3dmesh_geometry;

typedef struct h3dmesh {
    h3dmesh_geometry default_geometry;

    double max_x, min_x, max_y, min_y, max_z, min_z;
    int has_transparent_parts;

    h3dskeleton *skeleton;
    h3dmesh_geometry *skeleton_animated_geometry;
} h3dmesh;


typedef struct h3dmeshloadinfo {
    int invisible_keywords_count;
    char **invisible_keywords;
    int nocollision_keywords_count;
    char **nocollision_keywords;
} h3dmeshloadinfo;


int meshgeo_GetPartIndexForTex(
    h3dmesh_geometry *meshgeo,
    h3dtexture *tex,
    h3dmaterialprops *props
);

mesh3dtexturepart *meshgeo_GetPartForTex(
    h3dmesh_geometry *meshgeo,
    h3dtexture *tex,
    h3dmaterialprops *props
);

h3dmesh *mesh_GetFromFile(
    const char *path, h3dmeshloadinfo *loadinfo, char **error
);

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
);

int meshgeopart_AddVertex(
    mesh3dtexturepart *part,
    double vertex_x, double vertex_y, double vertex_z,
    double vertexuv_x, double vertexuv_y,
    double vertexnormal_x, double vertexnormal_y,
    double vertexnormal_z
);

void mesh_DestroyMesh(h3dmesh *mesh);

void _internal_meshgeo_UpdateOverallBoundaries(
    h3dmesh_geometry *meshgeo, double x, double y, double z
);

int mesh_LocateModelReferencedFilePath(
    const char *model_path, const char *referenced_file_path,
    char **result_relative, char **result_absolute
);

h3dtexture *mesh_LocateModelReferencedTexture(
    const char *model_path, const char *referenced_texture_path
);

void meshloadinfo_Free(h3dmeshloadinfo *loadinfo);

void meshgeopart_AssignMaterial(mesh3dtexturepart *part);

void mesh_PrintLoadSummary(h3dmesh *m);

#endif  // HORSE3D_MESHES_H_
