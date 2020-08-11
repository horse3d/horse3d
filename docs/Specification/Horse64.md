
# Horse64 Specification

## Overview

In overall, Horse64 is situated somewhere between a scripting language
(like JavaScript, Python, Lua, ...) and a generic managed
backend programming language (like C#, Java, Go, ...).

Here is an overview how it roughly compares:

|*Feature List*                 |Horse64 |Scripting Lang|Backend Lang    |
|-------------------------------|--------|--------------|----------------|
|Dynamic Types                  |Yes     |Yes           |No              |
|Heavy Duck Typing              |No      |Some of them  |No              |
|Garbage Collected              |Yes     |Yes           |Some            |
|Compiles AOT & Optimized       |Yes     |Usually no    |Yes             |
|Slow dynamic scope lookups     |No      |Yes           |Usually no      |
|Runtime eval()                 |No      |Yes           |Usually no      |
|Runtime module load            |No      |Yes, common   |Yes, discouraged|
|Produces Standalone Binary     |Yes     |No, or tricky |Some of them    |
|Beginner-friendly              |Yes     |Yes           |Some of them    |
|Dynamic REPL mode              |No      |Yes           |Some of them    |

---
This documentation is CC-BY-4.0 licensed.
( https://creativecommons.org/licenses/by/4.0/ )
Copyright (C) 2020 Horse64 Team (See AUTHORS.md)
