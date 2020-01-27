#ifndef HORSE3D_COLWORLD_INTERNAL_H_
#define HORSE3D_COLWORLD_INTERNAL_H_

typedef struct h3dcolworld h3dcolworld;

typedef struct h3dcolobject {
    h3dcolworld *cworld;
    int type;
    btRigidBody *rigidbody;
    btDefaultMotionState *mstate;

    btCollisionShape *bodyshape, *bodyshapethin, *bodyshapelowheight;
    int shape_is_convex;

    double dimensions_x, dimensions_y, dimensions_z;
    double x, y, z;
    double rotx, roty, rotz, rotw;
    double scale;
    double _ccdminsize;

    double friction, restitution, mass;
    int disabledlocalgravity;

    int trianglemeshfinalized;
    btTriangleMesh *trianglemesh;
} h3dcolobject;


#endif  // HORSE3D_COLWORLD_INTERNAL_H_
