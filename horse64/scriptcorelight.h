#ifndef HORSE3D_SCRIPTCORELIGHT_H_
#define HORSE3D_SCRIPTCORELIGHT_H_

#include <lua.h>

typedef struct h3dlight h3dlight;

void scriptcorelight_AddFunctions(lua_State *l);

void lua_PushLightRef(lua_State *l, int64_t objid);


#endif  // HORSE3D_SCRIPTCORELIGHT_H_
