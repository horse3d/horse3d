
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "luamem.h"
#include "scriptcore.h"
#include "scriptcoreaudio.h"
#include "scriptcoreerror.h"
#include "vfs.h"

static h3daudiodevice **_opendevices = NULL;
static int _opendevicescount = 0;


int _h3daudio_listsoundcards(lua_State *l) {
    int backend = H3DAUDIO_BACKEND_SDL2;
    if (lua_gettop(l) >= 1 &&
            lua_type(l, 1) == LUA_TBOOLEAN &&
            lua_toboolean(l, 1)) {
        backend = H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY;
    }
    int count = h3daudio_GetDeviceSoundcardCount(backend);
    int returnedcount = 0;
    lua_newtable(l);
    if (count <= 0) {
        lua_pushnumber(l, 1);
        lua_pushstring(l, "default unknown device");
        lua_settable(l, -3);
        return 1;
    }
    int i = 0;
    while (i < count) {
        char *s = h3daudio_GetDeviceSoundcardName(backend, i);
        if (!s) {
            lua_pushstring(l, "out of memory");
            return lua_error(l);
        }
        returnedcount++;
        if (!luamem_EnsureFreePools(l) ||
                !luamem_EnsureCanAllocSize(l, strlen(s) * 2)) {
            free(s);
            lua_pushstring(l, "out of memory");
            return lua_error(l);
        }
        lua_pushnumber(l, i + 1);
        lua_pushstring(l, s);
        lua_settable(l, -3);
        free(s);
        i++;
    }
    return 1;
}

int _h3daudio_getdevicename(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(l, "expected arg #1 to be "
                       "horse3d.audio.audiodevice");
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_AUDIODEVICE) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.audiodevice");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value
    );
    if (!dev) {
        lua_pushstring(l, "couldn't access device - was it closed?");
        return lua_error(l);
    }
    lua_pushstring(l, h3daudio_GetDeviceName(dev));
    return 1;
}

int _h3daudio_opendevice(lua_State *l) {
    int backend = H3DAUDIO_BACKEND_SDL2;
    const char *devicename = NULL;
    if (lua_gettop(l) >= 1 &&
            lua_type(l, 1) == LUA_TBOOLEAN) {
        if (lua_toboolean(l, 1))
            backend = H3DAUDIO_BACKEND_SDL2_EXCLUSIVELOWLATENCY;
        if (lua_gettop(l) >= 2 &&
                lua_type(l, 2) == LUA_TSTRING) {
            devicename = lua_tostring(l, 2);
        }
    } else if (lua_gettop(l) == 1 &&
            lua_type(l, 1) == LUA_TSTRING) {
        devicename = lua_tostring(l, 1);
    }

    h3daudiodevice **newopendevices = realloc(
        _opendevices,
        sizeof(*_opendevices) * (_opendevicescount + 1)
    );
    if (!newopendevices) {
        lua_pushstring(l, "failed to allocate devices list");
        return lua_error(l);
    }
    _opendevices = newopendevices;

    char *error = NULL;
    h3daudiodevice *dev = h3daudio_OpenDeviceEx(
        48000, 2048, backend, devicename, &error
    );
    if (!dev) {
        if (error) {
            char buf[512];
            snprintf(
                buf, sizeof(buf) - 1,
                "failed to open device: %s", error
            );
            lua_pushstring(l, buf);
            free(error);
        } else {
            lua_pushstring(l, "failed to open device: unknown error");
        }
        return lua_error(l);
    }
    _opendevices[_opendevicescount] = dev;
    _opendevicescount++;

    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_AUDIODEVICE;
    ref->value = h3daudio_GetDeviceId(dev);
    luaL_newmetatable(l, "horse3d.audio.device");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_audio_device_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
    return 1;
}

int _h3daudio_playsound(lua_State *l) {
    if (lua_gettop(l) < 5 ||
            lua_type(l, 1) != LUA_TSTRING ||
            lua_type(l, 5) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected 5 arguments of types "
            "string, number, number, boolean, horse3d.audio.audiodevice"
        );
        return lua_error(l);
    }
    const char *soundpath = lua_tostring(l, 1);
    int existsresult = 0;
    if (!vfs_Exists(soundpath, &existsresult, 0)) {
        lua_pushstring(l, "vfs_Exists() failed - out of memory?");
        return lua_error(l);
    }
    if (!existsresult) {
        char buf[512];
        snprintf(buf, sizeof(buf) - 1,
                 "sound file not found: %s", soundpath);
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    double volume = lua_tonumber(l, 2);
    double panning = lua_tonumber(l, 3);
    int loop = lua_tonumber(l, 4);
    if (((scriptobjref*)lua_touserdata(l, 5))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 5))->type !=
            OBJREF_AUDIODEVICE) {
        lua_pushstring(l, "expected arg #1 to be "
                       "horse3d.audio.audiodevice");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 5))->value
    );
    if (!dev) {
        lua_pushstring(l, "couldn't access device - was it closed?");
        return lua_error(l);
    }
    volume = fmax(0, fmin(1, volume));
    panning = fmax(-1, fmin(1, panning));
    uint64_t soundid = h3daudio_PlaySoundFromFile(
        dev, soundpath, volume, panning, loop
    );
    if (soundid == 0) {
        char buf[512];
        snprintf(buf, sizeof(buf) - 1,
                 "failed to play sound: %s", soundpath);
        lua_pushstring(l, buf);
        return lua_error(l);
    }
    scriptobjref *ref = lua_newuserdata(l, sizeof(*ref));
    memset(ref, 0, sizeof(*ref));
    ref->magic = OBJREFMAGIC;
    ref->type = OBJREF_PLAYINGSOUND;
    ref->value = soundid;
    ref->value2 = h3daudio_GetDeviceId(dev);
    luaL_newmetatable(l, "horse3d.audio.sound");
    lua_setmetatable(l, -2);
    lua_pushstring(l, "horse3d_audio_sound_set_metatableref");
    lua_gettable(l, LUA_REGISTRYINDEX);
    if (lua_type(l, -1) != LUA_TFUNCTION) {
        lua_pop(l, 1);
    } else {
        lua_pushvalue(l, -2);
        lua_call(l, 1, 0);
    }
    return 1;
}

int _h3daudio_stopsound(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected arg #1 to be "
            "horse3d.audio.sound"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        // Device is already gone, so the sound is too. Do nothing.
        return 0;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    h3daudio_StopSound(dev, soundid);
    return 0;
}

int _h3daudio_soundsetvolume(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(
            l, "expected args of types "
            "horse3d.audio.sound, number"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushboolean(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    double volume = 0.7;
    double panning = 0;
    h3daudio_GetSoundVolume(dev, soundid, &volume, &panning);
    volume = fmax(0, fmin(1.0, lua_tonumber(l, 2)));
    h3daudio_ChangeSoundVolume(dev, soundid, volume, panning);
    return 0;
}

int _h3daudio_soundsetpanning(lua_State *l) {
    if (lua_gettop(l) < 2 ||
            lua_type(l, 1) != LUA_TUSERDATA ||
            lua_type(l, 2) != LUA_TNUMBER) {
        lua_pushstring(
            l, "expected args of types "
            "horse3d.audio.sound, number"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushboolean(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    double volume = 0.7;
    double panning = 0;
    h3daudio_GetSoundVolume(dev, soundid, &volume, &panning);
    panning = fmax(-1.0, fmin(1.0, lua_tonumber(l, 2)));
    h3daudio_ChangeSoundVolume(dev, soundid, volume, panning);
    return 0;
}

int _h3daudio_soundisplaying(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected arg #1 to be "
            "horse3d.audio.sound"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushboolean(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    lua_pushboolean(l, h3daudio_IsSoundPlaying(dev, soundid));
    return 1;
}

int _h3daudio_soundhaderror(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected arg #1 to be "
            "horse3d.audio.sound"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushboolean(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    lua_pushboolean(l, h3daudio_SoundHadPlaybackError(dev, soundid));
    return 1;
}

int _h3daudio_soundgetvolume(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected arg #1 to be "
            "horse3d.audio.sound"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushnumber(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    double volume = 0.0;
    double panning = 0;
    h3daudio_GetSoundVolume(dev, soundid, &volume, &panning);
    lua_pushnumber(l, volume);
    return 1;
}

int _h3daudio_soundgetpanning(lua_State *l) {
    if (lua_gettop(l) < 1 ||
            lua_type(l, 1) != LUA_TUSERDATA) {
        lua_pushstring(
            l, "expected arg #1 to be "
            "horse3d.audio.sound"
        );
        return lua_error(l);
    }
    if (((scriptobjref*)lua_touserdata(l, 1))->magic !=
            OBJREFMAGIC ||
            ((scriptobjref*)lua_touserdata(l, 1))->type !=
            OBJREF_PLAYINGSOUND) {
        lua_pushstring(l, "expected arg #1 to be horse3d.audio.sound");
        return lua_error(l);
    }
    h3daudiodevice *dev = h3daudio_GetDeviceById(
        (int)((scriptobjref*)lua_touserdata(l, 1))->value2
    );
    if (!dev) {
        lua_pushnumber(l, 0);
        return 1;
    }
    uint64_t soundid = ((scriptobjref*)lua_touserdata(l, 1))->value;
    double volume = 0.0;
    double panning = 0;
    h3daudio_GetSoundVolume(dev, soundid, &volume, &panning);
    lua_pushnumber(l, panning);
    return 1;
}

void scriptcoreaudio_AddFunctions(lua_State *l) {
    lua_pushcfunction(l, _h3daudio_opendevice);
    lua_setglobal(l, "_h3daudio_opendevice");
    lua_pushcfunction(l, _h3daudio_listsoundcards);
    lua_setglobal(l, "_h3daudio_listsoundcards");
    lua_pushcfunction(l, _h3daudio_getdevicename);
    lua_setglobal(l, "_h3daudio_getdevicename");
    lua_pushcfunction(l, _h3daudio_playsound);
    lua_setglobal(l, "_h3daudio_playsound");
    lua_pushcfunction(l, _h3daudio_stopsound);
    lua_setglobal(l, "_h3daudio_stopsound");
    lua_pushcfunction(l, _h3daudio_soundsetpanning);
    lua_setglobal(l, "_h3daudio_soundsetpanning");
    lua_pushcfunction(l, _h3daudio_soundsetvolume);
    lua_setglobal(l, "_h3daudio_soundsetvolume");
    lua_pushcfunction(l, _h3daudio_soundgetpanning);
    lua_setglobal(l, "_h3daudio_soundgetpanning");
    lua_pushcfunction(l, _h3daudio_soundgetvolume);
    lua_setglobal(l, "_h3daudio_soundgetvolume");
    lua_pushcfunction(l, _h3daudio_soundisplaying);
    lua_setglobal(l, "_h3daudio_soundisplaying");
    lua_pushcfunction(l, _h3daudio_soundhaderror);
    lua_setglobal(l, "_h3daudio_soundhaderror");
}
