#ifndef HORSE3D_SCRIPTCOREUIPLANE_H_
#define HORSE3D_SCRIPTCOREUIPLANE_H_

#include <lua.h>

void scriptcoreuiplane_AddFunctions(lua_State *l);

void lua_PushUiObjectRef(lua_State *l, int64_t objid);

void lua_PushUiPlaneRef(lua_State *l, int64_t objid);

#endif  // HORSE3D_SCRIPTCOREUIPLANE_H_
