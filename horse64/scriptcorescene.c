
#include <cgltf/cgltf.h>
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <string.h>

#include "hash.h"
#include "lights.h"
#include "meshes.h"
#include "meshes_gltf.h"
#include "scriptcore.h"
#include "scriptcoreerror.h"
#include "scriptcorescene.h"
#include "scriptcoreworld.h"
#include "skeleton.h"
#include "vfs.h"
#include "world.h"


static int _scene_additemsfrommap_processelement(
        hashmap *STSitemsmap, double scene_scale, const char *tag,
        int scene_scale_changes_light_range,
        int scene_scale_changes_object_scale
        ) {
    if (scene_scale < 0.00001)
        scene_scale = 1;
    if (!STSitemsmap)
        return 1;
    const char *classname = (
        hash_STSMapGet(STSitemsmap, "classname")
    );
    const char *positionstr = (
        hash_STSMapGet(STSitemsmap, "origin")
    );
    double position[3];
    memset(position, 0, sizeof(position));
    if (positionstr && strlen(positionstr) > 0) {
        char no1[30] = "";
        char no2[30] = "";
        char no3[30] = "";
        const char *p = positionstr;
        while (*p == ' ')
            p++;
        if (*p == '-') {
            no1[1] = '\0';
            no1[0] = '-';
            p++;
        }
        while ((*p >= '0' && *p <= '9') || *p == '.') {
            if (strlen(no1) < sizeof(no1) - 1) {
                no1[strlen(no1) + 1] = '\0';
                no1[strlen(no1)] = *p;
            }
            p++;
        }
        while (*p == ' ')
            p++;
        if (*p == ',')
            p++;
        while (*p == ' ')
            p++;
        if (*p == '-') {
            no2[1] = '\0';
            no2[0] = '-';
            p++;
        }
        while ((*p >= '0' && *p <= '9') || *p == '.') {
            if (strlen(no2) < sizeof(no2) - 1) {
                no2[strlen(no2) + 1] = '\0';
                no2[strlen(no2)] = *p;
            }
            p++;
        }
        while (*p == ' ')
            p++;
        if (*p == ',')
            p++;
        while (*p == ' ')
            p++;
        if (*p == '-') {
            no3[1] = '\0';
            no3[0] = '-';
            p++;
        }
        while ((*p >= '0' && *p <= '9') || *p == '.') {
            if (strlen(no3) < sizeof(no3) - 1) {
                no3[strlen(no3) + 1] = '\0';
                no3[strlen(no3)] = *p;
            }
            p++;
        }
        position[0] = atof(no1) * scene_scale;
        position[1] = atof(no2) * scene_scale;
        position[2] = atof(no3) * scene_scale;
    }
    if (classname &&
            strcasecmp(classname, "light") == 0) {
        double r = 1.0;
        if (hash_STSMapGet(STSitemsmap, "lightred") &&
                strlen(hash_STSMapGet(STSitemsmap, "lightred")) > 0)
            r = atof(hash_STSMapGet(STSitemsmap, "lightred"));
        double g = 1.0;
        if (hash_STSMapGet(STSitemsmap, "lightgreen") &&
                strlen(hash_STSMapGet(STSitemsmap, "lightgreen")) > 0)
            g = atof(hash_STSMapGet(STSitemsmap, "lightgreen"));
        double b = 1.0;
        if (hash_STSMapGet(STSitemsmap, "lightblue") &&
                strlen(hash_STSMapGet(STSitemsmap, "lightblue")) > 0)
            b = atof(hash_STSMapGet(STSitemsmap, "lightblue"));
        double range = 50.0;
        if (hash_STSMapGet(STSitemsmap, "range") &&
                strlen(hash_STSMapGet(STSitemsmap, "range")) > 0)
            range = (
                atof(hash_STSMapGet(STSitemsmap, "range"))
            );
        int falloff = 0;
        if (hash_STSMapGet(STSitemsmap, "falloff") &&
                strlen(hash_STSMapGet(STSitemsmap, "falloff")) > 0)
            falloff = atoi(hash_STSMapGet(STSitemsmap, "falloff"));
        if (falloff < 0)
            falloff = 0;
        if (falloff > 2)
            falloff = 2;

        h3dlight *l = lights_New(position);
        if (!l)
            return 0;
        double lpos[3];
        lights_GetPosition(l, lpos);
        h3dlightsettings *lsettings = lights_GetLightSettings(l);
        lsettings->r = r;
        lsettings->g = g;
        lsettings->b = b;
        if (scene_scale_changes_light_range)
            range *= scene_scale;
        lsettings->range = range;
        lsettings->fallofftype = falloff;
        if (tag && strlen(tag) > 0) {
            if (!lights_AddLightTag(l, tag)) {
                lights_DestroyLight(l);
                return 0;
            }
        }
        return 1;
    }
    return 1;
}

static int _scene_additemsfromquakemap(lua_State *l) {
    // ARGS ARE:
    // #1 (string): .map file path
    // #2 (number): scene scale
    // #3 (string): tag to add
    // #4 (boolean): whether scene scale changes object scale
    // #5 (boolean): whether scene scale changes light ranges

    if (lua_gettop(l) < 3 ||
            lua_type(l, 1) != LUA_TSTRING ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TSTRING) {
        lua_pushstring(
            l, "expected args of types string, "
            "number, string"
        );
        return lua_error(l);
    }
    const char *tag = lua_tostring(l, 3);
    int scene_scale_changes_light_range = (lua_toboolean(l, 5) != 0);
    int scene_scale_changes_object_scale = (lua_toboolean(l, 4) != 0);
    double scene_scale = lua_tonumber(l, 2);

    VFSFILE *f = NULL;
    if (!(f = vfs_fopen(lua_tostring(l, 1), "rb"))) {
        lua_pushstring(l, "failed to open file");
        return lua_error(l);
    }

    hashmap *elementmap = hash_NewStringToStringMap(32);
    if (!elementmap) {
        vfs_fclose(f);
        lua_pushstring(l, "alloc fail");
        return lua_error(l);
    }

    int inside_element = 0;
    while (!vfs_feof(f)) {
        char linebuf[512] = "";
        size_t linelen = vfs_freadline(
            f, linebuf, sizeof(linebuf)
        );
        if (linelen == 0 && strlen(linebuf) == 0) {
            vfs_fclose(f);
            hash_FreeMap(elementmap);
            lua_pushstring(l, "error reading file (I/O error)");
            return lua_error(l);
        }
        while (strlen(linebuf) > 0 &&
                (linebuf[strlen(linebuf) - 1] == '\n' ||
                 linebuf[strlen(linebuf) - 1] == ' '))
            linebuf[strlen(linebuf) - 1] = '\0';
        while (linebuf[0] == ' ' || linebuf[0] == '\t')
            memmove(linebuf, linebuf + 1, strlen(linebuf));
        if (strlen(linebuf) == 0 ||
                (linebuf[0] == '/' && linebuf[1] == '/'))
            continue;
        if (strcmp(linebuf, "{") == 0 || strcmp(linebuf, "}") == 0) {
            if (strcmp(linebuf, "{") == 0)
                inside_element = 1;
            else
                inside_element = 0;
            if (!_scene_additemsfrommap_processelement(
                    elementmap, scene_scale, tag,
                    scene_scale_changes_light_range,
                    scene_scale_changes_object_scale
                    ))
                goto parseerror;
            hash_ClearMap(elementmap);
            continue;
        }
        if (linebuf[0] == '"' || (
                linebuf[0] >= 'a' && linebuf[0] <= 'z')) {
            int i = 1;
            while (i < strlen(linebuf) &&
                    ((linebuf[0] == '"' && (i <= 1 ||
                      linebuf[i - 1] != '"' || linebuf[i] != ' ')) ||
                     (linebuf[0] != '"' && linebuf[i] != ' ')))
                i++;

            if (linebuf[i] != ' ')
                continue;
            int key_start = (linebuf[0] == '"' ? 1 : 0);
            int key_end = i;
            if (key_end > 1 && linebuf[0] == '"' &&
                    linebuf[key_end - 1] == '"')
                key_end--;
            while (linebuf[i] == ' ')
                i++;
            int value_start = i;
            int value_end = strlen(linebuf);
            if (linebuf[value_start] == '"' &&
                    value_end > value_start + 1 &&
                    linebuf[value_end - 1] == '"') {
                value_start++;
                value_end--;
            }
            char valuebuf[512];
            char keybuf[512];
            memcpy(valuebuf, linebuf + value_start, value_end - value_start);
            valuebuf[value_end - value_start] = '\0';
            memcpy(keybuf, linebuf + key_start, key_end - key_start);
            keybuf[key_end - key_start] = '\0';
            if (!hash_STSMapSet(elementmap, keybuf, valuebuf)) {
                parseerror:
                if (f) vfs_fclose(f);
                if (elementmap) hash_FreeMap(elementmap);
                lua_pushstring(
                    l, "error processing file (out of memory?)"
                );
                return lua_error(l);
            }
        }
    }
    vfs_fclose(f);
    f = NULL;

    if (!_scene_additemsfrommap_processelement(
            elementmap, scene_scale, tag,
            scene_scale_changes_light_range,
            scene_scale_changes_object_scale
            ))
        goto parseerror;

    hash_FreeMap(elementmap);
    elementmap = NULL;

    return 0;
}

typedef struct gltfscene_addoptions {
    double scale_light_pos_by;
    double scale_light_range_by;
    int lightadd_failed;
    char *tag;
} gltfscene_addoptions;

static int scene_addgltflights(
        cgltf_node *node, void *userdata
        ) {
    gltfscene_addoptions *loptions = userdata;
    if (node->light) {
        h3dskeleton_bone *node_bone;
        h3dskeleton *sk = mesh_GetCGLTFNodeTransformSkeleton(
            node, &node_bone, NULL
        );
        if (!sk) {
            loptions->lightadd_failed = 1;
            return 0;
        }

        double pos[3];
        pos[0] = node->translation[0];
        pos[1] = -node->translation[2];
        pos[2] = node->translation[1];
        bone_TransformPosition(node_bone, pos, pos);
        pos[0] *= loptions->scale_light_pos_by;
        pos[1] *= loptions->scale_light_pos_by;
        pos[2] *= loptions->scale_light_pos_by;
        double range = (
            node->light->range * loptions->scale_light_range_by
        );
        double r = fmax(0, fmin(1.0, node->light->color[0]));
        double g = fmax(0, fmin(1.0, node->light->color[1]));
        double b = fmax(0, fmin(1.0, node->light->color[2]));
        skeleton_Destroy(sk);
        sk = NULL;

        h3dlight *l = lights_New(pos);
        if (!l) {
            loptions->lightadd_failed = 1;
            return 0;
        }
        h3dlightsettings *settings = lights_GetLightSettings(l);
        settings->r = r;
        settings->g = g;
        settings->b = b;
        if (range <= 0)
            range = (node->light->intensity / 200.0);
        settings->range = range;
        settings->fallofftype = FALLOFF_INVERSEEXP;
        if (loptions->tag && strlen(loptions->tag) > 0) {
            if (!lights_AddLightTag(l, loptions->tag)) {
                lights_DestroyLight(l);
                loptions->lightadd_failed = 1;
                return 0;
            }
        }
    }
    return 1;
}

int _scene_additemsandgeometryfromgltfscene(lua_State *l) {
    // ARGS ARE:
    // #1 (string): scene file path
    // #2 (number): scene scale
    // #3 (string): tag to add
    // #4 (table): invis args
    // #5 (table): nocol args
    // #6 (boolean): whether to scale objects with scene scale
    // #7 (boolean): whether to scale light ranges with scene scale

    if (lua_gettop(l) < 5 ||
            lua_type(l, 1) != LUA_TSTRING ||
            lua_type(l, 2) != LUA_TNUMBER ||
            lua_type(l, 3) != LUA_TSTRING ||
            lua_type(l, 4) != LUA_TTABLE ||
            lua_type(l, 5) != LUA_TTABLE) {
        lua_pushstring(
            l, "expected args of types string, "
            "number, string, table, table"
        );
        return lua_error(l);
    }
    const char *path = lua_tostring(l, 1);
    const char *tag = lua_tostring(l, 3);
    int scene_scale_changes_light_range = (lua_toboolean(l, 7) != 0);
    int scene_scale_changes_object_scale = (
        lua_toboolean(l, 6) != 0
    );
    double scene_scale = lua_tonumber(l, 2);
    if (scene_scale <= 0) {
        lua_pushstring(l, "scene scale must be larger than zero");
        return lua_error(l);
    }

    h3dmeshloadinfo *loadinfo = malloc(sizeof(*loadinfo));
    if (!loadinfo) {
        lua_pushstring(l, "alloc fail");
        return lua_error(l);
    }
    memset(loadinfo, 0, sizeof(*loadinfo));
    if (!parse_invis_and_nocol_keywords(
            l, loadinfo, 4, 5
            )) {
        meshloadinfo_Free(loadinfo);
        lua_pushstring(l, "alloc fail");
        return lua_error(l);
    }

    char buf[2048];
    char *error = NULL;

    int exists_result = 0;
    if (!vfs_Exists(path, &exists_result) || !exists_result) {
        meshloadinfo_Free(loadinfo);
        lua_pushstring(l, "no such file or I/O error");
        return lua_error(l);
    }

    // Parse basic GLTF data:
    cgltf_options gltf_options = {};
    cgltf_data *gltf_data = NULL;
    if (!_cgltf_loaddata(&gltf_options, path, &gltf_data, &error)) {
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to load GLTF scene file: %s", error
        );
        meshloadinfo_Free(loadinfo);
        lua_pushstring(l, buf);
        return lua_error(l);
    }

    // Add object for scene with its geometry:
    h3dmesh *m = mesh_LoadFromGLTFData(
        gltf_data, path, 1, loadinfo, &error
    );
    meshloadinfo_Free(loadinfo);
    loadinfo = NULL;
    if (!m) {
        if (gltf_data)
            cgltf_free(gltf_data);
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to load GLTF scene geometry: %s", error
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    mesh_PrintLoadSummary(m);
    h3dobject *o = world_AddMeshObject(m);
    mesh_DestroyMesh(m);  // NOTE: LoadFromGLTFData doesn't populate cache.
    m = NULL;
    if (!o) {
        if (gltf_data)
            cgltf_free(gltf_data);
        lua_pushstring(l, "failed to create scene geometry object");
        return lua_error(l);
    }

    // Add in the lights of the scene:
    gltfscene_addoptions loptions;
    memset(&loptions, 0, sizeof(loptions));
    loptions.scale_light_pos_by = scene_scale;
    loptions.scale_light_range_by = 1.0;
    if (tag) {
        loptions.tag = strdup(tag);
        if (!loptions.tag) {
            if (gltf_data)
                cgltf_free(gltf_data);
            lua_pushstring(l, "tag alloc failed, out of memory?");
            return lua_error(l);
        }
    }
    if (scene_scale_changes_light_range)
        loptions.scale_light_range_by = scene_scale;
    int result = _mesh_IterateCGLTFNodes(
        gltf_data, &scene_addgltflights, &loptions
    );
    if (loptions.tag)
        free(loptions.tag);
    if (gltf_data) {
        cgltf_free(gltf_data);
        gltf_data = NULL;
    }
    if (!result) {
        lua_pushstring(
            l, "unexpected node iteration fail - out of memory?"
        );
        return lua_error(l);
    }
    if (loptions.lightadd_failed) {
        lua_pushstring(l, "failed to add some lights, out of memory?");
        return lua_error(l);
    }

    return 0;
}

void scriptcorescene_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _scene_additemsfromquakemap);
    lua_setglobal(l, "_scene_additemsfromquakemap");
    lua_pushcfunction(l, _scene_additemsandgeometryfromgltfscene);
    lua_setglobal(l, "_scene_additemsandgeometryfromgltfscene");
}
