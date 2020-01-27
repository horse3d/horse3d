
#include <assert.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "math3d.h"
#include "skeleton.h"


h3dskeleton *skeleton_New() {
    h3dskeleton *sk = malloc(sizeof(*sk));
    if (!sk)
        return NULL;
    memset(sk, 0, sizeof(*sk));

    return sk;
}

h3dskeleton_bone *bone_New(
        h3dskeleton *sk, h3dskeleton_bone *parent
        ) {
    h3dskeleton_bone *bone = malloc(sizeof(*bone));
    if (!bone)
        return NULL;
    memset(bone, 0, sizeof(*bone));
    bone->local_scale[0] = 1.0;
    bone->local_scale[1] = 1.0;
    bone->local_scale[2] = 1.0;
    bone->local_rotation[3] = 1.0;

    h3dskeleton_bone **new_bones = realloc(
        sk->bone,
        sizeof(*sk->bone) * (sk->bone_count + 1)
    );
    if (!new_bones) {
        bone_Destroy(bone);
        return NULL;
    }
    sk->bone = new_bones;
    sk->bone[sk->bone_count] = bone;
    sk->bone_count++;

    if (parent) {
        if (parent->skeleton != sk) {
            bone_Destroy(bone);
            return NULL;
        }
        h3dskeleton_bone **new_children = realloc(
            parent->child,
            sizeof(*bone->child) * (parent->child_count + 1)
        );
        if (!new_children) {
            bone_Destroy(bone);
            return NULL;
        }
        parent->child = new_children;
        parent->child[parent->child_count] = bone;
        parent->child_count++;
    }

    bone->skeleton = sk;
    sk->last_used_bone_id++;
    bone->id = sk->last_used_bone_id;
    bone->parent = parent;
    bone->skeleton = sk;
    return bone;
}

void bone_Destroy(h3dskeleton_bone *bone) {
    if (!bone)
        return;
    if (bone->skeleton) {
        int i = 0;
        while (i < bone->skeleton->bone_count) {
            if (bone->skeleton->bone[i] == bone) {
                if (i + 1 < bone->skeleton->bone_count)
                    memmove(
                        &(bone->skeleton->bone[i]),
                        &(bone->skeleton->bone[i + 1]),
                        sizeof(*bone->skeleton->bone) *
                        bone->skeleton->bone_count - i - 1
                    );
                bone->skeleton->bone_count--;
                continue;
            }
            i++;
        }
    }
    if (bone->parent) {
        int i = 0;
        while (i < bone->parent->child_count) {
            if (bone->parent->child[i] == bone) {
                if (i + 1 < bone->parent->child_count)
                    memmove(
                        &(bone->parent->child[i]),
                        &(bone->parent->child[i + 1]),
                        sizeof(*bone->parent->child) *
                        bone->parent->child_count - i - 1
                    );
                bone->parent->child_count--;
                continue;
            }
            i++;
        }
    }
    if (bone->child_count > 0) {
        int i = 0;
        while (i < bone->child_count) {
            if (bone->child[i]->parent == bone)
                bone->child[i]->parent = NULL;
            i++;
        }
    }
    if (bone->child)
        free(bone->child);
    if (bone->name)
        free(bone->name);
    free(bone);
}

void skeleton_Destroy(h3dskeleton *sk) {
    h3dskeleton_bone **bones = sk->bone;
    int bone_count = sk->bone_count;
    sk->bone = NULL;
    sk->bone_count = 0;
    if (bones) {
        int i = 0;
        while (i < bone_count) {
            bone_Destroy(bones[i]);
            i++;
        }
        free(bones);
    }
    free(sk);
}

void bone_TransformPosition(
        h3dskeleton_bone *bone, double output[3], double vec[3]
        ) {
    double v[3];
    memcpy(v, vec, sizeof(v));

    vec3_rotate_quat(v, v, bone->local_rotation);
    v[0] *= bone->local_scale[0];
    v[1] *= bone->local_scale[1];
    v[2] *= bone->local_scale[2];
    v[0] += bone->local_position[0];
    v[1] += bone->local_position[1];
    v[2] += bone->local_position[2];

    if (bone->parent)
        bone_TransformPosition(bone->parent, v, v);

    memcpy(output, v, sizeof(v));
}

void bone_TransformDirectionVec(
        h3dskeleton_bone *bone, double output[3], double vec[3]
        ) {
    double v[3];
    memcpy(v, vec, sizeof(v));

    vec3_rotate_quat(v, v, bone->local_rotation);

    if (bone->parent)
        bone_TransformDirectionVec(bone->parent, v, v);

    memcpy(output, v, sizeof(v));
}

int bone_Reparent(h3dskeleton_bone *bone, h3dskeleton_bone *new_parent) {
    assert(!new_parent || new_parent->skeleton == bone->skeleton);
    if (new_parent != NULL) {
        if (new_parent == bone->parent)
            return 1;
        h3dskeleton_bone **new_children = realloc(
            new_parent->child,
            sizeof(*bone->child) * (new_parent->child_count + 1)
        );
        if (!new_children) {
            return 0;
        }
        new_parent->child = new_children;
        new_parent->child[new_parent->child_count] = bone;
        new_parent->child_count++;
    }
    h3dskeleton_bone *oldparent = bone->parent;
    bone->parent = new_parent;
    if (oldparent) {
        int i = 0;
        while (i < oldparent->child_count) {
            if (oldparent->child[i] == bone) {
                if (i + 1 < oldparent->child_count)
                    memmove(
                        &(oldparent->child[i]),
                        &(oldparent->child[i + 1]),
                        sizeof(*oldparent->child) *
                        oldparent->child_count - i - 1
                    );
                oldparent->child_count--;
                continue;
            }
            i++;
        }
    }
    return 1;
}
