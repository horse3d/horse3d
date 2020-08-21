
# Horse64 Specification

This specification describes the syntax, semantics, and grammar of the
Horse64 behavior, with additional notes about
[horsec](../horsec/horsec.md) and [horsevm](../Misc%20Tooling/horsevm.md)
details when relevant to language behavior.


## Overview

In overall, Horse64 is situated somewhere between a typical
scripting language like JavaScript, Python, or Lua, and a generic
managed backend programming language like C#, Java, or Go.

Here is an overview how it roughly compares:

|*Feature List*                 |Horse64 |Scripting Lang|Backend Lang      |
|-------------------------------|--------|--------------|------------------|
|Dynamically typed              |Yes     |Yes           |No                |
|Heavy duck typing              |No      |Some          |No                |
|Garbage collected              |Yes     |Yes           |Some              |
|Compiles AOT & Optimized [1]   |Yes     |Usually no    |Yes               |
|Slow dynamic scope lookups [2] |No      |Yes           |No                |
|Compile-time scope verification|Yes     |No            |Yes               |
|Runtime eval()                 |No      |Yes, trivial  |No, or non-trivial|
|Runtime module load            |No      |Yes, common   |Yes, used rarely  |
|Produces standalone binary     |Yes     |No, or tricky |Some              |
|Beginner-friendly              |Yes     |Yes           |No, or less so    |
|Dynamic REPL mode              |No      |Yes           |Some              |
|Compiler easy to include[3]    |Yes     |Yes           |No, or less so    |
|Embeddable scripting engine[4] |No      |Yes, trivially|Often non-trivial |
|Runs via compiled machine code |No      |No            |Some              |

- Footnote [1]: AOT as in "Ahead of Time", so not one-shot running of
  a script with either a simple one-pass compiler or Just-In-Time compilation,
  but rather a separate slower compilation step that produces a binary
  that is then executed later.

- Footnote [2]: This refers to whether calling a global variable, or a member
  / attribute on a class object instance will occasionally invoke a slow
  string hash name lookup at runtime.

- Footnote [3]: Easy to include compiler refers to using the compiler
  of a language from a program inside the same language as a library,
  without the need of separately installing an entire SDK.

- Footnote [4]: Embeddable scripting engine refers to using the compiler
  from inside a *different* lowlevel language, e.g. to embed it for user
  scripts in a video game written in C/C++. Horse64 is not easily suitable
  for this right now.

**To find out more, also [see the Design Motivations](../Design.md).**


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

See the respective later sections for both the detailed [grammar](
    ./Horse64%20Grammar.md#grammar), the
[constructs](#syntax-constructs) and how they work,
as well as details on [data lifetime](#data-lifetime-and-scopes),
and more.


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

This can only happen if you **both** 1. use return with no
value specified, 2. follow up a return with a statement
in the same code block/scope.

Please note the second step is *inherently nonsensical*, since
such a statement is always unreachable. Therefore, don't ever do this
and you will never face this ambiguity.


### Variable definitions and assignments (var)

Variables can be declared with any given unicode name, which
becomes their identifier.
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

**Variable identifiers in detail:**

Any name in Horse64 that isn't a known keyword like `var`
is an identifier, and must refer to some known variable. [See
later section on lifetime and scopes](#data-lifetime-and-scopes)
for how whether a variable is known is determined.

There are built-in variables, for example the pre-defined `print` function.
Refer to the standard library reference for a complete list.

There are also the special identifiers `self` and `base` which don't
refer to variables, see [later section on classes](
    #defining-custom-classes-class-new
).


### Conditionals (if, elseif, else)

Conditionals can be tested with `if` statements, the inner
code runs if the conditional evaluates to boolean value `true`.
If the conditional doesn't evaluate to a boolean, a TypeError occurs.
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


### Inline expressions

Any inline expression, which can e.g. be used in variable assignments,
conditionals if it evaluates to a boolean, function arguments in calls,
and as container argument in a `for` loop if it evaluates to a container,
can be a literal, or a combination of literals with unary or binary
operators.

Here is an example for an inline expression that is a number literal:

```horse64
5
```

Here is another example where two literals are combined with a
binary operator:

```horse64
4 > 5
```

For all possible literals, see [section on data types](#datatypes).

For all possible operators, see [section on operators](#operators).


### Loops (while, for)

Horse64 supports two loop types, a `while` loop with an
arbitrary conditional, and a `for` loop that does a for
each iteration over a container. For details on how
the conditional is evaluated, see the [section on conditionals](
    #conditionals-if-elseif-else
).

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


### Import statement (import)

The import statement allows importing global symbols like
global variable definitions, function definitions, or
class definitions from another module:

```horse64
import io from core.horse64.org

func main {
    with io.open("A test file.txt", "w") as file {
        file.write("test")
    }
}
```
*(the import makes the function named `open` available
from the `io` module)*

Usually, a file corresponds to one module. Read the
[section on modules](#modules-libraries-and-code-files)
for details.


### Defining custom classes (class, new)

Horse64 allows defining custom classes:

```horse64
class MyCircle {
    func init {
        print("Hello from my new class object!")
    }
}
```
For details on how to use them, check [Horse64 OOP
specifications](Horse64%20OOP.md).


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
[next section](#handling-errors-dorescue)) stops it,
or the original `main` is bailed out of.
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

**Built-in vs custom error types:**

For a list of built-in error types, please consult the
standard library reference. You can use any of these as you see
fit, or if none is descriptive enough for your use, you can derive
your own as a class:

```horse64
class MyParserError extends RuntimeError {}
```
Please note a custom error class may not add or override any attributes,
it must inherit the base `Error` class attributes as-is.
This is a limitation of how [horsevm](../Misc%20Tooling/horsevm.md)
handles error instances for performance reasons.

Adding custom error types is only recommended if you don't find a
built-in type to fit your use case at all.


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

It has the following datatypes: where-as "by value"
being "yes" means assigning it to a new variable will create
a copy, while "no" means it will be shared "by" reference
to the same underlying data object:

|*Data Type*  |By value|GC'ed |Literal constructor          |
|-------------|--------|------|-----------------------------|
|none         |Yes     |No    |`none`                       |
|boolean      |Yes     |No    |`true` or `false`            |
|number       |Yes     |No    |`-?[0-9]+(\.[0-9]+)?`        |
|string       |Yes     |No    |`"` then any utf-8 then `"`  |
|function     |No      |No    | see below                   |
|list         |No      |Yes   | see below, or `[]` (empty)  |
|vector       |Yes     |No    | see below                   |
|map          |No      |Yes   | see below  or `{->}` (empty)|
|set          |No      |Yes   | see below, or `{}` (empty)  |
|obj. instance|No      |Yes   | see below                   |

- **By value** *column: a "yes" here means
  assigning values of this type to a new variable will create
  a copy, while "no" means it will instead assign "by reference"
  with all created values just referring to the same underlying
  original object.*

- **GC'ed** *column: A "yes" entry means any new
  instance of the given data type will cause garbage
  collector load, with the according performance implications.*

**Literal constructors** are shown above as regexes for the
simple cases, although check the
[grammar](./Horse64%20Grammar.md#grammar) for full
details. For the more complex objects, it differs:
functions can either just be specified by identifier, or
as an inline lambda with the `=>` syntax.
Lists can be created via `[<expr1>, <expr2>, ...]`, and
vectors via `[1: <number1>, 2: <number2>, ...]`, and
maps via `[<key1> -> <value1>, <key2> -> <value2>, ...]`, and
sets via `{<expr1>, <expr2>, ...}`, again [see grammar for
details](./Horse64%20Grammar.md#grammar). Object instances
can only be created via the `new` operator, or otherwise
must be specified by identifier - there is no inline
constructor.

**Containers:**

The types `list`, `vector`, `map`, `set` are all containers
that can be used with a `for loop`, see [section on
loops](#loops-while-for). All of them can old arbitrary
items of any type (including nested containers) except
for vectors, which can only hold values of a number type.
It is valid to add two lists to each other, so cycles are
permitted.

**Runtime quirks:**

Please note there are more hidden differentiations in the
[horsevm runtime](../Misc%20Tooling/horsevm.md) which
**may be subject to future change**:

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


### Data Lifetime and Scopes

**Code blocks and Scopes:**

Any use of `{` and `}` with code statements inside, as seen
with constructs like `if`, `while`, etc., is called a code block.
Each code block has its own scope, which is a defined range where
variables defined via `var` are valid.

**Lifetime of local variables:**

A `var` statement inside a code block adds a variable to its block's
scope, and may only be used from inside that code block from
statements *after* the declaration, as well as nested inner blocks
after the declaration (since they inherit the contents of their
parent blocks' scopes at that point in the file).

A variable can be re-declared with the same name inside a nested
block's scope. This is allowed, but not recommended since it can
make it less obvious what an identifier refers to.

**When a variable's data is freed from memory:**

As soon as a variable with a data type that is always passed by
value (see [section above on data types](#datatypes)) goes out
of scope, that is execution leaves the code block where it was
defined, it will cease to exist immediately.

As soon as a variable with a data type passed by reference goes
out of scope, the reference to the underlying data object is removed.
However, the underlying data object may continue to exist if it
was passed by reference to other places that still hold a reference.
Such a reference might also be stored inside a class object instance's
var attribute, as long as that object instance is still held in a
variable itself.

**Implementation detail:** Variable types passed by reference
might generally linger in memory for a while longer, because e.g.
object instances might refer each other in a cycle. Therefore,
identifying abandoned cyclic clusters can be a non-trivial task
done by the garbage collector. However, all such clusters will
be freed from memory *eventually*.

**Global variables:**

As a special case, `var` statements can be outside of any block
at the top level. In this case, it will be added to the "global scope"
which isn't attached to any code block and stays alive forever.

Global variables can be accessed through `import`
statements from other code files, and they may also be accessed
from any code inside functions anywhere, no matter of declaration
order. (E.g. a function declared earlier in a code file can
access a global varaible declared later in the same code file.)

**Best practice for global variables:**

We recommend to not overuse global variables: they
might be accessed by any function or even other modules, hence
you might lose track of what exactly changes and depends on
a global variable if you overuse it. In complex programs, this
can make it hard to reason about side effects of anything.

Therefore, try to keep state local inside functions or on
dedicated objects where appropriate to avoid unwieldy global
side effects.


### Operators

Horse64 supports the following operators, all of which
will raise a `TypeError` if not applied to the supported
types specified:

**Math operators:**

Math operators: `+`, `-`, `*`, `/`, `%`,
bitwise math operators: `<<`, `>>`, `~`, `&`, `|`, `^`.
*(all binary operators)*

They can be applied to any pair of numbers, the
bitwise ones will round the number to a 64bit integer
first if it has a fractional part.

The `+` operator can also be applied to two strings,
and the `-` operator can also be used as a unary operator;

**Comparison operators:**

Comparison operators: `==`, `!=`, `>`, `<`, `>=`, `<=`.
*(all binary operators)*

The first two can be applied to any types, the others
to a pair of numbers, or a pair of strings which compares
them based on unicode code point value. All comparisons
evaluate to either false or true.

**Boolean operators:**

Boolean operators: `not`, `and`, `or`.
*(`not` unary, others binary operators)*

All of these can be applied to any pair of booleans,
and will return a boolean.
The `and` operator will only evaluate the left-hand side
if it turns out to be false. The `or` operator will only
evaluate the left-hand side if it turns out to be true.

**New operator:**

New operator: `new`.
*(unary operator)*

Example: `new class_name(arg1, arg2)`
This operator can be applied to an identifier that
refers to a class, or a value that was assigned from
an identifier referring to a class, combined with
arguments for the constructor.
It evaluates to an object instance, or may raise any
error caused by the `init` function attribute when run.

**Index by expression operator:**

Index by expression operator: `[` with closing `]`.
*(binary operator)*

Example: `somecontainer[<indexexpr>]`.
This operator can be applied to any container of type
list or vector, and takes a number type for indexing. It
evaluates to the respective nth list or vector item.
If the number passed in is lower than `1` or higher than
the amount of items, an `IndexError` will be raised.

**Attribute by identifier operator:**

Attribute by identifier operator: `.` (dot).
*(binary operator)*

Example: `someitem.identifier`.
The item can be any arbitrary expression, the identifier must
be an actual identifier and not any other type of expression.
The operator can be applied on any item, even numbers or
booleans or even none, but will raise an `AttributeError` if
the given data type doesn't have this attribute. All
data types have the `.as_str` attribute that returns a
string representation. For object instances of classes, the
according attributes as defined by the class are returned.
Containers and strings have the `.length` attribute which
returns the number of entries or characters respectively.

**Call operator:**

Call operator: `(` with closing `)`.
*(binary operator)*

Calls are also treated as an operator. Check the [section
on functions and calls](#functions-and-calls-func) for details.

**Operator precedences:**

If operators aren't clearly bracketed off to indicate evaluation
order, then it is determined by operator precedence.
For simplicity, precedence is presented here by pasting the
according C code of [horsec](../horsec/horsec.md):

```C
    case H64OP_ATTRIBUTEBYIDENTIFIER: return 0;
    case H64OP_INDEXBYEXPR: return 0;
    case H64OP_CALL: return 0;
    case H64OP_NEW: return 1;
    case H64OP_MATH_UNARYSUBSTRACT: return 2;
    case H64OP_MATH_BINNOT: return 3;
    case H64OP_MATH_BINAND: return 3;
    case H64OP_MATH_BINOR: return 4;
    case H64OP_MATH_BINXOR: return 5;
    case H64OP_MATH_DIVIDE: return 6;
    case H64OP_MATH_MULTIPLY: return 6;
    case H64OP_MATH_MODULO: return 6;
    case H64OP_MATH_ADD: return 7;
    case H64OP_MATH_SUBSTRACT: return 7;
    case H64OP_MATH_BINSHIFTLEFT: return 8;
    case H64OP_MATH_BINSHIFTRIGHT: return 8;
    case H64OP_CMP_EQUAL: return 9;
    case H64OP_CMP_NOTEQUAL: return 9;
    case H64OP_CMP_LARGEROREQUAL: return 10;
    case H64OP_CMP_SMALLEROREQUAL: return 10;
    case H64OP_CMP_LARGER: return 10;
    case H64OP_CMP_SMALLER: return 10;
    case H64OP_BOOLCOND_NOT: return 11;
    case H64OP_BOOLCOND_AND: return 12;
    case H64OP_BOOLCOND_OR: return 13;
```

A lower number means the operator will be evaluated first
compared to one with a higher number (or nested more deeply
inside in a syntax tree sense), and same number means it
will be evaluated left-to-right.


## Modules, Libraries, and Code Files

Horse64 allows specifying larger programs in multiple code files,
organized as modules. This section specifies how code files must
look like, and how they map to modules, and how modules are found.

### Code Files

The **file names** of all code files are expected to end in `.h64`,
and only contain characters that are a valid identifier in
the [grammar](./Horse64%20Grammar.md#grammar).
Please note this excludes space characters.

All bytes inside a code file must be valid utf-8, including in any
string literals. Having other values in string literals is possible,
but only with escaping.


### Modules

**What is a module, and where is it:**

Outside of C extensions, every .h64 code file maps to a module of
the same name.
Modules are found in two separate places: 1. local project imports for
`import` statements with no `from`, 2. external package imports for
`import` statements with a `from`.

The import path is made up from the code file names themselves, plus
the folders they're in. E.g. `mytest/myfile.h64` could be imported
with `import mytest.myfile` (or any relative variation, see local
project imports).

**When are the imports processed?**

All `import`s in Horse64 are resolved at compile time and must be
at the top level (not nested inside functions or other scopes),
and they are all baked into the resulting output binary.

Please note if you are familiar with the "Python"
programming language that is different, since there imports are
resolved at runtime and can be done inside inner scopes and functions,
and need to be present in python's so-called "site-packages" at runtime.

**Where can I get packages/libraries from?**

To use any external library, install it via [hpm](../Misc%20Tooling/hpm.md)
to your project-local `horse_modules` folder first.
[Read about hpm](../Misc%20Tooling/hpm.md) for more details.
You can also place external source code manually [where external packages
are searched](#modules-external-package-imports),
but this is not recommended.


### Modules: Local Project Imports

**Local project imports** are resolved possibly locally from the file
you import from, or alternatively from any parent folder up to the project
root. This is tested recursively descending-out-of-folders order.

Here is an example of local import resolution:

*Project tree:*
```
main.h64
mymodule1/mymodule.h64
```

*Contents of `mymodule/mymodule.h64`:*
```
import testfolder.testmodule
```
The compiler will now check the module path recursively,
by preferring relative interpretations where possible.
In the concrete case, it will check these file paths in-order:

1. `mymodule/testfolder/testmodule.h64`
2. `testfolder/testmodule.h64`

Then it will terminate with an error since neither of these exist.


### Modules: External Package Imports

**External package imports** are always tested in the
`horse_modules/` subfolder in your project root. They must match
the full non-relative module path, e.g.

```
import io from core.horse64.org
```

must match to `horse_modules/core.horse64.org/io.h64`.

Unlike local project imports, external imports don't support
any alternate relative path interpretations.


### Garbage Collection Details

FIXME: write this

---
This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020 Horse64 Team (See AUTHORS.md)
