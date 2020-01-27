# Misc: Lua Environment

## Lua in Horse3D

This page documents information about the [Lua](https://lua.org)-environment
as contained in Horse3D.
**Please note that no separate Lua install is required, Horse3D contains it.**
All you need is a text editor like Notepad.

Lua version: 5.3 (normal interpreter, not LuaJIT)


## Differences to Standard Lua

- `print` is redirected to the ingame console
- Custom handler for error backtraces everywhere
- Some modules like "horse3d" and "lfs" (a reimplemented compatibility version)
  are integrated for use via `require` as usual
- A custom allocator is used behind the scenes to allow Horse3D to anticipate
  out of memory better
- I/O functions are wrapped to support @{86_Misc_VFS.md|Horse3D's VFS}


## Pitfalls

- "lfs" is a custom basic reimplementation
  of [LuaFileSystem](https://keplerproject.github.io/luafilesystem/), **it is
  not the official original "lfs"**
- `io.open` prefers `.h3dpak`-contained resources over disk
  files if the path is the same (see @{86_Misc_VFS.md|Horse3D's VFS} for h3dpak)
