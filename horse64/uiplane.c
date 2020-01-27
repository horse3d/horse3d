
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "hash.h"
#include "uiobject.h"
#include "uiplane.h"


h3duiplane *defaultplane = NULL;
h3duiplane **all_planes = NULL;
int all_planes_count = 0;
static uint64_t _last_uiplane_id = 0;
static hashmap *uiplane_by_id_cache = NULL;
static h3duiplane **unparented_planes = NULL;
static int unparented_planes_count = 0;


__attribute__((constructor)) static void _initplane() {
    defaultplane = uiplane_New();
}


h3duiplane *uiplane_GetPlaneById(uint64_t id) {
    if (!uiplane_by_id_cache)
        return NULL;
    uint64_t queryid = id;
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            uiplane_by_id_cache,
            (char*)&queryid, sizeof(queryid),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3duiplane*)(void*)hashptrval;
}

int uiplane_MarkAsUnparented(h3duiplane *plane) {
    if (!plane)
        return 0;

    int k = 0;
    while (k < unparented_planes_count) {
        if (unparented_planes[k] == plane)
            return 1;
        k++;
    }

    int new_count = unparented_planes_count + 1;
    h3duiplane **new_unparanted_planes = realloc(
        unparented_planes, sizeof(*unparented_planes) * new_count
    );
    if (!new_unparanted_planes) {
        return 0;
    }
    unparented_planes = new_unparanted_planes;
    unparented_planes[new_count - 1] = plane;
    unparented_planes_count = new_count;
    plane->parented = 0;
    return 1;
}

int uiplane_MarkAsParented(h3duiplane *plane) {
    if (!plane)
        return 0;

    int k = 0;
    while (k < unparented_planes_count) {
        if (unparented_planes[k] == plane)
            unparented_planes[k] = NULL;
        k++;
    }

    plane->parented = 1;
    return 1;
}

h3duiplane *uiplane_New() {
    h3duiplane *plane = malloc(sizeof(*plane));
    if (!plane)
        return NULL;
    memset(plane, 0, sizeof(*plane));
    _last_uiplane_id++;
    plane->id = _last_uiplane_id;
    plane->width = 1600;
    plane->height = 900;

    h3duiplane **all_planes_new = realloc(
        all_planes, sizeof(*all_planes) * (all_planes_count + 1)
    );
    if (!all_planes_new) {
        free(plane);
        return NULL;
    }
    all_planes = all_planes_new;
    all_planes[all_planes_count] = plane;
    all_planes_count++;

    if (!uiplane_by_id_cache) {
        uiplane_by_id_cache = hash_NewBytesMap(64);
        if (!uiplane_by_id_cache) {
            all_planes[all_planes_count - 1] = NULL;
            free(plane);
            return NULL;
        }
    }

    uint64_t id = plane->id;
    if (!hash_BytesMapSet(
            uiplane_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)plane)) {
        all_planes[all_planes_count - 1] = NULL;
        all_planes_count--;
        free(plane);
        return NULL;
    }

    if (!uiplane_MarkAsUnparented(plane)) {
        all_planes[all_planes_count - 1] = NULL;
        all_planes_count--;
        hash_BytesMapUnset(
            uiplane_by_id_cache,
            (char*)&id, sizeof(id)
        );
        free(plane);
        return NULL;
    }

    return plane;
}

int uiplane_AddObject(h3duiplane *plane, h3duiobject *obj) {
    if (obj->parentplane == plane)
        return 1;
    h3duiobject **newobjects = realloc(
        plane->objects,
        sizeof(*plane->objects) * (plane->objects_count + 1)
    );
    if (!newobjects)
        return 0;
    plane->objects = newobjects;
    int k = 0;
    while (k < plane->objects_count) {
        if (plane->objects[k]->zindex > obj->zindex ||
                (fabs(plane->objects[k]->zindex - obj->zindex) < 0.0001 &&
                 plane->objects[k]->id > obj->id))
            break;
        k++;
    }
    const int insertslot = k;
    if (insertslot < plane->objects_count)
        memmove(
            &plane->objects[insertslot],
            &plane->objects[insertslot + 1],
            plane->objects_count - insertslot - 1
        );
    plane->objects[insertslot] = obj;
    plane->objects_count++;
    if (obj->parentplane)
        uiplane_RemoveObject(obj->parentplane, obj);
    obj->parentplane = plane;
    return 1;
}

void uiplane_UpdateObjectSort(h3duiplane *plane, h3duiobject *obj) {
    if (!obj || !plane || obj->parentplane != plane)
        return;
    int insertslot = -1;
    int oldslot = -1;
    int k = 0;
    while (k < plane->objects_count) {
        if (plane->objects[k] == obj) {
            oldslot = k;
        } else if (plane->objects[k]->zindex > obj->zindex ||
                (fabs(plane->objects[k]->zindex - obj->zindex) < 0.0001 &&
                 plane->objects[k]->id > obj->id)) {
            insertslot = k;
        }
        k++;
    }
    assert(oldslot >= 0);
    if (insertslot < 0)
        insertslot = plane->objects_count;
    if (oldslot != insertslot) {
        if (oldslot < plane->objects_count - 1) {
            memmove(
                &plane->objects[oldslot],
                &plane->objects[oldslot + 1],
                plane->objects_count - oldslot - 1
            );
        }
        plane->objects_count--;
        if (insertslot > oldslot)
            insertslot--;
        if (insertslot < plane->objects_count)
            memmove(
                &plane->objects[insertslot],
                &plane->objects[insertslot + 1],
                plane->objects_count - insertslot - 1
            );
        plane->objects[insertslot] = obj;
        plane->objects_count++;
    }
}

void uiplane_RemoveObject(h3duiplane *plane, h3duiobject *obj) {
    int i = 0;
    while (i < plane->objects_count) {
        if (plane->objects[i] == obj) {
            plane->objects[i] = NULL;
            if (i + 1 < plane->objects_count)
                memmove(
                    &plane->objects[i], &plane->objects[i + 1],
                    sizeof(*plane->objects) *
                    (plane->objects_count - i - 1)
                );
            plane->objects_count--;
            continue;
        }
        i++;
    }
    if (obj->parentplane == plane)
        obj->parentplane = NULL;
}

void uiplane_Destroy(h3duiplane *plane) {
    uint64_t queryid = plane->id;
    hash_BytesMapUnset(
        uiplane_by_id_cache,
        (char*)&queryid, sizeof(queryid)
    );
    while (plane->objects_count > 0)
        uiobject_DestroyObject(plane->objects[0]);
    if (plane->parentobject) {
        if (plane->parentobject->childrenplane == plane)
            plane->parentobject->childrenplane = NULL;
    }
    int i = 0;
    while (i < all_planes_count) {
        if (all_planes[i] == plane) {
            all_planes[i] = NULL;
            if (i + 1 < all_planes_count)
                memmove(
                    &all_planes[i], &all_planes[i + 1],
                    sizeof(*all_planes) *
                    (all_planes_count - i - 1)
                );
            all_planes_count--;
            continue;
        }
        i++;
    }
    i = 0;
    while (i < unparented_planes_count) {
        if (!unparented_planes[i])
            unparented_planes[i] = NULL;
        i++;
    }
}

int uiplane_Draw(
        h3duiplane *plane,
        double additionaloffsetx, double additionaloffsety
        ) {
    if (!plane)
        return 0;
    int result = 1;
    int k = 0;
    while (k < plane->objects_count) {
        if (!plane->objects[k]) {
            k++;
            continue;
        }
        if (!uiobject_Draw(
                plane->objects[k],
                plane->offset_x + additionaloffsetx,
                plane->offset_y + additionaloffsety
                ))
            result = 0;
        k++;
    }
    return result;
}

int uiplane_DrawScreenPlanes() {
    int result = 1;
    int i = 0;
    while (i < unparented_planes_count) {
        if (!unparented_planes[i] ||
                unparented_planes[i]->has3dplacement) {
            i++;
            continue;
        }
        if (!uiplane_Draw(unparented_planes[i], 0, 0))
            result = 0;
        i++;
    }
    return result;
}

void uiplane_ResizeScreenPlanes(int width, int height) {
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    int i = 0;
    while (i < unparented_planes_count) {
        if (!unparented_planes[i] ||
                unparented_planes[i]->has3dplacement) {
            i++;
            continue;
        }
        unparented_planes[i]->width = width;
        unparented_planes[i]->height = height;
        i++;
    }
}
