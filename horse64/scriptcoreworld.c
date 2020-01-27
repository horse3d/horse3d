
#include <assert.h>
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <stdint.h>
#include <string.h>

#include "collision/charactercontroller.h"
#include "filesys.h"
#include "meshes.h"
#include "render3d.h"
#include "scriptcore.h"
#include "scriptcoreaudio.h"
#include "scriptcoreevents.h"
#include "scriptcoreerror.h"
#include "scriptcoremath.h"
#include "texture.h"
#include "world.h"


void lua_PushObjectRef(lua_State *l, int64_t objid) {
    //lua_checkstack(l, LUA_MINSTACK);
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    if (!ref) {
        lua_pushnil(l);
        return;
    }
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_OBJ;
    ref->value = objid;
    luaL_newmetatable(l, "horse3d.world.object");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_world_object_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
}

static int _world_set_drawcollisionboxes(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TBOOLEAN) {
        lua_pushstring(l, "expected arg of type boolean");
        return lua_error(l);
    }

    world_SetDrawCollisionBoxes(
        lua_toboolean(l, 1)
    );
    return 0;
}

static int _world_obj_destroy(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }

    world_DestroyObject(obj);
    return 0;
}

static int _world_object_tag(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TSTRING) {
        lua_pushstring(
            l, "expected args of type "
            "horse3d.world.object, string"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    const char *tag = lua_tostring(l, 2);
    if (!tag || strlen(tag) == 0)
        return 0;

    if (!world_AddObjectTag(obj, tag)) {
        lua_pushstring(l, "failed to tag object - out of memory?");
        return lua_error(l);
    }
    return 0;
}

static int _world_obj_setrotation(lua_State *l) {
    if (lua_gettop(l) != 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected two args of types "
                       "horse3d.world.object, table");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    world_EnableObjectDynamics(obj);

    double quat[4];
    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    quat[0] = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    quat[1] = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 2);
    quat[2] = lua_tonumber(l, -1);
    lua_pushnumber(l, 4);
    lua_gettable(l, 2);
    quat[3] = lua_tonumber(l, -1);

    world_SetObjectRotation(obj, quat);
    return 0;
}

static int _world_obj_applyforce(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE ||
            lua_rawlen(l, 2) < 3) {
        lua_pushstring(
            l, "expected two args of type "
            "horse3d.world.object, table"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    double fx, fy, fz;
    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    fx = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    fy = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 2);
    fz = lua_tonumber(l, -1);
    world_ApplyObjectForce(
        obj, fx, fy, fz
    );
    return 0;
}

static int _world_obj_getcharscalebasedforcefactors(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    if (obj->collisiontype != OBJCOLTYPE_CHARACTER) {
        lua_pushstring(
            l, "not a character controller"
        );
        return lua_error(l);
    }
    if (!obj->colcharacter) {
        lua_pushnumber(l, 1);
        return 1;
    }
    lua_pushnumber(
        l, charactercontroller_ScaleBasedForcesFactor(obj->colcharacter)
    );
    return 1;
}

static int _world_obj_disablecollision(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TBOOLEAN) {
        lua_pushstring(l, "expected args of types "
                       "horse3d.world.object, boolean");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    world_DisableObjectCollision(obj, lua_toboolean(l, 2));
    return 0;
}

static int _world_obj_getcharisonfloor(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    if (obj->collisiontype != OBJCOLTYPE_CHARACTER) {
        lua_pushstring(
            l, "not a character controller"
        );
        return lua_error(l);
    }
    if (!obj->colcharacter) {
        lua_pushboolean(l, 1);
        return 1;
    }
    lua_pushboolean(
        l, charactercontroller_GetOnFloor(obj->colcharacter)
    );
    return 1;
}

static int _world_obj_getcharisonslope(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }
    if (obj->collisiontype != OBJCOLTYPE_CHARACTER) {
        lua_pushstring(
            l, "not a character controller"
        );
        return lua_error(l);
    }
    if (!obj->colcharacter) {
        lua_pushboolean(l, 0);
        return 1;
    }
    lua_pushboolean(
        l, charactercontroller_GetOnSlope(obj->colcharacter)
    );
    return 1;
}

static int _world_obj_getrotation(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }

    double quat[4];
    memset(quat, 0, sizeof(*quat));
    world_GetObjectRotation(obj, quat);
    if (quat[0] == 0 && quat[1] == 0 && quat[2] == 0 && quat[3] == 0) {
        lua_pushstring(l, "internal error: object has invalid rotation");
        return lua_error(l);
    }
    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, quat[0]);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, quat[1]);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, quat[2]);
    lua_settable(l, -3);
    lua_pushnumber(l, 4);
    lua_pushnumber(l, quat[3]);
    lua_settable(l, -3);
    return 1;
}


static int _world_obj_setposition(lua_State *l) {
    if (lua_gettop(l) != 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TTABLE) {
        lua_pushstring(l, "expected two args of types "
                       "horse3d.world.object, table");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }

    lua_pushnumber(l, 1);
    lua_gettable(l, 2);
    double x = lua_tonumber(l, -1);
    lua_pushnumber(l, 2);
    lua_gettable(l, 2);
    double y = lua_tonumber(l, -1);
    lua_pushnumber(l, 3);
    lua_gettable(l, 2);
    double z = lua_tonumber(l, -1);

    world_SetObjectPosition(obj, x, y, z);
    return 0;
}

static int _world_obj_getposition(lua_State *l) {
    if (lua_gettop(l) != 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        lua_pushstring(
            l, "failed to get object - was it deleted?"
        );
        return lua_error(l);
    }

    double x, y, z;
    world_GetObjectPosition(obj, &x, &y, &z);
    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, x);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, y);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, z);
    lua_settable(l, -3);
    return 1;
}

static int _world_obj_enabledynamics(lua_State *l) {
    if (lua_gettop(l) != 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TSTRING) {
        lua_pushstring(l, "expected two args of type "
                       "horse3d.world.object, string");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    world_EnableObjectDynamics(obj);
    if (lua_type(l, 2) == LUA_TSTRING &&
            strcasecmp(lua_tostring(l, 2), "box") == 0) {
        world_SetObjectCollisionType(obj, OBJCOLTYPE_BOX);
    } else if (lua_type(l, 2) == LUA_TSTRING &&
            strcasecmp(lua_tostring(l, 2), "character") == 0) {
        world_SetObjectCollisionType(obj, OBJCOLTYPE_CHARACTER);
    } else {
        lua_pushstring(l, "unsupported collision type");
        return lua_error(l);
    }
    return 0;
}

static int _world_objectisnomeshtype(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushboolean(l, obj->type == OBJTYPE_INVISIBLENOMESH);
    return 1;
}

static int _world_objectgetdimensions(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }

    double sx = 0;
    double sy = 0;
    double sz = 0;
    world_GetObjectDimensions(
        obj, &sx, &sy, &sz
    );

    lua_newtable(l);
    lua_pushnumber(l, 1);
    lua_pushnumber(l, sx);
    lua_settable(l, -3);
    lua_pushnumber(l, 2);
    lua_pushnumber(l, sy);
    lua_settable(l, -3);
    lua_pushnumber(l, 3);
    lua_pushnumber(l, sz);
    lua_settable(l, -3);
    return 1;
}

static int _world_objecthas3dmesh(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushboolean(l, obj->type == OBJTYPE_MESH);
    return 1;
}

static int _world_object_getid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushinteger(l, obj->id);
    return 1;
}

static int _world_object_byid(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TNUMBER) {
        lua_pushstring(l, "expected arg of type number");
        return lua_error(l);
    }
    int64_t id = (int64_t)lua_tointeger(l, 1);
    h3dobject *obj = world_GetObjectById(id);
    if (!obj) {
        lua_pushnil(l);
        return 1;
    }
    lua_PushObjectRef(l, id);
    return 1;
}

static int _world_object3dmeshhasanimation(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushboolean(l,
        (obj->type == OBJTYPE_MESH &&
         obj->mesh && obj->mesh->skeleton &&
         obj->mesh->skeleton->bone_count > 0)
    );
    return 1;
}

void world_RunObjectLuaOnUpdate(
        lua_State *l, h3dobject *obj, double dt
        ) {
    assert(obj != NULL);
    int prevstack = lua_gettop(l);
    char buf[64];
    snprintf(buf, sizeof(buf) - 1, "obj_%" PRId64 "on_update",
             (int64_t)obj->id);
    lua_pushstring(l, buf);
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
        if (lua_gettop(l) > prevstack)
            lua_settop(l, prevstack);
        return;
    }

    lua_pushtracebackfunc(l);
    lua_insert(l, -2);

    lua_PushObjectRef(l, obj->id);
    lua_pushnumber(l, dt);

    int result = lua_pcall(
        l, 2, 0, -4
    );
    if (result != 0) {
        fprintf(stderr, "horse3d: error: SCRIPT ERROR: %s\n",
                lua_tostring(l, -1));
    }
    if (lua_gettop(l) > prevstack)
        lua_settop(l, prevstack);
}

static int _world_obj_setscale(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "expected two args of type "
                       "horse3d.world.object, number");
        return lua_error(l);
    }
    if (lua_tonumber(l, 2) <= 0) {
        lua_pushstring(l, "scale must be positive number");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    world_SetObjectScale(obj, lua_tonumber(l, 2));
    return 0;
}

static int _world_obj_getscale(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushnumber(l, obj->scale);
    return 1;
}

static int _world_obj_setmass(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(l, "expected two args of type "
                       "horse3d.world.object, number");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    world_SetObjectMass(obj, lua_tonumber(l, 2));
    return 0;
}

static int _world_obj_getmass(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg of type "
                       "horse3d.world.object");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_OBJ) {
        lua_pushstring(l, "expected arg #1 to be horse3d.world.object");
        return lua_error(l);
    }
    h3dobject *obj = world_GetObjectById(
        ((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!obj) {
        char buf[256];
        snprintf(
            buf, sizeof(buf) - 1,
            "failed to get object id=%d - was it deleted?",
            (int)((scriptobjref*)lua_touserdata(l, 1))->value
        );
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    lua_pushnumber(l, world_GetObjectMass(obj));
    return 1;
}

static int _world_createinvisobject(lua_State *l) {
    if (lua_gettop(l) < 4 ||
            lua_type(l, 1) != LUA_TNUMBER ||
            lua_type(l, 2) != LUA_TNUMBER ||
            (lua_type(l, 3) != LUA_TFUNCTION &&
             lua_type(l, 3) != LUA_TNIL) ||
            lua_type(l, 4) != LUA_TSTRING) {
        lua_pushstring(
            l, "expected three args of types "
            "number, number, func/nil, string"
        );
        return lua_error(l);
    }

    const char *tag = lua_tostring(l, 4);
    double scale_w = lua_tonumber(l, 1);
    double scale_h = lua_tonumber(l, 2);
    if (scale_w <= 0.001 || scale_h <= 0.001) {
        lua_pushstring(l, "object size cannot be null or negative");
        return lua_error(l);
    }

    h3dobject *o = world_AddInvisibleObject(scale_w, scale_h);
    if (!o) {
        lua_pushstring(l, "failed to create object");
        return lua_error(l);
    }
    if (tag && strlen(tag) > 0) {
        if (!world_AddObjectTag(o, tag)) {
            world_DestroyObject(o);
            lua_pushstring(l, "failed to create object");
            return lua_error(l);
        }
    }

    if (lua_gettop(l) >= 3 && lua_type(l, 3) == LUA_TFUNCTION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1, "obj_%" PRId64 "on_update",
                 (int64_t)o->id);
        lua_pushstring(l, buf);
        lua_pushvalue(l, 3);
        lua_settable(l, LUA_REGISTRYINDEX);
    }
    lua_PushObjectRef(l, o->id);
    return 1;
}

int parse_invis_and_nocol_keywords(
        lua_State *l, h3dmeshloadinfo *loadinfo,
        int invisindex, int nocolindex
        ) {
    assert(loadinfo != NULL);
    assert(invisindex >= 1);
    assert(nocolindex >= 1);
    if (lua_gettop(l) >= invisindex &&
            lua_type(l, invisindex) == LUA_TTABLE) {
        int count = lua_rawlen(l, invisindex);
        int i = 0;
        while (i < count) {
            lua_pushnumber(l, i + 1);
            lua_gettable(l, invisindex);
            const char *p = lua_tostring(l, -1);
            if (lua_type(l, -1) != LUA_TSTRING || !p) {
                lua_pop(l, 1);
                i++;
                continue;
            }
            char **new_invisible_keywords = realloc(
                loadinfo->invisible_keywords,
                sizeof(*loadinfo->invisible_keywords) *
                (loadinfo->invisible_keywords_count + 1)
            );
            if (!new_invisible_keywords) {
                lua_pop(l, 1);
                return 0;
            }
            loadinfo->invisible_keywords = new_invisible_keywords;
            loadinfo->invisible_keywords[
                loadinfo->invisible_keywords_count
                ] = strdup(p);
            lua_pop(l, 1);
            if (!loadinfo->invisible_keywords[
                    loadinfo->invisible_keywords_count
                    ])
                return 0;
            loadinfo->invisible_keywords_count++;
            i++;
        }
    }
    if (lua_gettop(l) >= nocolindex &&
            lua_type(l, nocolindex) == LUA_TTABLE) {
        int count = lua_rawlen(l, nocolindex);
        int i = 0;
        while (i < count) {
            lua_pushnumber(l, i + 1);
            lua_gettable(l, nocolindex);
            const char *p = lua_tostring(l, -1);
            if (lua_type(l, -1) != LUA_TSTRING || !p) {
                lua_pop(l, 1);
                i++;
                continue;
            }
            char **new_nocollision_keywords = realloc(
                loadinfo->nocollision_keywords,
                sizeof(*loadinfo->nocollision_keywords) *
                (loadinfo->nocollision_keywords_count + 1)
            );
            if (!new_nocollision_keywords) {
                lua_pop(l, 1);
                return 0;
            }
            loadinfo->nocollision_keywords = new_nocollision_keywords;
            loadinfo->nocollision_keywords[
                loadinfo->nocollision_keywords_count
                ] = strdup(p);
            lua_pop(l, 1);
            if (!loadinfo->nocollision_keywords[
                    loadinfo->nocollision_keywords_count
                    ])
                return 0;
            loadinfo->nocollision_keywords_count++;
            i++;
        }
    }
    return 1;
}

static int _world_createmeshobject(lua_State *l) {
    // ARGS ARE:
    // #1 (string): mesh file path
    // #2 (table): invis args
    // #3 (table): nocol args
    // #4 (func): on_update func for object
    // #5 (string): tag to add

    char *errormsg = NULL;

    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TSTRING) {
        lua_pushstring(l, "expected arg #1 to be string");
        return lua_error(l);
    }
    const char *loadpathforerror = lua_tostring(l, 1);

    h3dmeshloadinfo *loadinfo = malloc(sizeof(*loadinfo));
    if (!loadinfo)
        goto loadfail;
    memset(loadinfo, 0, sizeof(*loadinfo));

    if (!parse_invis_and_nocol_keywords(
            l, loadinfo, 2, 3
            ))
        goto loadfail;
    const char *tag = NULL;
    if (lua_gettop(l) >= 5 && lua_type(l, 5) == LUA_TSTRING) {
        tag = lua_tostring(l, 5);
    }

    h3dmesh *m = mesh_GetFromFile(
        lua_tostring(l, 1), loadinfo, &errormsg
    );
    if (m && errormsg) {
        free(errormsg);
        errormsg = NULL;
    }
    meshloadinfo_Free(loadinfo);
    loadinfo = NULL;

    if (!m) {
        loadfail:
        if (loadinfo)
            meshloadinfo_Free(loadinfo);
        char buf[512];
        snprintf(buf, sizeof(buf) - 1, "failed to load mesh (%s): %s",
                 loadpathforerror, (errormsg ? errormsg : "<null error>"));
        lua_pushstring(l, buf);
        return lua_error(l);
    }

    h3dobject *o = world_AddMeshObject(m);
    if (!o) {
        lua_pushstring(l, "failed to create object");
        return lua_error(l);
    }
    if (tag && strlen(tag) > 0) {
        if (!world_AddObjectTag(o, tag)) {
            world_DestroyObject(o);
            lua_pushstring(l, "failed to create object");
            return lua_error(l);
        }
    }

    if (lua_gettop(l) >= 4 && lua_type(l, 4) == LUA_TFUNCTION) {
        char buf[64];
        snprintf(buf, sizeof(buf) - 1, "obj_%" PRId64 "on_update",
                 (int64_t)o->id);
        lua_pushstring(l, buf);
        lua_pushvalue(l, 4);
        lua_settable(l, LUA_REGISTRYINDEX);
    }
    lua_PushObjectRef(l, o->id);
    if (errormsg)
        free(errormsg);
    return 1;
}

static int _world_draw(lua_State *l) {
    if (!world_Render()) {
        if (!render3d_GetOutputWindow()) {
            lua_pushstring(
                l, "world draw FAILURE, no window. "
                "out of memory, or outdated OpenGL driver?"
            );
            return lua_error(l);
        } else if (!render3d_HaveGLOutput()) {
            lua_pushstring(
                l, "world draw FAILURE, no OpenGL context. "
                "outdated driver?"
            );
            return lua_error(l);
        }
    }
    return 0;
}

static int _world_update(lua_State *l) {
    world_Update();
    return 0;
}

struct _world_getall_iterinfo {
    lua_State *l;
    int returnedno, tableindex;
};

static void _world_getallobjs_iter(h3dobject *obj, void *userdata) {
    struct _world_getall_iterinfo *iinfo =
        (struct _world_getall_iterinfo *)userdata;
    lua_pushnumber(iinfo->l, iinfo->returnedno + 1);
    lua_PushObjectRef(iinfo->l, obj->id);
    lua_settable(iinfo->l, iinfo->tableindex);
    iinfo->returnedno++;
}

static int _world_getallobjs(lua_State *l) {
    const char *tag = NULL;
    if (lua_gettop(l) >= 1 && lua_type(l, 1) == LUA_TSTRING)
        tag = lua_tostring(l, 1);

    lua_newtable(l);
    struct _world_getall_iterinfo iinfo;
    memset(&iinfo, 0, sizeof(iinfo));
    iinfo.l = l;
    iinfo.tableindex = lua_gettop(l);
    world_IterateObjects(
        tag, _world_getallobjs_iter, &iinfo
    );
    return 1;
}

void scriptcoreworld_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _world_draw);
    lua_setglobal(l, "_world_draw");
    lua_pushcfunction(l, _world_update);
    lua_setglobal(l, "_world_update");
    lua_pushcfunction(l, _world_createmeshobject);
    lua_setglobal(l, "_world_createmeshobject");
    lua_pushcfunction(l, _world_createinvisobject);
    lua_setglobal(l, "_world_createinvisobject");

    lua_pushcfunction(l, _world_obj_setposition);
    lua_setglobal(l, "_world_obj_setposition");
    lua_pushcfunction(l, _world_obj_getposition);
    lua_setglobal(l, "_world_obj_getposition");
    lua_pushcfunction(l, _world_obj_getrotation);
    lua_setglobal(l, "_world_obj_getrotation");
    lua_pushcfunction(l, _world_obj_setrotation);
    lua_setglobal(l, "_world_obj_setrotation");
    lua_pushcfunction(l, _world_obj_setscale);
    lua_setglobal(l, "_world_obj_setscale");
    lua_pushcfunction(l, _world_obj_getscale);
    lua_setglobal(l, "_world_obj_getscale");
    lua_pushcfunction(l, _world_set_drawcollisionboxes);
    lua_setglobal(l, "_world_set_drawcollisionboxes");
    lua_pushcfunction(l, _world_objectisnomeshtype);
    lua_setglobal(l, "_world_objectisnomeshtype");
    lua_pushcfunction(l, _world_objecthas3dmesh);
    lua_setglobal(l, "_world_objecthas3dmesh");
    lua_pushcfunction(l, _world_object3dmeshhasanimation);
    lua_setglobal(l, "_world_object3dmeshhasanimation");
    lua_pushcfunction(l, _world_object_byid);
    lua_setglobal(l, "_world_object_byid");
    lua_pushcfunction(l, _world_object_getid);
    lua_setglobal(l, "_world_object_getid");
    lua_pushcfunction(l, _world_objectgetdimensions);
    lua_setglobal(l, "_world_objectgetdimensions");
    lua_pushcfunction(l, _world_obj_disablecollision);
    lua_setglobal(l, "_world_obj_disablecollision");
    lua_pushcfunction(l, _world_obj_applyforce);
    lua_setglobal(l, "_world_obj_applyforce");
    lua_pushcfunction(l, _world_obj_getcharisonfloor);
    lua_setglobal(l, "_world_obj_getcharisonfloor");
    lua_pushcfunction(l, _world_obj_getcharisonslope);
    lua_setglobal(l, "_world_obj_getcharisonslope");
    lua_pushcfunction(l, _world_obj_setmass);
    lua_setglobal(l, "_world_obj_setmass");
    lua_pushcfunction(l, _world_obj_getmass);
    lua_setglobal(l, "_world_obj_getmass");
    lua_pushcfunction(l, _world_obj_destroy);
    lua_setglobal(l, "_world_obj_destroy");
    lua_pushcfunction(l, _world_object_tag);
    lua_setglobal(l, "_world_object_tag");
    lua_pushcfunction(l, _world_getallobjs);
    lua_setglobal(l, "_world_getallobjs");
    lua_pushcfunction(l, _world_obj_getcharscalebasedforcefactors);
    lua_setglobal(l, "_world_obj_getcharscalebasedforcefactors");

    lua_pushcfunction(l, _world_obj_enabledynamics);
    lua_setglobal(l, "_world_obj_enabledynamics");
}
