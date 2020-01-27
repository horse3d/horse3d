#ifndef HORSE3D_SCRIPTCORE_H_
#define HORSE3D_SCRIPTCORE_H_

#include <lua.h>

#define OBJREF_TEXTURE 1
#define OBJREF_MESH 2
#define OBJREF_OBJ 3
#define OBJREF_AUDIODEVICE 4
#define OBJREF_LIGHT 5
#define OBJREF_PLAYINGSOUND 6
#define OBJREF_UIOBJECT 7
#define OBJREF_UIPLANE 8

#define OBJREFMAGIC 4862490

typedef struct scriptobjref {
    int magic;
    int type;
    int64_t value;
    int64_t value2;
} scriptobjref;


char *_prefix__file__(
    const char *contents, const char *filepath
);

int scriptcore_Run();

lua_State *scriptcore_MainState();

#endif  // HORSE3D_SCRIPTCORE_H_
