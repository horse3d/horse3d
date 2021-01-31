
# Features Overview

This is a quick overview of **Horse64**'s language and tooling
features. You may also want to [read the design approach](
./Design.md) and [the specifications](./Specification/Horse64.md).

Summed up, Horse64 finds itself somewhere between scripting and
backend languages. If you find Python/Lua/JS too error prone for
large projects but Go/C#/Java too tedious, then Horse64 might
be for you. The syntax is a mix of Python, Lua, and Go.


## Notable features

- **Dynamic types with simplicity** as you know and love it
  from Python, Lua, etc. with exceptionally clean syntax.
  Worry not, strong typing and AOT checks offset many potential bugs,
  see below.

- **Strong types with avoidance of error-prone type coercions**,
  e.g. no accidential additions of numbers and strings with
  unexpected results. This prevents many sneaky typing bugs.

- **Flexible numbers data type with well-defined errors,**
  like a proper overflow error and division by zero errors.
  No not-a-number value with its problems like NaN poisoning.

- **Unicode as a first class citizen.** Strings do indexing,
  sub string, and length computations based on glyph boundaries
  rather than code points. Even e.g. emojis have a length of one.

- **AOT bytecode with excellent checks.** Programs are compiled
  ahead of time, and thoroughly: horsec finds typos, undefined
  variables, many type errors and missing attributes, and more.

- **Self contained tooling.** Even on Windows, all you need is
  the horsec binary, and e.g. horp if you want to manage packages.
  No need for C/C++ compilers, a big IDE, or anything else. (Unless
  you want to!)

- **Self contained programs.** All your programs are compiled
  to a single, self-contained binary with only libc dependencies.
  No need for shipping a big runtime, or making the user install one.

- **First class async networking.** Writing server code that
  handles hundreds of connections easily is trivial. All
  tools are built-in, even TLS/SSL support with a simple switch.

- **Comprehensive standard library.** JSON, web backend and client
  tools, HTTP/HTTPS, easy and comprehensive filesystem functions,
  and more, are all integrated into Horse64 for easy use.

- **Both for advanced coders and beginners.** Horse64 has many
  small tweaks that don't hurt experts but help beginners, like
  clear keywords, simple syntax, good error checks, and more.

- **Large projects are well supported.** The excellent module
  handling, support for cyclic imports and in-depth static
  name resolution, a good package manager, and more, make
  Horse64 ready to handle even sizeable endaveours.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 20202-2021 Horse64 Team (See AUTHORS.md)
