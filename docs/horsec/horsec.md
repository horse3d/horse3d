
# HorseC / Compiler

The `horsec` program allows you to compile Horse64 source
code to binaries. The source code can be found in the
[core.horse64.org repository](
    ../Contributing.md#corehorse64org-package
) inside the `horse64/compiler/` folder.


## Get it

`horsec` is part of the [official SDK](../Introduction.md#download).

You can also fetch the source code and compile `horsec` yourself
if you prefer your own binaries.

To compile `horsec` manually, do the following:

1. Install git, gcc, GNU make, python3. Please note that building
   is only supported on Linux. For a Windows build, install a MinGW
   cross compiler for 64bit targets.

2. `git clone` the [repository](../Contributing.md#corehorse64org-package)

3. Change directory in your terminal into the repository folder

4. Run: `git submodule init --update` (fetches dependencies)

5. Run: `make` (does actual build)

   To cross-compile, run `CC=<your-cross-compiler> make` instead.

6. You should now have a `horsec`/`horsec.exe` binary.


## How does `horsec` work

If you think you found a bug then please check this full
documentation carefully and possibly consult with community
members. If that confirms you likely found a bug,
[report it to get it fixed](../Contributing.md#report-bugs).

A few starting points to learn more about horsec's internals:

- [Compiler Stages of horsec](./Compiler%20Stages.md)


---
*This documentation is CC-BY-4.0 licensed.
( https://creativecommons.org/licenses/by/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
