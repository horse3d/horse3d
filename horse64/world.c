
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "collision/charactercontroller.h"
#include "collision/colworld.h"
#include "hash.h"
#include "math3d.h"
#include "meshes.h"
#include "render3d.h"
#include "scriptcore.h"
#include "scriptcoreworld.h"
#include "uiplane.h"
#include "world.h"

#define DEFAULT_STEPHEIGHT 0.2


static int draw_is_disabled = 0;
static int draw_collision_boxes = 0;
static int objects_count = 0;
static h3dobject **objects_all = 0;
static int _lastobjidused = 0;
static hashmap *object_by_id_cache = NULL;

h3dobject **queue_objects_physicsupdate = NULL;
int queue_objects_physicsupdate_len = 0;
h3dcolworld *collisionworld = NULL;


void world_ObjRetrievePos(h3dobject *obj) {
    if (obj->colobject) {
        double oldx = obj->x;
        double oldy = obj->y;
        double oldz = obj->z;
        colworld_GetObjectPosition(
            obj->colobject, &obj->x, &obj->y, &obj->z
        );
        obj->z -= world_GetCharObjectZPosShift(obj) * 0.5;
        if (oldx != obj->x || oldy != obj->y || oldz != obj->z)
            obj->objecttransformcached = 0;
    }
}

void world_DisableObjectCollision(h3dobject *obj, int disabled) {
    if (!obj)
        return;
    if (disabled)
        world_SetObjectCollisionType(obj, OBJCOLTYPE_OFF);
    else if (obj->collisiontype != OBJCOLTYPE_OFF)
        return;
    else
        world_SetObjectCollisionType(obj, OBJCOLTYPE_UNDETERMINED);
}

void world_ObjStorePos(h3dobject *obj) {
     if (obj->colobject) {
        double oldx = obj->x;
        double oldy = obj->y;
        double oldz = obj->z;
        colworld_SetObjectPosition(
            obj->colobject,
            obj->x,
            obj->y,
            obj->z + world_GetCharObjectZPosShift(obj) * 0.5
        );
    }
}

h3dobject *world_GetObjectById(int id) {
    if (!object_by_id_cache)
        return NULL;
    uint64_t queryid = id;
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            object_by_id_cache,
            (char*)&queryid, sizeof(queryid),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3dobject*)(void*)hashptrval;
}

double world_GetCharObjectZPosShift(h3dobject *obj) {
    if (!obj || obj->collisiontype != OBJCOLTYPE_CHARACTER)
        return 0;
    if (obj->type == OBJTYPE_MESH) {
        if (!obj->mesh)
            return 0;
        return (
            fabs(obj->mesh->max_z - obj->mesh->min_z) *
            obj->_invisible_stepheight * obj->scale
        );
    }
    return obj->_invisible_h * obj->_invisible_stepheight * obj->scale;
}

void world_SetDrawCollisionBoxes(int dodraw) {
    draw_collision_boxes = (dodraw != 0);
}

static int world_QueueObjectForPhysicsUpdate(
        h3dobject *obj
        ) {
    obj->colobject_updated = 0;
    h3dobject **queue_objects_physicsupdate_new = realloc(
        queue_objects_physicsupdate,
        sizeof(*queue_objects_physicsupdate) *
        (queue_objects_physicsupdate_len + 1)
    );
    if (!queue_objects_physicsupdate_new)
        return 0;
    queue_objects_physicsupdate = (
        queue_objects_physicsupdate_new
    );
    queue_objects_physicsupdate[
        queue_objects_physicsupdate_len
    ] = obj;
    queue_objects_physicsupdate_len++;
    return 1;
}

void world_SetObjectOffsetByMeshCenter(h3dobject *obj) {
    if (obj->_offset_was_set_by_mesh_center || !obj->mesh)
        return;
    h3dmesh *m = obj->mesh;
    obj->_offset_was_set_by_mesh_center = 1;
    obj->visual_offset_x = m->min_x + (m->max_x - m->min_x) * 0.5;
    obj->visual_offset_y = m->min_y + (m->max_y - m->min_y) * 0.5;
    obj->visual_offset_z = m->min_z + (m->max_z - m->min_z) * 0.5;
    obj->visual_offset_x_scaled = obj->visual_offset_x * obj->scale;
    obj->visual_offset_y_scaled = obj->visual_offset_y * obj->scale;
    obj->visual_offset_z_scaled = obj->visual_offset_z * obj->scale;
    obj->objecttransformcached = 0;
}

void world_SetObjectScale(h3dobject *obj, double scale) {
    if (scale < 0.0001)
        scale = 0.0001;
    obj->scale = scale;
    obj->visual_offset_x_scaled = obj->visual_offset_x * obj->scale;
    obj->visual_offset_y_scaled = obj->visual_offset_y * obj->scale;
    obj->visual_offset_z_scaled = obj->visual_offset_z * obj->scale;
    obj->objecttransformcached = 0;
    if (obj->colobject)
        colworld_SetObjectScale(obj->colobject, obj->scale);
    if (obj->colcharacter && obj->automass)
        charactercontroller_UpdateMassAfterScaling(obj->colcharacter);
    else if (obj->automass)
        world_AutoCalcMass(obj);
}

double world_GetObjectMass(h3dobject *obj) {
    return obj->mass;
}

void world_SetObjectMass(h3dobject *obj, double mass) {
    obj->automass = 0;
    obj->mass = mass;
    if (obj->colobject)
        colworld_SetObjectMass(obj->colobject, mass);
}

void world_AutoCalcMass(h3dobject *obj) {
    double sx = 0;
    double sy = 0;
    double sz = 0;
    obj->automass = 1;
    world_GetObjectDimensions(obj, &sx, &sy, &sz);
    obj->mass = (
        fabs(sx) * fabs(sy) * fabs(sz)
    ) * 49;
}

h3dobject *world_AddInvisibleObject(
        double size_w, double size_h
        ) {
    if (size_w <= 0 || size_h <= 0)
        return NULL;

    h3dobject **objects_all_new = realloc(
        objects_all,
        sizeof(*objects_all_new) * (objects_count + 1)
    );
    if (!objects_all_new)
        return NULL;
    objects_all = objects_all_new;

    h3dobject *obj = malloc(sizeof(*obj));
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->quat[3] = 1.0;
    if (!world_QueueObjectForPhysicsUpdate(obj)) {
        free(obj);
        return NULL;
    }
    _lastobjidused++;
    obj->id = _lastobjidused;
    obj->type = OBJTYPE_INVISIBLENOMESH;
    obj->_invisible_w = size_w;
    obj->_invisible_h = size_h;
    obj->automass = 1;
    obj->scale = 1;
    world_AutoCalcMass(obj);
    obj->_invisible_stepheight = DEFAULT_STEPHEIGHT;
    if (!object_by_id_cache) {
        object_by_id_cache = hash_NewBytesMap(128);
        if (!object_by_id_cache) {
            objectidcachefail:
            ; int k = 0;
            while (k < queue_objects_physicsupdate_len) {
                if (queue_objects_physicsupdate[k] == obj)
                    queue_objects_physicsupdate[k] = 0;
                k++;
            }
            free(obj);
            return NULL;
        }
    }
    uint64_t id = obj->id;
    if (!hash_BytesMapSet(
            object_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)obj)) {
        goto objectidcachefail;
    }
    objects_all[objects_count] = obj;
    objects_count++;
    return obj;
}

h3dobject *world_AddMeshObject(
        h3dmesh *m
        ) {
    if (!m)
        return NULL;

    h3dobject **objects_all_new = realloc(
        objects_all,
        sizeof(*objects_all_new) * (objects_count + 1)
    );
    if (!objects_all_new)
        return NULL;
    objects_all = objects_all_new;

    h3dobject *obj = malloc(sizeof(*obj));
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->quat[3] = 1.0;
    if (!world_QueueObjectForPhysicsUpdate(obj)) {
        free(obj);
        return NULL;
    }
    _lastobjidused++;
    obj->id = _lastobjidused;
    obj->type = OBJTYPE_MESH;
    obj->mesh = m;
    obj->automass = 1;
    obj->scale = 1;
    world_AutoCalcMass(obj);
    obj->_invisible_stepheight = DEFAULT_STEPHEIGHT;
    if (!object_by_id_cache) {
        object_by_id_cache = hash_NewBytesMap(128);
        if (!object_by_id_cache) {
            objectidcachefail:
            ; int k = 0;
            while (k < queue_objects_physicsupdate_len) {
                if (queue_objects_physicsupdate[k] == obj)
                    queue_objects_physicsupdate[k] = 0;
                k++;
            }
            free(obj);
            return NULL;
        }
    }
    uint64_t id = obj->id;
    if (!hash_BytesMapSet(
            object_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)obj)) {
        goto objectidcachefail;
    }
    objects_all[objects_count] = obj;
    objects_count++;
    return obj;
}

void world_DisableDraw(int disabled) {
    draw_is_disabled = (disabled != 0);
}

int world_Render() {
    if (draw_is_disabled)
        return 1;
    int failure = 0;

    // SOLID PASS:
    int i = 0;
    while (i < objects_count) {
        if (!objects_all[i]) {
            i++;
            continue;
        }
        h3dobject *obj = objects_all[i];
        if (obj->colobject) {
            world_ObjRetrievePos(obj);
            if (obj->collisiontype != OBJCOLTYPE_CHARACTER)
                colworld_GetObjectRotation(
                    obj->colobject, obj->quat
                );
        }
        if (obj->type == OBJTYPE_MESH && obj->mesh) {
            if (!render3d_RenderMeshForObject(
                    obj->mesh, 0, obj
                    ))
                failure = 1;
        }
        i++;
    }
    // COLLISION BOXES:
    if (draw_collision_boxes) {
        int i = 0;
        while (i < objects_count) {
            if (!objects_all[i]) {
                i++;
                continue;
            }
            h3dobject *obj = objects_all[i];
            if (((obj->type == OBJTYPE_MESH && obj->mesh) ||
                    obj->type == OBJTYPE_INVISIBLENOMESH) &&
                    (obj->collisiontype == OBJCOLTYPE_BOX ||
                     obj->collisiontype == OBJCOLTYPE_CHARACTER ||
                     obj->collisiontype == OBJCOLTYPE_STATIC_BOX)) {
                // Make box based on collision shape:
                double v[3 * 8];
                double size_x, size_y, size_z;
                double colscale = obj->scale;
                if (obj->colobject)
                    colscale = (
                        colworld_GetEffectiveObjectScale(obj->colobject)
                    );
                if (obj->type == OBJTYPE_MESH && obj->mesh) {
                    h3dmesh *m = obj->mesh;
                    size_x = fabs(m->max_x - m->min_x) * colscale;
                    size_y = fabs(m->max_y - m->min_y) * colscale;
                    size_z = fabs(m->max_z - m->min_z) * colscale;
                } else if (obj->type == OBJTYPE_INVISIBLENOMESH) {
                    size_x = obj->_invisible_w * colscale;
                    size_y = obj->_invisible_w * colscale;
                    size_z = obj->_invisible_h * colscale;
                } else {
                    i++;
                    continue;
                }
                int k = 0;
                while (k < 8) {
                    v[k * 3 + 0] = size_x / 2.0;
                    v[k * 3 + 1] = size_y / 2.0;
                    v[k * 3 + 2] = size_z / 2.0;
                    if (k == 1 || k == 3 || k == 5 || k == 7) {
                        v[k * 3 + 1] *= -1;
                    }
                    if (k == 2 || k == 3 || k == 6 || k == 7) {
                        v[k * 3 + 0] *= -1;
                    }
                    if (k == 0 || k == 1 || k == 2 || k == 3) {
                        v[k * 3 + 2] *= -1;
                    }
                    k++;
                }
                // Rotate:
                if (obj->collisiontype != OBJCOLTYPE_CHARACTER) {
                    k = 0;
                    while (k < 8) {
                        vec3_rotate_quat(
                            &v[k * 3], &v[k * 3], obj->quat
                        );
                        k++;
                    }
                }
                // Translate:
                k = 0;
                while (k < 3 * 8) {
                    v[k + 0] += obj->x;
                    v[k + 1] += obj->y;
                    v[k + 2] += obj->z;
                    k += 3;
                }
                // Draw 3d lines:
                k = 0;
                while (k < 3 * 8) {
                    render3d_Draw3DLine(
                        v[0 + 0 + k], v[1 + 0 + k], v[2 + 0 + k],
                        v[0 + 3 + k], v[1 + 3 + k], v[2 + 3 + k],
                        255, 100, 0, 0.5
                    );
                    render3d_Draw3DLine(
                        v[0 + 3 + k], v[1 + 3 + k], v[2 + 3 + k],
                        v[0 + 9 + k], v[1 + 9 + k], v[2 + 9 + k],
                        255, 100, 0, 0.5
                    );
                    render3d_Draw3DLine(
                        v[0 + 6 + k], v[1 + 6 + k], v[2 + 6 + k],
                        v[0 + 9 + k], v[1 + 9 + k], v[2 + 9 + k],
                        255, 100, 0, 0.5
                    );
                    render3d_Draw3DLine(
                        v[0 + 0 + k], v[1 + 0 + k], v[2 + 0 + k],
                        v[0 + 6 + k], v[1 + 6 + k], v[2 + 6 + k],
                        255, 100, 0, 0.5
                    );
                    k += 3 * 4;
                }
                k = 0;
                while (k < 3 * 4) {
                    render3d_Draw3DLine(
                        v[0 + 0 + k], v[1 + 0 + k], v[2 + 0 + k],
                        v[0 + 12 + k], v[1 + 12 + k], v[2 + 12 + k],
                        255, 100, 0, 0.5
                    );
                    k += 3;
                }
            }
            i++;
        }
    }
    // UI PLANES IN 3D SPACE:
    // FIXME FIXME

    // TRANSPARENT PASS:
    i = 0;
    while (i < objects_count) {
        if (!objects_all[i]) {
            i++;
            continue;
        }
        h3dobject *obj = objects_all[i];
        if (obj->type == OBJTYPE_MESH &&
                obj->mesh && !obj->mesh->has_transparent_parts) {
            i++;
            continue;
        }
        if (obj->colobject) {
            world_ObjRetrievePos(obj);
            if (obj->collisiontype != OBJCOLTYPE_CHARACTER)
                colworld_GetObjectRotation(
                    obj->colobject, obj->quat
                );
        }
        if (obj->type == OBJTYPE_MESH && obj->mesh) {
            if (!render3d_RenderMeshForObject(
                    obj->mesh, 1, obj
                    ))
                failure = 1;
        }
        i++;
    }

    // DRAW UI ON TOP:
    if (!uiplane_DrawScreenPlanes())
        failure = 1;

    if (failure)
        return 0;
    return 1;
}

void world_EnableObjectDynamics(h3dobject *obj) {
    if (!obj || obj->hasdynamics)
        return;
    if (obj->colobject) {
        world_ObjRetrievePos(obj);
        colworld_DestroyObject(obj->colobject);
        obj->colobject = NULL;
    }

    if (obj->type == OBJTYPE_MESH && obj->mesh &&
            (obj->collisiontype == OBJCOLTYPE_STATIC_TRIANGLEMESH ||
             obj->collisiontype == OBJCOLTYPE_UNDETERMINED)) {
        obj->collisiontype = OBJCOLTYPE_BOX;
    } else if (obj->type == OBJTYPE_INVISIBLENOMESH &&
            (obj->collisiontype == OBJCOLTYPE_STATIC_TRIANGLEMESH ||
             obj->collisiontype == OBJCOLTYPE_UNDETERMINED)) {
        obj->collisiontype = OBJCOLTYPE_CHARACTER;
    }
    obj->colobject_updated = 0;
    obj->hasdynamics = 1;
    world_QueueObjectForPhysicsUpdate(obj);
}

void world_SetObjectCollisionType(h3dobject *obj, int type) {
    if (!obj || obj->collisiontype == type ||
            (!obj->hasdynamics &&
             type != OBJCOLTYPE_STATIC_TRIANGLEMESH &&
             type != OBJCOLTYPE_UNDETERMINED &&
             type != OBJCOLTYPE_OFF))
        return;
    if (obj->colobject) {
        world_ObjRetrievePos(obj);
        colworld_DestroyObject(obj->colobject);
        if (obj->colcharacter)
            charactercontroller_Destroy(obj->colcharacter);
        obj->colcharacter = NULL;
        obj->colobject = NULL;
    }
    obj->colobject_updated = 0;
    obj->collisiontype = type;
    world_QueueObjectForPhysicsUpdate(obj);
}

double world_StepHeightScalarRelativeToCollider(
        h3dobject *obj
        ) {
    double obj_h = obj->_invisible_h;
    if (obj->type == OBJTYPE_MESH) {
        assert(obj->mesh != NULL);
        obj_h = fabs(obj->mesh->max_z - obj->mesh->min_z);
    }
    assert(obj_h > 0);
    double step_height_abs = (
        obj_h * obj->_invisible_stepheight * obj->scale
    );
    double physobj_height = (
        obj_h * obj->scale - step_height_abs
    );
    assert(step_height_abs < physobj_height ||
           obj->_invisible_stepheight >= 0.99);
    double scalar = (step_height_abs / physobj_height);
    return scalar;
}

static int world_CharacterObjectUsesBoxFallback(
        h3dobject *obj
        ) {
    if (obj->collisiontype == OBJCOLTYPE_CHARACTER &&
             obj->type == OBJTYPE_INVISIBLENOMESH &&
             obj->_invisible_h * (1.0 - obj->_invisible_stepheight)
             < obj->_invisible_w * 1.1)
        return 1;
    if (obj->collisiontype == OBJCOLTYPE_CHARACTER &&
             obj->type == OBJTYPE_MESH && obj->mesh &&
             fabs(obj->mesh->max_z - obj->mesh->min_z) *
                (1.0 - obj->_invisible_stepheight)
             < fmax(
                 fabs(obj->mesh->max_x - obj->mesh->min_x),
                 fabs(obj->mesh->max_y - obj->mesh->min_y)
             ) * 1.1)
        return 1;
    return 0;
}

void world_EnsureObjectPhysicsShapes() {
    if (!collisionworld) {
        collisionworld = colworld_NewWorld();
        if (!collisionworld)
            return;
    }
    int queuenowempty = 1;
    int i = 0;
    while (i < queue_objects_physicsupdate_len) {
        if (!queue_objects_physicsupdate[i] ||
                queue_objects_physicsupdate[i]->colobject_updated) {
            i++;
            continue;
        }
        h3dobject *obj = queue_objects_physicsupdate[i];
        if (obj->colobject) {
            world_ObjRetrievePos(obj);
            colworld_DestroyObject(obj->colobject);
            if (obj->colcharacter)
                charactercontroller_Destroy(obj->colcharacter);
            obj->colcharacter = NULL;
            obj->colobject = NULL;
        }
        if (obj->collisiontype == OBJCOLTYPE_OFF) {
            obj->colobject_updated = 1;
            i++;
            continue;
        }
        if (obj->collisiontype == OBJCOLTYPE_UNDETERMINED) {
            if (!obj->hasdynamics) {
                if (obj->type == OBJTYPE_MESH)
                    obj->collisiontype = OBJCOLTYPE_STATIC_TRIANGLEMESH;
                else
                    obj->collisiontype = OBJCOLTYPE_STATIC_BOX;
            } else {
                if (obj->type == OBJTYPE_MESH) {
                    obj->collisiontype = OBJCOLTYPE_BOX;
                } else if (obj->type == OBJTYPE_INVISIBLENOMESH) {
                    obj->collisiontype = OBJCOLTYPE_CHARACTER;
                } else {
                    fprintf(
                        stderr, "horse3d/world.c: warning: "
                        "failed to auto-determine collision shape"
                    );
                }
            }
        }
        if (obj->type == OBJTYPE_MESH && obj->mesh &&
                obj->collisiontype == OBJCOLTYPE_STATIC_TRIANGLEMESH) {
            obj->colobject = (
                colworld_NewTriangleMeshObject(collisionworld)
            );
            if (!obj->colobject) {
                queuenowempty = 0;
                i++;
                continue;
            }
            colworld_SetObjectScale(obj->colobject, obj->scale);
            obj->collisiontype = OBJCOLTYPE_STATIC_TRIANGLEMESH;
            int k = 0;
            while (k < obj->mesh->default_geometry.texturepart_count) {
                if (obj->mesh->default_geometry.
                        texturepart[k].attribute_nocollision) {
                    k++;
                    continue;
                }
                mesh3dtexturepart *part = (
                    &obj->mesh->default_geometry.texturepart[k]
                );
                int failedtoadd = 0;
                int j = 0;
                while (j < part->polycount) {
                    if (!colworld_AddTriangleMeshPolygon(
                            obj->colobject,
                            part->vertex_x[part->polyvertex1[j]],
                            part->vertex_y[part->polyvertex1[j]],
                            part->vertex_z[part->polyvertex1[j]],
                            part->vertex_x[part->polyvertex2[j]],
                            part->vertex_y[part->polyvertex2[j]],
                            part->vertex_z[part->polyvertex2[j]],
                            part->vertex_x[part->polyvertex3[j]],
                            part->vertex_y[part->polyvertex3[j]],
                            part->vertex_z[part->polyvertex3[j]])) {
                        failedtoadd = 1;
                        break;
                    }
                    j++;
                }
                if (failedtoadd) {
                    colworld_DestroyObject(obj->colobject);
                    break;
                }
                k++;
            }
            if (!obj->colobject) {
                queuenowempty = 0;
                i++;
                continue;
            }
        } else if (obj->type == OBJTYPE_INVISIBLENOMESH &&
                obj->collisiontype == OBJCOLTYPE_STATIC_BOX) {
            double size_x, size_y, size_z;
            size_x = obj->_invisible_w;
            size_y = obj->_invisible_w;
            size_z = obj->_invisible_h;
            if (size_x < 0.0001)
                size_x = 0.01;
            if (size_y < 0.0001)
                size_y = 0.01;
            if (size_z < 0.0001)
                size_z = 0.01;
            obj->colobject = colworld_NewBoxObject(
                collisionworld,
                size_x, size_y, size_z,
                0, 0.1
            );
            if (!obj->colobject) {
                queuenowempty = 0;
                i++;
                continue;
            }
            colworld_SetObjectScale(obj->colobject, obj->scale);
        } else if ((obj->type != OBJTYPE_MESH || obj->mesh) && (
                obj->collisiontype == OBJCOLTYPE_BOX ||
                (obj->collisiontype == OBJCOLTYPE_CHARACTER &&
                 world_CharacterObjectUsesBoxFallback(obj)))) {
            double setmass = 0;
            if (obj->hasdynamics)
                setmass = obj->mass;
            double size_x, size_y, size_z;
            if (obj->mesh) {
                world_SetObjectOffsetByMeshCenter(obj);
                if (obj->collisiontype == OBJCOLTYPE_CHARACTER) {
                    size_x = fmax(
                        fabs(obj->mesh->max_x - obj->mesh->min_x),
                        fabs(obj->mesh->max_y - obj->mesh->min_y)
                    );
                    size_y = size_x;
                } else {
                    size_x = fabs(obj->mesh->max_x - obj->mesh->min_x);
                    size_y = fabs(obj->mesh->max_y - obj->mesh->min_y);
                }
                size_z = fabs(obj->mesh->max_z - obj->mesh->min_z);
            } else {
                size_x = obj->_invisible_w;
                size_y = obj->_invisible_w;
                size_z = obj->_invisible_h;
            }
            if (obj->collisiontype == OBJCOLTYPE_CHARACTER)
                size_z *= (1.0 - obj->_invisible_stepheight);
            if (size_x < 0.0001)
                size_x = 0.01;
            if (size_y < 0.0001)
                size_y = 0.01;
            if (size_z < 0.0001)
                size_z = 0.01;
            obj->colobject = colworld_NewBoxObject(
                collisionworld,
                size_x, size_y, size_z,
                setmass, 0.1
            );
            if (!obj->colobject) {
                queuenowempty = 0;
                i++;
                continue;
            }
            colworld_SetObjectScale(obj->colobject, obj->scale);
            if (obj->collisiontype == OBJCOLTYPE_CHARACTER) {
                obj->colcharacter = charactercontroller_New(
                    obj,
                    world_StepHeightScalarRelativeToCollider(obj)
                );
                if (!obj->colcharacter) {
                    colworld_DestroyObject(obj->colobject);
                    obj->colobject = NULL;
                    queuenowempty = 0;
                    i++;
                    continue;
                }
            }
        } else if ((obj->type != OBJTYPE_MESH || obj->mesh) &&
                obj->collisiontype == OBJCOLTYPE_CHARACTER &&
                !world_CharacterObjectUsesBoxFallback(obj)) {
            double capsule_radius, capsule_height;
            if (obj->type == OBJTYPE_MESH) {
                capsule_radius = fmax(
                    fabs(obj->mesh->max_x - obj->mesh->min_x),
                    fabs(obj->mesh->max_y - obj->mesh->min_y)
                ) * 0.5;
                capsule_height = (
                    fabs(obj->mesh->max_z - obj->mesh->min_z) *
                        (1.0 - obj->_invisible_stepheight) -
                    capsule_radius * 2
                );
            } else {
                capsule_radius = obj->_invisible_w * 0.5;
                capsule_height = (
                    obj->_invisible_h * (1.0 - obj->_invisible_stepheight) -
                    capsule_radius * 2
                );
            }
            double setmass = 0;
            if (obj->hasdynamics)
                setmass = obj->mass;
            if (capsule_radius > 0.001 && capsule_height > 0.0001) {
                world_SetObjectOffsetByMeshCenter(obj);
                obj->colobject = colworld_NewCapsuleObject(
                    collisionworld,
                    capsule_radius * 2,
                    capsule_height + capsule_radius * 2,
                    setmass, 0.1
                );
                if (!obj->colobject) {
                    queuenowempty = 0;
                    i++;
                    continue;
                }
                colworld_SetObjectScale(obj->colobject, obj->scale);
                if (obj->collisiontype == OBJCOLTYPE_CHARACTER) {
                    obj->colcharacter = charactercontroller_New(
                        obj,
                        world_StepHeightScalarRelativeToCollider(obj)
                    );
                    if (!obj->colcharacter) {
                        colworld_DestroyObject(obj->colobject);
                        obj->colobject = NULL;
                        queuenowempty = 0;
                        i++;
                        continue;
                    }
                }
            }
        } else {
            fprintf(
                stderr,
                "horse3d/world.c: warning: unexpected failure to "
                "set collision object for object %d\n", obj->id
            );
        }
        world_ObjStorePos(obj);
        obj->colobject_updated = 1;
        if (obj->colobject &&
                obj->collisiontype != OBJCOLTYPE_CHARACTER)
            colworld_SetObjectRotation(
                obj->colobject, obj->quat
            );
        i++;
    }
    if (queuenowempty) {
        free(queue_objects_physicsupdate);
        queue_objects_physicsupdate = NULL;
        queue_objects_physicsupdate_len = 0;
    }
}

void world_GetObjectRotation(
        h3dobject *obj, double *quaternion) {
    if (obj->colobject &&
            obj->collisiontype != OBJCOLTYPE_CHARACTER) {
        double oldquat[4];
        memcpy(oldquat, obj->quat, sizeof(oldquat));
        colworld_GetObjectRotation(
            obj->colobject, obj->quat
        );
        if (memcmp(oldquat, obj->quat, sizeof(oldquat)) != 0)
            obj->objecttransformcached = 0;
    }
    memcpy(quaternion, obj->quat, sizeof(*quaternion) * 4);
}

void world_SetObjectRotation(
        h3dobject *obj, double *quaternion) {
    if (memcmp(obj->quat, quaternion, sizeof(*quaternion) * 4) == 0)
        return;
    obj->objecttransformcached = 0;
    memcpy(obj->quat, quaternion, sizeof(*quaternion) * 4);
    if (obj->colobject &&
            obj->collisiontype != OBJCOLTYPE_CHARACTER)
        colworld_SetObjectRotation(
            obj->colobject, obj->quat
        );
}

void world_GetObjectPosition(
        h3dobject *obj, double *x, double *y, double *z
        ) {
    world_ObjRetrievePos(obj);
    *x = obj->x;
    *y = obj->y;
    *z = obj->z;
}

void world_SetObjectPosition(
        h3dobject *obj, double x, double y, double z
        ) {
    if (obj->x == x && obj->y == y && obj->z == z)
        return;
    obj->objecttransformcached = 0;
    obj->x = x;
    obj->y = y;
    obj->z = z;
    world_ObjStorePos(obj);
}

void world_ApplyObjectForce(
        h3dobject *obj, double x, double y, double z
        ) {
    double f[3];
    f[0] = x;
    f[1] = y;
    f[2] = z;
    if (obj->collisiontype == OBJCOLTYPE_CHARACTER) {
        if (obj->colcharacter)
            charactercontroller_ApplyForce(
                obj->colcharacter, f
            );
    } else {
        if (obj->colobject)
            colworld_ObjectApplyForce(
                obj->colobject, f
            );
    }
}

void world_GetObjectDimensions(
        h3dobject *obj, double *size_x, double *size_y,
        double *size_z
        ) {
    if (obj->type == OBJTYPE_INVISIBLENOMESH) {
        *size_x = obj->scale * obj->_invisible_w;
        *size_y = obj->scale * obj->_invisible_w;
        *size_z = obj->scale * obj->_invisible_h;
    } else if (obj->type == OBJTYPE_MESH && obj->mesh) {
        *size_x = obj->scale * (
            obj->mesh->max_x - obj->mesh->min_x
        );
        *size_y = obj->scale * (
            obj->mesh->max_y - obj->mesh->min_y
        );
        *size_z = obj->scale * (
            obj->mesh->max_z - obj->mesh->min_z
        );
    } else {
        *size_x = 0.5;
        *size_y = 0.5;
        *size_z = 0.5;
    }
}

void _world_RunLuaFuncs(h3dcolworld *cworld, double dt) {
    int i = 0;
    while (i < objects_count) {
        if (!objects_all[i]) {
            i++;
            continue;
        }
        world_RunObjectLuaOnUpdate(
            scriptcore_MainState(), objects_all[i], dt
        );
        i++;
    }
}

void world_Update() {
    world_EnsureObjectPhysicsShapes();
    if (!collisionworld)
        return;
    int i = 0;
    while (i < objects_count) {
        if (!objects_all[i]) {
            i++;
            continue;
        }
        if (objects_all[i]->colcharacter) {
            charactercontroller_Update(
                objects_all[i]->colcharacter
            );
        }
        i++;
    }
    double dt = 0;
    colworld_UpdateWorld(
        collisionworld, &dt, _world_RunLuaFuncs
    );
}

void world_DestroyObject(h3dobject *obj) {
    if (!obj)
        return;
    if (obj->colcharacter)
        charactercontroller_Destroy(obj->colcharacter);
    if (obj->colobject) {
        colworld_DestroyObject(obj->colobject);
        obj->colobject = NULL;
    }
    uint64_t queryid = obj->id;
    hash_BytesMapUnset(
        object_by_id_cache,
        (char*)&queryid, sizeof(queryid)
    );
    int k = 0;
    while (k < objects_count) {
        if (objects_all[k] == obj) {
            if (k < objects_count - 1) {
                memmove(
                    &objects_all[k],
                    &objects_all[k + 1],
                    sizeof(*objects_all) * (
                        objects_count - k - 1
                    )
                );
            }
            objects_count--;
            continue;
        }
        k++;
    }
    k = 0;
    while (k < queue_objects_physicsupdate_len) {
        if (queue_objects_physicsupdate[k] == obj) {
            if (k < queue_objects_physicsupdate_len - 1) {
                memmove(
                    &queue_objects_physicsupdate[k],
                    &queue_objects_physicsupdate[k + 1],
                    sizeof(*queue_objects_physicsupdate) * (
                        queue_objects_physicsupdate_len - k - 1
                    )
                );
            }
            queue_objects_physicsupdate_len--;
            continue;
        }
        k++;
    }
    if (obj->tag_count > 0) {
        k = 0;
        while (k < obj->tag_count) {
            free(obj->tags[k]);
            k++;
        }
        free(obj->tags);
    }
    free(obj);
}

void world_IterateObjects(
        const char *filter_by_tag,
        void (*iter_callback)(h3dobject *obj, void *userdata),
        void *userdata
        ) {
    int k = 0;
    while (k < objects_count) {
        if (!objects_all[k]) {
            k++;
            continue;
        }
        int skipbyfilter = 0;
        if (filter_by_tag && strlen(filter_by_tag) > 0) {
            skipbyfilter = 1;
            int i = 0;
            while (i < objects_all[k]->tag_count) {
                if (strcmp(objects_all[k]->tags[i], filter_by_tag) == 0) {
                    skipbyfilter = 0;
                    break;
                }
                i++;
            }
        }
        if (!skipbyfilter) {
            iter_callback(objects_all[k], userdata);
        }
        k++;
    }
}

int world_AddObjectTag(h3dobject *obj, const char *tag) {
    if (!obj || !tag || strlen(tag) == 0)
        return 0;

    int i = 0;
    while (i < obj->tag_count) {
        if (strcmp(obj->tags[i], tag) == 0)
            return 1;
        i++;
    }
    char *new_tag = strdup(tag);
    if (!new_tag)
        return 0;
    char **new_tags = realloc(
        obj->tags,
        sizeof(*obj->tags) * (obj->tag_count + 1)
    );
    if (!new_tags) {
        free(new_tag);
        return 0;
    }
    obj->tags = new_tags;
    obj->tags[obj->tag_count] = new_tag;
    obj->tag_count++;
    return 1;
}
