
# Horse64 Specification

This specification describes the syntax, semantics, and grammar of the
Horse64 behavior, with additional notes about
[horsec](../horsec/horsec.md) and [horsevm](../Misc%20Tooling/horsevm.md)
details when relevant to language behavior.


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

- *Suitable for imperative and clean OOP.* Classes exist and
  are clean and simple, but you don't need to use them.

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
the constructs and how they work, as well as details on Garbage
Collection and more.


## Syntax Constructs

### Functions and calls

All code other than classes and global variables must be inside
functions declared with `func`.
The `main` func in the code file supplied to `horsec compile` will
be the starting point, and other functions like the built-in `print`
can be called as a statement:

```horse64
func main {
    print("Hello World! This is where my program starts.")
}
```

### Variable definitions and assignments

Variables can be declared with any given unicode name.
They default to none, but may be assigned a default value.
They can be reassigned later if known in the current scope:

```horse64
var my_none_variable
var my_number_variable = 5
my_number_variable = 7  # change value to 7!
```

For constant unchangeable values, use `const` instead:

```horse64
const my_variable = 5
```
(This sometimes also allows the compiler to optimize better.)


### Conditionals

Conditionals can be tested with `if` statements, the inner
code runs if the conditional evaluates to a trueish value. 
Otherwise, all `elseif` are tested if present, and `else`
is taken if all fails.

```horse64
if my_tested_value == 5 {
    print("This is the case where the value is 5.")
} elseif my_tested_value == 6 {
    print("This is the case where the value is 6.")
} else {
    print("The value is something else than 5 or 6.")
}
```

**Evaluation order:**

- Later `elseif` conditionals are never evaluated if a previous
  branch is taken.

- The conditional itself is evaluated in precendece order and left-to-right,
  evaluation stopping as soon as the value is clear. (E.g. a logical `and`
  combination will only have the left-hand side evaluated if that returns
  a falseish value, skipping the right-hand side.)


### Loops

Horse64 supports two loop types, a `while` loop with an
arbitrary conditional, and a `for` loop that does a for
each iteration over a container.

```horse64
# While loop (using a conditional):

var i = 0;
while (i < 10) {
    print("Counting up: " + i.as_str)
    i += 1
}

# For loop (iterating a container):

var items = ["banana", "chair"]
for item in items {
    print("Item: " + item)
}
```
The conditional of a while loop is re-evaluated before each next
rerun of the loop, retriggering inner side effects like embedded calls.

Container iteration notes:

- A container can be any of: list, set, vector, or map.
- For the map, the keys will be iterated like a set.
- If the container is changed during iteration,
  a `ContainerIterationError` will be raised.


### More

FIXME


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
runtime which **may be subject to future change**:

- An object instance can actually be an error instance
  (created through a raised error, rather than new on
  a regular class) which is internally optimized to not
  be garbage collected. However, it is still passed by
  reference and otherwise behaves like a regular object
  instance.

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
