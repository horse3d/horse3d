# Misc: Virtual File System

## What is this?

Horse3D ships with a **Virtual File System**, or "VFS" in short. The
VFS allows you to load up .h3dpak archives as virtual folders. This
allows you to easily contain your
entire game in one archive, while all your path references to local
files on disk can remain unchanged.

E.g. if you create an object like this:
```lua
local obj = horse3d.world.add_object("my-model-file.obj")
```
... then Horse3D will check any known `.h3dpak` archives for this file first,
and only afterwards for the disk path.


## Path resolution

Horse3D's VFS will check known `.h3dpak` archives first for any resource,
be it 3d model, sound, or whatever else.
In addition, `io.open` in Horse3D can open any archive files in read-only
mode as well. (If you want to write, you can only do so to paths not pointing
to a file contained in a .h3dpak.)

When resolving any path, the path is normalized relative to the game folder.
Therefore, you can even specify resources as an absolute path inside your
game directory, e.g. with your game running in `C:\mygame\` you can load
up a .h3dpak-contained file `myfile.txt` via
`io.open("C:\\mygame\\myfile.txt")`. This should allow you to switch to
.h3dpak archives with minimal effort when releasing your game.
**Obviously, don't hardcode absolute disk paths in any case,**
but this might help you if you derive absolute paths from relative ones
at runtime.


## What is an .h3dpak archive

A .h3dpak file is simply a ZIP archive renamed. To create a .h3dpak file,
make sure your operating system shows **file extensions**, then create a
.zip file with the contents you want, and rename it to a .h3dpak ending.


## Which .h3dpak files are loaded

By default, when Horse3D launches it will look for any .h3dpak file
in the directory it is in and load them.
Then, if any of the .h3dpak archives includes a `main.lua` at the root level,
it will run this as your game. Only if no `main.lua` is found in any
archive, then Horse3D will check for a `main.lua` as a regular disk file.

Beyond that, your game can then use @{horse3d.vfs.add_h3dpak} to load up
.h3dpak files from any other location.
