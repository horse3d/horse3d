
# Common Style Guide

This is the recommended **common style of code formatting** for Horse64.
While nothing of this is enforced by any of the standard tools, we
recommended to use this style to make sharing and collaborating easier
for everyone in the community.


## Whitespace and brackets

### Indentation and brackets

Indentation should be 4 spaces, and opening brackets for code blocks
(`{`) on the same line as their block element, and always separated by
a single space from what comes before:

```Horse64
func main {
    print("Hello World!")
}
```

As seen above, closing brackets (`}`) should be on their own separate
line, except when chained for `elseif`:

```Horse64
func process_command(c) {
    if c == "quit" {
        system.quit(0)
    } else if c == "ping" {
        print("pong")
    }
}
```

### Line length

To facilitate work flows for multiple panes of code and to motivate
keeping code nestings flat and simple, lines should not exceed 79
characters in length. This allows keeping two or even three files
at once on the screen, which is useful especially as projects grow
in size.

If your code can't fit well into 79 lines due to nesting, it is
recommended you try to simplify your nestings. Often, the same
task can be achieved while nesting less deeply.


### Whitespace in statements

Any opposite-facing brackets should, outside of the call and indexing
operators, be separated with a whitespace:

```Horse64
func my_func(a, b, c) {  # opposite-facing brackets ) { should be separate
    process(a + b)[5]  # .. except call and indexing operators )[
}
```

Any same-facing brackets should be kept together with no spacing:

```Horse64
func main {
    print({pos -> [x: 0, y: 0]})  # same-facing ]}) should be together
}
```

Block brackets, like `{` for function contents, should be separated from
every non-bracket item on the same line with a space. Other brackets,
like `{` or `[` for sets, maps, lists, and vectors, or '(' for calls,
should not be separated from non-bracket items like identifiers on the
same line:

```Horse64
func main {  # '{' bracket separate (block bracket!)
    var result = process({"color" -> "red"})  # '(' bracket close (call!)
    ...
}
```


## Whitespace in constructors

In any list/set/other container constructor, as well as in parameter lists,
commas and colons should be followed but not preceded by a space:

```
func my_func(a, b, c) {  # space after each comma
    print([1: 2])  # space after a colon.
}
```

The key value arrows in maps should have space on each side:

```
func my_func {
    return [1 -> 2, "test" -> 5]  # space after and before each -> arrow
}
```


## Identifiers

### Identifier naming

Identifiers that refer to **variables**, **constants**, or **functions**
should be spelled in **snake case:**

```Horse64
func process_entries {
    var entry_count = 5
    ...
}
```

Identifiers that refer to **classes** should be spelled in **camel case:**

```Horse64
func main {
    var player = new PlayerCharacter()
    player.teleport([x: 0, y: 0, z: 10])
}
```


## Other Rules

This document is continuously updated, but in the end we recommend sticking
to what `hstyle` checks. It should enforce all the rules listed here, and
then some. If there is a discrepancy between this document and `hstyle`,
please file a bug.  FIXME: add link to hstyle.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020-2021 Horse64 Team (See AUTHORS.md)*
