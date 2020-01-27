#ifndef HORSE3D_LUAMEM_H_
#define HORSE3D_LUAMEM_H_


int luamem_EnsureFreePools(lua_State *l);

int luamem_EnsureCanAllocSize(lua_State *l, size_t bytes);

lua_State *luamem_NewMemManagedState();


#endif  // HORSE3D_LUAMEM_H_
