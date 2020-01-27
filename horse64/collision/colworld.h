#ifndef HORSE3D_COLWORLD_H_
#define HORSE3D_COLWORLD_H_

#ifdef __cplusplus
extern "C" {
#endif

#define PHYSICS_STEP_MS ((int)(1000/60))

typedef struct h3dcolworld {
    double gravity_x, gravity_y, gravity_z;

    int needAABBUpdate;
    void *bulletworldptr;
} h3dcolworld;

#define COLWORLD_OTYPE_BOX 1
#define COLWORLD_OTYPE_CAPSULE 2
#define COLWORLD_OTYPE_TRIANGLEMESH 3

typedef struct h3dcolobject h3dcolobject;


h3dcolworld *colworld_NewWorld();

double colworld_GetEffectiveObjectScale(
    h3dcolobject *obj
);

void colworld_DestroyWorld(h3dcolworld *cworld);

void colworld_SetObjectScale(h3dcolobject *obj, double scale);

h3dcolobject *colworld_NewBoxObject(
    h3dcolworld *cworld,
    double size_x, double size_y, double size_z,
    double mass, double friction
);

h3dcolobject *colworld_NewCapsuleObject(
    h3dcolworld *cworld,
    double size_w, double size_h,
    double mass, double friction
);

h3dcolobject *colworld_NewTriangleMeshObject(
    h3dcolworld *cworld
);

int colworld_AddTriangleMeshPolygon(
    h3dcolobject *obj,
    double pos1x, double pos1y, double pos1z,
    double pos2x, double pos2y, double pos2z,
    double pos3x, double pos3y, double pos3z
);

void colworld_GetObjectRotation(
    h3dcolobject *obj, double *quaternion
);

void colworld_SetObjectRotation(
    h3dcolobject *obj, double *quaternion
);

void colworld_GetObjectPosition(
    h3dcolobject *obj, double *x, double *y, double *z
);

void colworld_SetObjectPosition(
    h3dcolobject *obj, double x, double y, double z
);

int colworld_UpdateWorld(
    h3dcolworld *cworld, double *dt,
    void (*stepCallback)(h3dcolworld *world, double dt)
);

void colworld_GetObjectOrientation(h3dcolobject *obj, double *quat);

void colworld_DestroyObject(h3dcolobject *obj);

void colworld_SetObjectFixedAngle(
    h3dcolobject *obj, int enabled
);

void colworld_DisableObjectGravity(
    h3dcolobject *obj, int disabled
);

int colworld_DoRaycast(
    h3dcolworld *cworld,
    const double from[3], const double to[3],
    h3dcolobject *optional_ignore_object,
    double result_impact[3], double result_normal[3],
    double *result_distance
);

int colworld_ObjectDoSweep(
    h3dcolobject *obj,
    const double endpos[3],
    double *result_distanceToCollision,
    double result_normal[3]
);

int colworld_ObjectDoSweepThin(
    h3dcolobject *obj,
    const double endpos[3],
    double *result_distanceToCollision,
    double result_normal[3]
);

int colworld_ObjectDoSweepLowHeight(
    h3dcolobject *obj,
    const double endpos[3],
    double *result_distanceToCollision,
    double result_normal[3]
);

void colworld_ObjectApplyForce(
    h3dcolobject *obj, double force[3]
);

void colworld_ConvertObjectToMultishape(h3dcolobject *obj);

void colworld_SetObjectMass(h3dcolobject *obj, double mass);

double colworld_GetObjectMass(h3dcolobject *obj);

void colworld_SetFriction(
    h3dcolobject *obj, double friction
);

#ifdef __cplusplus
}
#endif

#endif  // HORSE3D_COLWORLD_H_
