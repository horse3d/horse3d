#ifndef HORSE3D_SCRIPTCOREWORLD_H_
#define HORSE3D_SCRIPTCOREWORLD_H_

#include <lua.h>

typedef struct h3dobject h3dobject;

void scriptcoreworld_AddFunctions(lua_State *l);

void lua_PushObjectRef(lua_State *l, int64_t objid);

void world_RunObjectLuaOnUpdate(
    lua_State *l, h3dobject *obj, double dt
);

int parse_invis_and_nocol_keywords(
    lua_State *l, h3dmeshloadinfo *loadinfo,
    int invisindex, int nocolindex
);

#endif  // HORSE3D_SCRIPTCOREWORLD_H_
