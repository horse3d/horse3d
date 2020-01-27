#include <assert.h>
#include <check.h>

#include "skeleton.h"

#include "testmain.h"

START_TEST (test_skeleton)
{
    h3dskeleton *sk = skeleton_New();
    h3dskeleton_bone *bparent = bone_New(sk, NULL);
    h3dskeleton_bone *bchild = bone_New(sk, bparent);
    assert(bchild->parent == bparent);
    assert(bchild->id != bparent->id);

    double pos[3];
    pos[0] = 0; pos[1] = 0; pos[2] = 0;
    bone_TransformPosition(
        bchild, pos, pos
    );
    assert(pos[0] == 0 && pos[1] == 0 && pos[2] == 0);

    pos[0] = 0; pos[1] = 0; pos[2] = 0;
    bchild->local_position[0] = 1;
    bparent->local_position[0] = -2;
    bone_TransformPosition(
        bchild, pos, pos
    );
    assert(pos[0] == -1 && pos[1] == 0 && pos[2] == 0);

    skeleton_Destroy(sk);
}
END_TEST

TESTS_MAIN(test_skeleton)
