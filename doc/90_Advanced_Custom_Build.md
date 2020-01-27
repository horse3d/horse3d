# Advanced: Custom Build

@lookup horse3d_test.lua


## How to build Horse3D yourself

If you are an advanced developer, you may want to build Horse3D yourself.
Here is how to do it:


### Prerequisites

**Firstly: note building is only supported on x64 Linux systems right now,
even for the Windows release.** On Windows, use WSL or a Linux VM in
Virtualbox or similar to build.

To build, you need the following tools preinstalled:

* gcc, g++ (or for a Windows build, `x86_64-w64-mingw32-gcc` or similar)
* GNU make, GNU autotools, cmake
* lua (5.1 or newer, won't be linked but needed for build tools)
* python3 (again used for build tools)
* zip, unzip
* git
* Development headers for X11, OpenGL, Wayland, alsa, and pulseaudio
  (Linux build only)


### Doing the build

To build Horse3D's Linux release from the
[git repository source](https://github.com/horse3d/horse3d),
clone it with git, `cd` into the directory, and use these
terminal commands:
```
$ pwd
/home/ellie/Develop/horse3d
$ git submodule init && git submodule update
$ make
```

To build the Windows release, install a cross-compiler like
`x86_64-w64-mingw32-gcc` through your distribution package manager, and use:

```
$ CC=x86_64-w64-mingw32-gcc make
```

**Important: if you built the Linux release before or otherwise need a
clean slate, make sure to clean up first:**
the following commands clean things up and rebuild the
dependencies (make sure to specify `CC=... make build-deps` too
if you are cross compiling):

```
$ make veryclean
$ make build-deps
```


## Unit tests

To run the unit tests, use:
```
make test
```


## Release build

By default, building from the git repository code gives you a debug build.
The debug build has less optimizations, debugging symbols, and asserts
enabled for debugging Horse3D itself.

If you want a release build instead, use this instead:
```
$ make release
```
*(This command, unlike the default make target, will also invoke build-deps
and make clean implicitly to always build from scratch. Consequently,
make release is always a very time consuming action.)*


## Generating the documentation

To generate the documentation, install
[ldoc](https://github.com/stevedonovan/ldoc) and python3,
and then run:

```
$ make doc
```

The generated files appear in the `./doc/` directory.
