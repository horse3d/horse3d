#ifndef HORSE3D_WORLD_H_
#define HORSE3D_WORLD_H_

#define OBJTYPE_INVISIBLENOMESH 0
#define OBJTYPE_MESH 1
#define OBJTYPE_SPRITE 2

#define OBJCOLTYPE_UNDETERMINED 0
#define OBJCOLTYPE_STATIC_TRIANGLEMESH 1
#define OBJCOLTYPE_BOX 2
#define OBJCOLTYPE_CHARACTER 3
#define OBJCOLTYPE_OFF 4
#define OBJCOLTYPE_STATIC_BOX 5

typedef struct h3dmesh h3dmesh;
typedef struct h3dcolobject h3dcolobject;
typedef struct h3dcolcharacter h3dcolcharacter;

typedef struct h3dobject {
    double x, y, z;
    double quat[4];
    double scale;
    double mass;
    int automass;

    int objecttransformcached;
    double objecttransform[16];
    double objecttransform_rotationonly[16];

    int _offset_was_set_by_mesh_center;
    double visual_offset_x;
    double visual_offset_y;
    double visual_offset_z;
    double visual_offset_x_scaled;
    double visual_offset_y_scaled;
    double visual_offset_z_scaled;

    int id;
    int type;
    union {
        h3dmesh *mesh;
    };
    double _invisible_w, _invisible_h;
    double _invisible_stepheight;

    int tag_count;
    char **tags;

    int hasdynamics;
    int colobject_updated;
    h3dcolobject *colobject;
    h3dcolcharacter *colcharacter;
    int collisiontype;
} h3dobject;


h3dobject *world_GetObjectById(int id);

h3dobject *world_AddMeshObject(
    h3dmesh *m
);

h3dobject *world_AddInvisibleObject(
    double size_w, double size_h
);

int world_AddObjectTag(h3dobject *obj, const char *tag);

void world_SetObjectScale(h3dobject *obj, double scale);

int world_Render();

void world_EnsureObjectPhysicsShapes();

void world_Update();

void world_GetObjectRotation(
    h3dobject *obj, double *quaternion
);

void world_SetObjectRotation(
    h3dobject *obj, double *quaternion
);

void world_GetObjectPosition(
    h3dobject *obj, double *x, double *y, double *z
);

void world_SetObjectPosition(
    h3dobject *obj, double x, double y, double z
);

void world_EnableObjectDynamics(h3dobject *obj);

void world_SetObjectCollisionType(h3dobject *obj, int type);

void world_SetDrawCollisionBoxes(int dodraw);

void world_GetObjectDimensions(
    h3dobject *obj, double *size_x, double *size_y,
    double *size_z
);

double world_GetCharObjectZPosShift(h3dobject *obj);

void world_ApplyObjectForce(
    h3dobject *obj, double x, double y, double z
);

void world_SetObjectMass(h3dobject *obj, double mass);

double world_GetObjectMass(h3dobject *obj);

void world_DestroyObject(h3dobject *obj);

void world_AutoCalcMass(h3dobject *obj);

void world_IterateObjects(
    const char *filter_by_tag,
    void (*iter_callback)(h3dobject *obj, void *userdata),
    void *userdata
);

void world_DisableObjectCollision(h3dobject *obj, int disabled);

void world_DisableDraw(int disabled);

#endif  // HORSE3D_WORLD_H_
