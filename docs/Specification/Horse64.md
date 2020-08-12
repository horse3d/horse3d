
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
|Compiles AOT & Optimized [1]   |Yes     |Usually no    |Yes             |
|Slow dynamic scope lookups [2] |No      |Yes           |Usually no      |
|Runtime eval()                 |No      |Yes           |Usually no      |
|Runtime module load            |No      |Used often    |Used rarely     |
|Produces Standalone Binary     |Yes     |No, or tricky |Some of them    |
|Beginner-friendly              |Yes     |Yes           |Some of them    |
|Dynamic REPL mode              |No      |Yes           |Some of them    |
|Compiler easy to include[2]    |Yes     |Yes           |Some of them    |
|Embeddable scripting engine[3] |No      |Yes, trivially|Non-trivial     |

- Footnote [1]: AOT as in "Ahead of Time", so not one-shot running of
  a script with either a simple one-pass compiler or Just-In-Time compilation,
  but rather a separate slower compilation step that produces a binary
  that is then executed later.

- Footnote [2]: This refers to whether calling a global variable, or a member
  / attribute on a class object instance will usually invoke a slow
  string hash name lookup at runtime.

- Footnote [3]: Easy to include compiler refers to using the compiler
  of this language from a program inside the language itself as a library,
  without the need of separately installing an entire SDK.

- Footnote [4]: Embeddable scripting engine refers to using the compiler and
  runtime in a *different* lowlevel language, e.g. to embed it for user
  scripts in a video game written in C/C++. Horse64 is not easily suitable
  for this.


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

### Functions and calls (func)

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

A function can have a list of arguments, e.g. observe this
example with arguments `first_value` and `second_value`:
```horse64
func my_func(first_value, second_value) {
    print("I was given this: " + first_value.as_str +
          ", " + second_value.as_str)
}
func main {
    my_func(5, "apple")
    # ^ Previous line outputs: "I was given this: 5, apple"
}
```

Arguments as such that have no default value are called
"positional arguments". All positional arguments must be
fully specified for any call in exact order.

**Keyword arguments (optional/keyword named arguments):**

To specify an argument freely by a name rather than exact order,
as well as optionally omit it, use "keyword arguments" as shown
here wih `option_1` and `option_2`:

```horse64
func my_func(mandatory_pos_arg, option_1=false, option_2=5) {
    print("Mandatory value: " + mandatory_pos_arg.as_str)
    print(
        "Options: option_1: %s "
        "option_2: %v", option_1".format(
            option_1, option_2
        )
    )
}
func main {
    my_func(25, option_2="test")
    # ^ Previous line outputs:
    #  "Mandatory value: 25"
    #  "Options: option_1: false option_2: test"
}
```

**Keyword argument evaluation:**

The default values for keyword arguments and their side effects
are evaluated **at call time,** again for every call.
Please note if you are familiar with the "Python" programming
language that this is different, since Python evaluates keyword
argument default values at program start.

**Return statements:**

To leave a function early, and/or return a value from it
other than the default of `none`, use the `return` statement:

```horse64
func my_func {
    return 5
}
func main {
    print("Value: " + my_func().as_str)
    # ^ Previous line prints: "Value: 5"
}
```
The return statement can also take no argument.

**Important ambiguity warning regarding return statements:**

If a return statement is followed by a call, the call will always
be assumed as argument even if meant as separate followup statement:

```horse64
func my_func {
    print("Oops, maybe don't run further")
    return  # bail out here

    print("test")
    # ^ This is actually seen by the compiler as: return print("test")
}
func main {
    my_func()
    # ^ Previous line prints: "test"
}
```
This is the case because Horse64 has no significant line breaks,
and since calls can both be inline values and statements, a `return`
will always greedily take them in as argument if in doubt.

This can only happen if you **both** 1. You use return with no
value specified, 2. You follow up a return with a statement
in the same code block/scope.

Please note the second step is *inherently nonsensical*, since
such a statement is always unreachable. Therefore, don't ever do this
and you will never face this ambiguity.


### Variable definitions and assignments (var)

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
const my_number_constant = 5
```
(This sometimes also allows the compiler to optimize better.)


### Conditionals (if, elseif, else)

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

- Side effects like inlined calls only apply for conditionals,
  and the parts of conditionals, that are actually evaluated at
  runtime.

- Later `elseif` branches' conditionals are only evaluated once the
  previous `if`/`elseif` was evaluated and determined to not be taken.
  Once a branch is taken, later ones are no longer evaluated.

- The conditional itself is evaluated in the defined operator precedence
  order first and outside-in/left-to-right second. Evaluation stops early
  for binary operators when the result is obvious. (E.g. a logical `and`
  combination will only have the left-hand side evaluated if that returns
  a falseish value, skipping the right-hand side.)


### Loops (while, for)

Horse64 supports two loop types, a `while` loop with an
arbitrary conditional, and a `for` loop that does a for
each iteration over a container. For details on how
the conditional is evaluated, see the Conditionals section.

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


### Defining custom classes (class, new)

To help with object-oriented programming, Horse64 allows
defining custom classes:

```horse64
class MyCircle {
    var radius = 5
    func init {
        print("Circle was created!")
    }
    func print_radius {
        print("Hello! My radius: " + self.radius)
    }
}

func main {
    var circle_1 = new myclass()
    # ^ Previous line outputs "Circle was created!"

    circle_1.print_radius()
    # ^ Previous line outputs "Hello! My radius: 5"

    print(circle_1.radius)
    # ^ Previous line outputs "5"
}
```

**Object-Oriented Programming (OOP) in Horse64 in detail:**

In Horse64, a class describes, as commonly done in OOP, how all
so-called object instances that were created from it behave.
This is done via so-called attributes that are populated onto
all object instances when they are created.

An object instance can be created from a class with the `new`
operator. A class can be defined via the `class` statement.

A class can specify `var` attributes which are initialised with the
given value on each object instance once created. (The default
value assignment with according side effects is evaluated no
earlier than actual object creation.) On each object instance, obtain
attributes via `identifier_referring_to_obj_instance.attribute_name`.
Attributes can be both read and written independently on each
object instance.

A class can specify `func` attributes which can then be called via
`identifier_referring_to_obj_instance.func_attr()`. These attributes
are fixed to the given function and can not be altered. Inside such
a function attribute's statements, the special identifier `self`
refers to the current object instance it runs on.

If a class specifies the optional `init` function attribute for its
object instances, then this function will be automatically invoked
by `new` on creation.

**OOP Best Practices:**

We recommend you **DO NOT** use a class if all it has is one single
function attribute, and you just set some other attribute values and
then proceed to call that one function. This indicates that you should
just use a plain function with parameters instead. **Overuse of
object-oriented programming obscures code flow with hidden state,**
it becomes no longer obvious what a function call depends on if the
parameters are hidden in an object instance rather than spelled out.

A class / OOP is a good match for anything that would also make
a good self-contained object with complex behavior and state logically,
e.g. a file object with read/write/... and more functions, something
that represents a data record with multiple manipulation methods, etc.


### Deriving classes (extends)

Classes can derive from a different existing class, inheriting
its existing attributes. This is done via `extends`:

```horse64
class Rectangle {
    var width_x = 5
    var width_y = 5
}
class BoxShaped extends Rectangle {
    var height_z = 5
}
func main {
    var box = new BoxShaped()
    print(  # will output: "Size: 5x5x5"
        "Size: %vx%vx%v".format(
            width_x, width_y, width_z
        )
    )
}
```

A class that extends (=derives from) another will then have
the combined attributes on all its object instances. Function
attributes may be specified on the derived class even when
existing on the base class, in which case they will override
them. The special identifier `base` can be used to call the
original function attribute via `base.overridden_func_attr()`.

If `init` is overridden, `base.init(...)` must be called
somewhere inside the new overriding function attribute,
and this call must not be inside any conditional block.


### Raising errors (raise)

You may raise errors to indicate unrecoverable errors, like
being passed an argument of wrong type. This is done with the
`raise` statement:

```horse64
func my_func(number_value) {
    if type(number_value) != "number" {
        raise TypeError("argument must be number")
    }
    print("Received number: " + number_value.as_str)
}
```
This will cause execution to stop at `raise`, and bail -
similar to a `return`, but beyond just this function and
up the entire call chain until either a `rescue` (see
next section) stops it, or the original `main` is bailed out of.
In the latter case, the program ends.

**Error best practice:**

**Errors should only ever be used to handle 1. obvious programming
mistakes of whoever called your code, 2. unhandleable errors caused
by the outside world like I/O or network failure.** We
strongly recommend that you **do NOT use errors for events
expected to happen or unavoidable in normal operation**.

E.g. please do **NOT** use errors for:

- To inform the caller of a special case while the operation
  was successful anyway
- To inform the caller of an event that regularly happens in normal
  expected operation, e.g. the regular end of a file or stream,
  or regularly reaching the end of a data set like an iterated container
- To make it easier for yourself to bail out of a call chain where
  nothing really unusual occured, even if you just use it internally

Where you **should** use errors:

- I/O errors that aren't usually expected to happen
- Out of memory conditions or other unexpected resource exhaustion
- Invalid arguments passed to your function that should have been
  preventable by adhering to its documentation
- Invalid userdata passed to your code that should have been
  previously sanitized and wasn't
- You are parsing a complex data format passed by the caller,
  and the passed data was found to be unrecoverably invalid

Errors are a dangerous tool that should not be overused, due to
their potential to be so disruptive. Errors are complex to contain
(via `rescue`, see below) and can even unintentionally terminate the
entire program if outside code isn't aware of them possibly being raised.

Therefore, in summary, **only use errors where appropriate.**
Please also always document in a comment what errors your functions
might raise, so that the caller can decide to `rescue` them.

**Built-in error types:**

For a list of built-in error types, please consult the
standard library reference. You can use any of these as you see
fit, or if none is descriptive enough for your use, you can derive
your own as a class:

```horse64
class MyParserError extends RuntimeError {}
```
This is only recommended if you don't find a built-in type to
fit your use case at all.


### Handling errors (do/rescue)

To handle errors without them terminating the entire program,
use a `do` statement with a `rescue` clause:

```horse64
func main {
    do {
        # This code in here is known to possibly raise a RuntimeError():
        dangerous_func()
    } rescue RuntimeError {
        print("Oops, there was an error! Thank god we are safe.")
    }
}
```
As soon as any code inside the first code block following `do` errors,
the type of the error is checked against the one specified in your
`rescue` clause, and if it matches, it won't propagate up further but
instead your rescue clause will run. After the rescue clause ends,
execution resumes after the `do` block as usual.

**Catch-all rescueing:**

To rescue from any type of error, it is possible to put `rescue Error`
as the base class of all errors. **However, this is not recommended:**
unintentionally rescueing from any errors, even those you may not even
be aware were happening and could indicate grave program errors, can
cause follow-up bugs and security errors in your program. Therefore,
please rescue from errors as specific as possible, with an exact
knowledge of how to resume your program safely for that exact type
of error.


### Cleanup in case of error (do/finally)

Sometimes you may not want to rescue from an error, especially
unknown errors you didn't anticipate anyway, but at least do
a basic clean-up to prevent worse fallout from happening.
This can e.g. be to close an opened file again after writing.

To do this, use a `do` statement with a finally block:

```horse64
var myfile 
do {
    myfile = io.open("C:\\temp\\testfile.txt, "w")
    myfile.write("test")  # might cause IOError
} finally {
    # This always runs at the end, no matter if above block had an error.
    if myfile != none {
        myfile.close()
    }
    # If we had an error, it will be propagated up at this block end.
}
# Execution never reaches this if an error occured above.
```

If an `IOError` occurs here, the file will be closed anyway,
**and the error will afterwards propagate up at the end of finally.**

**Combining rescue and finally:**

If both a rescue and finally clause are specified, on error
the `rescue` clause will run if applicable, then the
`finally` clause will run, and then execution will either 1.
resume after your `do` block if the `rescue` clause was applied,
or 2. bail out propagating the error to the caller after completing
your `finally` block:

```horse64
do {
    # ... dangerous code here...
} rescue MyDangerousError {
    # Runs on error, but only if it was a MyDangerousError.
    # Skipped if another error type occured.
} finally {
    # Runs ALWAYS, no matter if an error occurred or not,
    # and no matter if an unknown error type.
    # Runs LAST, after the rescue clause in any case.
    do_cleanup()

    # Ok, 'finally' block done!
    # Normal execution resumes from here if there wasn't ever
    # an error, or it was rescued successfully.
    # If it was NOT rescued, execution will bail out and raise the error.
}
print("Ok, let's continue")  # not reached in case of un-rescued error
```

### Scoped lifetime with cleanup (with)

The with statement provides a quicker alternative to `do`/`finally`
for objects with a special `.close()` clean-up function attribute:

```horse64
with io.open("C:\\temp\\testfile.txt", "w") as myfile {
    myfile.write("test")
}  # File is automatically closed once scope ends.
```
This is equivalent to:

```horse64
var myfile
do {
    myfile = io.open("C:\\temp\\testfile.txt, "w")
    myfile.write("test")
} finally {
    if myfile != none {
        myfile.close()
    }
}
````

Any object instance in Horse64 with a such clean-up function
attribute named `close`, the purpose of which is to trigger
a clean-up before regular destruction by the garbage collector,
can be used with a `with` statement.

As a common example, file objects from the `io` core module
have a `.close()` function.


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
This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020 Horse64 Team (See AUTHORS.md)
