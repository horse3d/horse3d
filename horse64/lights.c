
#include <assert.h>
#include <mathc/mathc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "datetime.h"
#include "hash.h"
#include "lights.h"

typedef struct h3dlight {
    int id;
    double pos[3];
    int globalactive, wasglobalactive, fadingout;
    double fadeinprogress, fadeoutprogress;

    int tag_count;
    char **tags;
    h3dlightsettings settings;
} h3dlight;

static int _last_light_id = 0;
int lights_count = 0;
int lights_allocated = 0;
h3dlight **global_lights = NULL;
static hashmap *light_by_id_cache = NULL;


int lights_GetLightId(h3dlight *l) {
    return l->id;
}

void lights_GetPosition(h3dlight *l, double position[3]) {
    if (!l) {
        memset(position, 0, sizeof(*position) * 3);
        return;
    }
    memcpy(
        position, l->pos, sizeof(*position) * 3
    );
}

h3dlightsettings *lights_GetLightSettings(h3dlight *l) {
    if (!l)
        return NULL;
    return &(l->settings);
}

int lights_AddLightTag(h3dlight *l, const char *tag) {
    if (!l || !tag || strlen(tag) == 0)
        return 0;
    int i = 0;
    while (i < l->tag_count) {
        if (strcmp(l->tags[i], tag) == 0)
            return 1;
        i++;
    }
    char *new_tag = strdup(tag);
    if (!new_tag)
        return 0;
    char **new_tags = realloc(
        l->tags, sizeof(*l->tags) * (l->tag_count + 1)
    );
    if (!new_tags) {
        free(new_tag);
        return 0;
    }
    l->tags = new_tags;
    l->tags[l->tag_count] = new_tag;
    l->tag_count++;
    return 1;
}

h3dlight *lights_New(
        const double position[3]
        ) {
    h3dlight *l = malloc(sizeof(*l));
    if (!l)
        return NULL;
    memset(l, 0, sizeof(*l));

    int foundslot = -1;
    int i = 0;
    while (i < lights_allocated) {
        if (!global_lights[i]) {
            foundslot = i;
            break;
        }
        i++;
    }
    if (foundslot < 0) {
        int old_allocated = lights_allocated;
        int new_allocated = lights_allocated * 2;
        if (new_allocated < 8)
            new_allocated = 8;
        h3dlight **global_lights_new = realloc(
            global_lights, sizeof(*global_lights) * new_allocated
        );
        if (!global_lights_new) {
            free(l);
            return NULL;
        }
        global_lights = global_lights_new;
        lights_allocated = new_allocated;
        {
            int j = old_allocated;
            while (j < new_allocated) {
                global_lights[j] = NULL;
                j++;
            }
        }
        foundslot = old_allocated;
        assert(foundslot < new_allocated);
    }

    if (!light_by_id_cache) {
        light_by_id_cache = hash_NewBytesMap(32);
        if (!light_by_id_cache) {
            free(l);
            return NULL;
        }
    }

    _last_light_id++;
    l->id = _last_light_id;
    uint64_t id = l->id;
    if (!hash_BytesMapSet(
            light_by_id_cache,
            (char*)&id, sizeof(id),
            (uint64_t)(uintptr_t)l)) {
        free(l);
        return NULL;
    }
    assert(foundslot >= 0 && foundslot < lights_allocated);
    global_lights[foundslot] = l;
    if (position)
        memcpy(l->pos, position, sizeof(*position) * 3);
    l->settings.r = 1;
    l->settings.g = 0.9;
    l->settings.b = 0.8;
    l->settings.range = 5.0;
    lights_count++;
    return l;
}

static int lidx(h3dlight *l) {
    int i = 0;
    while (i < lights_allocated) {
        if (l && global_lights[i] == l)
            return i;
        i++;
    }
    return -1;
}

static uint64_t lastfadeupdatets = 0;

void lights_UpdateFade() {
    uint64_t now = datetime_Ticks();
    if (lastfadeupdatets + 300 < now)
        lastfadeupdatets = now;
    while (lastfadeupdatets + 20 < now) {
        int i = 0;
        while (i < lights_allocated) {
            if (!global_lights[i] || !global_lights[i]->globalactive) {
                i++;
                continue;
            }
            global_lights[i]->fadeinprogress = fmin(
                1.0,
                global_lights[i]->fadeinprogress + 0.05
            );
            if (global_lights[i]->fadingout)
                global_lights[i]->fadeoutprogress = fmin(
                    1.0,
                    global_lights[i]->fadeoutprogress + 0.05
                );
            i++;
        }
        lastfadeupdatets += 20;
    }
}

h3dlight **lights_EnableNthActiveLights(
        const double position[3], int count
        ) {
    h3dlight **result = malloc(
        sizeof(*result) * count
    );
    if (!result)
        return NULL;
    memset(result, 0, sizeof(*result) * count);

    double rangef = 0.5;
    int resultc = 0;
    int i = 0;
    while (i < lights_allocated) {
        if (!global_lights[i]) {
            i++;
            continue;
        }
        global_lights[i]->globalactive = 0;

        int addtoindex = -1;
        double relevantdistnew = vec3_distance(
            (double *)position, global_lights[i]->pos
        ) - global_lights[i]->settings.range * rangef;
        int k = resultc;
        while (k >= 0) {
            double relevantdist = 0;
            if (k < resultc && result[k])
                relevantdist = vec3_distance(
                    (double *)position, result[k]->pos
                ) - result[k]->settings.range * rangef;
            if (relevantdist > relevantdistnew || k == resultc ||
                    result[k] == global_lights[i])
                addtoindex = k;
            k--;
        }

        if (addtoindex >= 0 && addtoindex < count &&
                result[addtoindex] != global_lights[i]) {
            if (addtoindex + 1 < count) {
                if (resultc >= count && result[count - 1] &&
                        result[count - 1]->globalactive)
                    result[count - 1]->globalactive = 0;
                memmove(
                    &result[addtoindex + 1],
                    &result[addtoindex],
                    (count - addtoindex - 1) * sizeof(*result)
                );
                resultc++;
                if (resultc > count)
                    resultc = count;
            } else if (result[addtoindex] &&
                    result[addtoindex]->globalactive) {
                result[addtoindex]->globalactive = 0;
            } else if (resultc < addtoindex + 1) {
                resultc++;
            }
            result[addtoindex] = global_lights[i];
            if (!result[addtoindex]->globalactive) {
                result[addtoindex]->globalactive = 1;
            }
        }
        i++;
    }
    i = 0;
    while (i < resultc) {
        if (!result[i]) {
            i++;
            continue;
        }
        assert(result[i]->globalactive);
        if (i >= MAXACTIVELIGHTS && i < MAXLIGHTS) {
            if (!result[i]->fadingout) {
                //printf("--> fade out %d\n", lidx(result[i]));
                result[i]->fadingout = 1;
                result[i]->fadeoutprogress = 0.0;
            }
            if (result[i]->fadeoutprogress >= 0.99)
                result[i]->fadeinprogress = 0.0;
        } else {
            result[i]->fadingout = 0;
            result[i]->fadeoutprogress = 0.0;
        }
        i++;
    }
    i = 0;
    while (i < lights_allocated) {
        if (!global_lights[i]) {
            i++;
            continue;
        }
        if (global_lights[i]->globalactive &&
                !global_lights[i]->wasglobalactive
                ) {
            global_lights[i]->fadeinprogress = 0.0;
            //printf("--> fade in %d\n", i);
        }
        global_lights[i]->wasglobalactive = (
            global_lights[i]->globalactive
        );
        if (!global_lights[i]->globalactive)
            global_lights[i]->fadingout = 0;
        i++;
    }
    return result;
}

void lights_MoveLight(h3dlight *l, const double position[3]) {
    memcpy(l->pos, position, sizeof(*position) * 3);
}

void lights_DestroyLight(h3dlight *l) {
    int i = 0;
    while (i < lights_allocated) {
        if (global_lights[i] == l) {
            global_lights[i] = NULL;
            lights_count--;
        }
        i++;
    }
    uint64_t queryid = l->id;
    int result = hash_BytesMapUnset(
        light_by_id_cache,
        (char*)&queryid, sizeof(queryid)
    );
    assert(result != 0);
    if (l->tag_count > 0) {
        i = 0;
        while (i < l->tag_count) {
            free(l->tags[i]);
            i++;
        }
        free(l->tags);
    }
    free(l);
}

h3dlight *lights_GetLightById(int64_t id) {
    uint64_t queryid = id;
    uintptr_t hashptrval = 0;
    if (!hash_BytesMapGet(
            light_by_id_cache,
            (char*)&queryid, sizeof(queryid),
            (uint64_t*)&hashptrval))
        return NULL;
    return (h3dlight*)(void*)hashptrval;
}

double lights_GetFadeFactor(h3dlight *l) {
    return (
        l->fadeinprogress * (1.0 - l->fadeoutprogress)
    );
}
