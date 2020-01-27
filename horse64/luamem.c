
#include <assert.h>
#include <inttypes.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <lstate.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hash.h"
#include "horseheap.h"
#include "scriptcore.h"

//#define HORSE3D_NOHORSEHEAP

typedef struct luastateinfo {
    lua_State *l;
    int64_t memory_use;
    horseheap *hheap;
} luastateinfo;


static void *lua_memalloc(
        void *userdata, void *ptr,
        size_t oldsize, size_t newsize
        ) {
    luastateinfo *lsinfo = (luastateinfo *)userdata;
    if (!lsinfo)
        return NULL;
    if (!ptr)
        oldsize = 0;
    void *result = NULL;
    if (newsize == 0) {
        if (ptr) {
            lsinfo->memory_use -= oldsize;
            #ifndef HORSE3D_NOHORSEHEAP
            horseheap_Free(lsinfo->hheap, ptr);
            #else
            free(ptr);
            #endif
        }
    } else {
        lsinfo->memory_use += ((int)newsize - (int)oldsize);
        #ifndef HORSE3D_NOHORSEHEAP
        result = horseheap_Realloc(lsinfo->hheap, ptr, newsize);
        #else
        result = realloc(ptr, newsize);
        #endif
    }
    assert(lsinfo->memory_use >= 0);
    return result;
}

int luamem_EnsureFreePools(lua_State *l) {
    if (!l)
        return 0;
    #ifndef HORSE3D_NOHORSEHEAP
    return 1;
    #else
    luastateinfo *lsinfo = (luastateinfo *)(l->l_G->ud);
    if (!lsinfo)
        return 0;
    return horseheap_EnsureHeadroom(lsinfo->hheap);
    #endif
}

int luamem_EnsureCanAllocSize(lua_State *l, size_t bytes) {
    if (!l)
        return 0;
    #ifndef HORSE3D_NOHORSEHEAP
    return 1;
    #else
    luastateinfo *lsinfo = (luastateinfo *)(l->l_G->ud);
    if (!lsinfo)
        return 0;
    return horseheap_EnsureCanAllocSize(lsinfo->hheap, bytes);
    #endif
}

lua_State *luamem_NewMemManagedState() {
    luastateinfo *lstateinfo = malloc(sizeof(*lstateinfo));
    if (!lstateinfo)
        return NULL;
    memset(lstateinfo, 0, sizeof(*lstateinfo));
    #ifndef HORSE3D_NOHORSEHEAP
    lstateinfo->hheap = horseheap_New();
    if (!lstateinfo->hheap) {
        free(lstateinfo);
        return NULL;
    }
    #endif
    lua_State *l = NULL;
    #ifndef HORSE3D_NOHORSEHEAP
    l = lua_newstate(lua_memalloc, lstateinfo);
    #else
    l = luaL_newstate();
    #endif
    if (!l) {
        #ifndef HORSE3D_NOHORSEHEAP
        horseheap_Destroy(lstateinfo->hheap);
        #endif
        free(lstateinfo);
        return NULL;
    }
    lstateinfo->l = l;
    return l;
}
