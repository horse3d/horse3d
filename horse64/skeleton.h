#ifndef HORSE3D_SKELETON_H_
#define HORSE3D_SKELETON_H_

#include <stdint.h>


typedef struct mesh3dtexturepart mesh3dtexturepart;
typedef struct h3dskeleton_bone h3dskeleton_bone;
typedef struct h3dskeleton h3dskeleton;

typedef struct h3dskeleton_bone {
    int id;
    int parent_id;
    char *name;

    double local_scale[3];
    double local_position[3];
    double local_rotation[4];

    int affected_vertices_count;
    int *affected_vertices_id;
    mesh3dtexturepart **affected_vertices_part;
    double affected_vertex_weight;

    h3dskeleton_bone *parent;
    int child_count;
    h3dskeleton_bone **child;
    h3dskeleton *skeleton;
} h3dskeleton_bone;

typedef struct h3dskeleton {
    int bone_count;
    h3dskeleton_bone **bone;
    int last_used_bone_id;
} h3dskeleton;


h3dskeleton *skeleton_New();

h3dskeleton_bone *bone_New(
    h3dskeleton *sk, h3dskeleton_bone *parent
);

void skeleton_Destroy(h3dskeleton *sk);

void bone_Destroy(h3dskeleton_bone *bone);

void bone_TransformPosition(
    h3dskeleton_bone *bone, double output[3], double vec[3]
);

void bone_TransformDirectionVec(
    h3dskeleton_bone *bone, double output[3], double vec[3]
);

int bone_Reparent(h3dskeleton_bone *bone, h3dskeleton_bone *new_parent);

#endif  // HORSE3D_SKELETON_H_
