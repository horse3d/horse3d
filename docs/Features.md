
# Features Overview

This is a quick overview of **Horse64**'s language and tooling
features. You may also want to [read the design approach](
./Design.md) and [the specifications](./Specification/Horse64.md).

Summed up, Horse64 finds itself somewhere between scripting and
backend languages. If you find Python/Lua/JS too error prone for
large projects but Go/C#/Java too tedious, then Horse64 might
be for you. The syntax is a mix of Python, Lua, and Go.


## Features

### Clean Syntax

**Dynamic types with elegant simplicity** as you know and love it
from Python, Lua, etc. offer a readable, clean syntax:
```horse64
func main {
    print("Hello World from Horse64!")
}
```
Strong typing and compile checks help with many potential bugs that
may go unnoticed in other dynamically typed languages.
No significant whitespace, so no "indentation bugs" and you
can type all your code in one line if you want. (Not recommended.)

### Strong types

**Strong types with avoidance of error-prone type coercions** e.g.
no accidential additions of numbers and strings with unexpected
results which prevents many sneaky typing bugs:
```horse64
func main {
    var v = "my value"
    v = 352
    print("This is a  value: " + v.as_str)  # .as_str not optional!
}
```
The horsec compiler also finds many other runtime errors at
compile time, like most typos and wrong references, which
would require extra tooling in Python, JS, and alike to catch.

### Flexible, safe numbers

**Flexible numbers data type with well-defined errors,** like a proper
overflow error and division by zero errors. No not-a-number value with
its problems like NaN poisoning. Example:
```horse64
func main {
    var v = 9000000000000000000
    v /= 0.0001  # Will trigger an OverflowError.
}
```
Numbers internally will be either integer or floats allowing a
larger precision for higher range integers than in languages that use
64bit floats only like JavaScript/NodeJS.

### Unicode Support

**Unicode-aware behavior as a first class citizen.** Strings do indexing,
sub string, and length computations based on glyph boundaries
rather than code points. Even e.g. complex emojis glyphs
are understood as a length of one:
```
func main {
    var flag = "\u1F1FA\u1F1F8'
    print("A multi code point flag emoji: " + flag)
    print("String length: " + flag.len.as_str)  # Prints: "String length: 1"
}
```

*Note: Unicode(R) is a registered trademark. No affiliation implied.*

### Compile checks

**AOT bytecode with excellent checks.** Programs are compiled
ahead of time (on the developer's machine, not on the user machine),
and thoroughly: horsec finds typos, undefined
variables, many type errors and missing attributes, and more.

### Portable tooling

**Self contained tooling with no SDK dependencies.** Even on Windows,
all you need is the horsec binary, and e.g. horp if you want to
manage packages, and any basic text editor.
No need for C/C++ compilers, a big IDE, or anything else. (Unless
you want to!) Note: using custom C/C++ extensions may require a
C/C++ toolchain, but the common extensions are all available prebuilt.

### Portable programs

**Self contained programs, even if you use UI, and more.** All
your programs are compiled to a single, self-contained binary with
only libc and system library dependencies.
No need for shipping an extra runtime, or making the user install one.
  
Once you got horsec, this is sufficient to ship your program self-contained:
```bash
horsec compile -o ./myprogram.exe ./mycode.h64
```

### Networking

**First class async networking.** Writing server code that
handles hundreds of connections easily is trivial, especially
with async being a deeply integrated first class mechanism. All
tools are built-in, even TLS/SSL support with a simple switch.

### Standard library

**Comprehensive standard library.** JSON, web backend and client
tools, HTTP/HTTPS, easy and comprehensive filesystem functions,
and more, are all integrated into Horse64 for easy use. (Note:
still work in progress.)

### Education suitable

**Designed for both advanced coders and beginners.** Horse64 has many
small tweaks that don't hurt experts but help beginners, like
clear keywords, simple syntax, good error checks, and more, while
preventing sneaky coding mistakes better than other choices like
Python/Lua. This makes Horse64 a good choice for beginner courses,
as well as experienced coders looking for a simpler option while
avoiding the common caveats of scripting languages.

### Enterprise ready

**Large projects are well supported.** The excellent module
handling, support for cyclic imports and in-depth static
name resolution, a capable package manager, and more, make
Horse64 ready to handle even sizeable endaveours. A built-in
deprecated keyword, and other tweaks help keep code maintainable.

### Cross-platform

**Supports multiple platforms.** Horse64 is available on many
[diverse platforms](./Platform%20Support.md) including Linux also
including ARM64 embedded, Windows, and FreeBSD (Note: WIP).
It will likely expand to more platforms in the future.

### What it doesn't do

**Please keep in mind Horse64 is not suitable for everything.**
Most notably, it is bad at the following things:

1. Horse64 can't compete with low-level languages like C/C++, Zig,
   Rust, ... on peformance, it is bytecode interpreted without use of
   JIT (to allow for better & safer maintenance). Please note it is
   however due to its async use often very scalable, more so
   than traditional scripting languages usually inherently would be,

2. You'll have a worse time with entirely undocumented code due to
   no inherent annotations as e.g. C# and Java have (but shouldn't
   you document your public interfaces with doc comments anyway?),

3. No advanced type constraints: as many scripting or scripting-like
   languages, Horse64 has an intentionally simple type system.
   If you like detailed elaborate interface declarations that only
   match certain very specific arguments, advanced type constraints,
   and so on, then you should probably use something else,
   
4. No tiny output binaries: Horse64 strives for simple deployment
   and painless portability, but this means all programs bring the
   entire VM, a full crypto and networking library, and more. If
   you need your binaries to be tiny, you may want to look elsewhere,

5. Not much access to unsafe operations: like many other higher level
   languages, Horse64 isn't suited for messing with raw pointers,
   manual memory management, and so on. If you need this you may
   want a different choice,

6. No limitless dynamic overriding: Horse64 is in some ways more static
   than Python, Ruby, and others, often to the benefit of better AOT
   checks and enforcing simpler code. Not everything is an object, and
   reflection isn't one of Horse64's strengths so it may often require a
   more conservative solution for better or for worse. (The designers
   think this is often for the better, but surely some will disagree.)

Also see [design overview for things that are, and are NOT supported.](
Design.md#overview).

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020-2021 Horse64 Team (See AUTHORS.md)*
