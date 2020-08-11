
# Horse64 Specification

This specification describes the syntax, semantics, and grammar of the
Horse64 behavior, with additional notes about the relevant
[horsec](../horsec/horsec.md)/[horsevm](../Misc Tooling/horsevm.md)
details as far as they strongly affect language behavior.


## Overview

In overall, Horse64 is situated somewhere between a scripting language
(like JavaScript, Python, Lua, ...) and a generic managed
backend programming language (like C#, Java, Go, ...).

Here is an overview how it roughly compares:

|*Feature List*                 |Horse64 |Scripting Lang|Backend Lang    |
|-------------------------------|--------|--------------|----------------|
|Dynamic Types                  |Yes     |Yes           |No              |
|Heavy Duck Typing              |No      |Some of them  |No              |
|Garbage Collected              |Yes     |Yes           |Some of them    |
|Compiles AOT & Optimized       |Yes     |Usually no    |Yes             |
|Slow dynamic scope lookups     |No      |Yes           |Usually no      |
|Runtime eval()                 |No      |Yes           |Usually no      |
|Runtime module load            |No      |Used often    |Used rarely     |
|Produces Standalone Binary     |Yes     |No, or tricky |Some of them    |
|Beginner-friendly              |Yes     |Yes           |Some of them    |
|Dynamic REPL mode              |No      |Yes           |Some of them    |
|Compiler easy to include       |Yes     |Yes           |Some of them    |


## Syntax Basics

The basic syntax is a mix of Python, Lua, and C.
Here is a code example:

```horse64
func main {
    var numbers_list = []
    var i = 0
    while i < 500 {
        numbers_list.add(i)
        i += 1
    }
    print("Hello World!")
    print("Here is a long list:\n" + numbers_list.as_str)
}
```

It has the following notable properties:

- *Mostly imperative with clean OOP.* Classes exist and
  are very clean and simple, but you don't need to use them.

- *No significant whitespace,* indentation doesn't matter.
  However, we suggest you always use 4 spaces.

- *No significant line breaks,* all code can be written in one
  line. While we recommend this isn't used, if you do, please
  separate statements by two space characters for better
  visual separation.

- *Strong scoping,* as found e.g. with JavaScript's new `let`.
  All variables only exist in their scope as enclosed by the
  surrounding `{` and `}` code block brackets, and must be
  declared before use.

- *No manual memory management,* since Horse64 is garbage-collected.
  There is a `new` operator to make object instances, but no
  explicit delete operator of any kind.

See the respective later sections for both the detailed grammar,
as well as details on Garbage Collection.


## Datatypes

Horse64 is mostly inspired by Python in its core semantics,
while it does away with most of the dynamic scope.

It has the following datatypes, where-as passed by value
being "yes" means assigning it to a new variable will create
a copy, while "no" means it will reference the existing copy:

|*Data Type*    |Passed by value  |Garbage-Collected  |
|---------------|-----------------|-------------------|
|none           |Yes              |No                 |
|boolean        |Yes              |No                 |
|number         |Yes              |No                 |
|string         |Yes              |No                 |
|function       |No               |No                 |
|list           |No               |Yes                |
|vector         |No               |Yes                |
|map            |No               |Yes                |
|set            |No               |Yes                |
|object instance|No               |Yes                |

A "yes" entry for garbage-collection means any new instance of
the given data type will cause garbage collector load, with
the according performance implications.

Please note there are more hidden differentiations in the
runtime:

- An object instance can actually be an error instance
  (created through a raised error, rather than new on
  a regular class) which is internally optimized to not
  be garbage collected.

- A function can also be a closure, indirectly causing
  garbage collector loads through variables captured by
  reference.

- A hidden special value indicates a keyword argument
  not being set. This is never exposed to the user outside
  of bugs.

- Short strings internally have a different type to cut
  down on allocations and indirections.

- Numbers internally can be either a 64bit integer, or
  a 64bit floating point value. Conversions happen
  transparently.

---
This documentation is CC-BY-4.0 licensed.
( https://creativecommons.org/licenses/by/4.0/ )
Copyright (C) 2020 Horse64 Team (See AUTHORS.md)
