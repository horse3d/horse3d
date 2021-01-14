
# Horse64 Origin and Design

## Origin of Horse64

Horse64 was started by [@ell1e](https://github.com/ell1e) as
an alternative to the popular **Python** scripting language.
Like Python, it strives for readable, simple syntax, and
beginner-friendly design, but it tries to be more grounded
with less surprises in bigger projects.

It's intended main usage is productive work on backend services,
end-user applications, networking, and terminal tools.
Use for multimedia and basic 3D is also possible.


## Overview

In overall, Horse64 is designed to be somewhere between a typical
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
|Beginner-friendly syntax[3]    |Yes     |Yes           |No, or less so    |
|Dynamic REPL mode              |No      |Yes           |Some              |
|Compiler easy to include[4]    |Yes     |Yes           |No, or less so    |
|Embeddable scripting engine[5] |No      |Yes, trivially|Often non-trivial |
|Runs via compiled machine code |No      |No            |Some              |

- Footnote [1]: AOT as in "Ahead of Time", so not one-shot running of
  a script with either a simple one-pass compiler or Just-In-Time compilation,
  but rather a separate slower compilation step that produces a binary
  that is then executed later.

- Footnote [2]: This refers to whether calling a global variable, or a member
  / attribute on a class object instance will occasionally invoke a slow
  string hash name lookup at runtime.

- Footnote [3]: Beginner-friendly syntax here refers mostly to favoring
  fully spelled out words and simple constructs over more compressed,
  arcane special character syntaxes (which often can be harder to learn).

- Footnote [4]: Easy to include compiler refers to using the compiler
  of a language from a program inside the same language as a library,
  without the need of separately installing an entire SDK.

- Footnote [5]: Embeddable scripting engine refers to using the compiler
  from inside a *different* lowlevel language, e.g. to embed it for user
  scripts in a video game written in C/C++. Horse64 is not easily suitable
  for this right now.


**There are also [detailed specifications](./Specification/Horse64.md)
available.**


## Design Twist: Static Scope

The main difference of Horse64 to most scripting languages is
that Horse64 has a fully static, compile-time evaluated scope, which
usually is only found in statically typed or type inferred languages.

This gives you:

- The simplicity of truly dynamic, annotation-free typing in
  *all* cases (unlike type inferred languages that only give it for
  *most*),
- vastly improved compile-time error detection,
- no potential for bugs related to dynamic scope modification as
  is common in scripting languages.


## General Design Philosophy

Horse64 attempts to keep the language design simple, while versatile
enough to take on most types of projects. The standard library
attempts to cover most bases, to give a fully usable universal experience.

If there were to be design principles underlying Horse64,
it'd probably be similar to these (**work in progress**):

### Design goal: Simplicity over flexibility:

It is preferred to make rarer tasks more complicated,
to making very common tasks too easy to do in too many different ways.
There should preferrably one obvious way to do something, such that
people understand each others' code without learning a needless pile of
syntax variations.
If the language is as a result a bad match for some uses then that's okay.

*(This goal should apply mostly to the language core, less so to the
standard library.)*

### Design goal: Readability over conciseness:

Keeping the language approachable is in overall preferred to saving
advanced programmers letters to type out, unless the impact is too grave.

*(As a practical consequence, Horse64 often uses a keyword instead of
a short symbol for various operators.)*

### Design goal: Self-contained tooling over perfection:

Having a less optimal implementation is preferred over adding in
large dependencies to core tooling. This keeps the project more
technology independent, and therefore hopefully more portable.

*(Large dependencies could be e.g. external GC libraries, LLVM, etc.
Horse64's core avoids those.)*

### Design goal: Common shared style is encouraged

Everyone using Horse64 is encouraged to stick to the
[Common Style Guide](./Common%20Style%20Guide.md).

### Design goal: We shouldn't forget nothing is perfect

Okay, this should be obvious and it's also more of a community goal,
but I think it's important. Please don't give people a hard time if
they disagree. Sometimes compromises might work even if suggestions
seem outlandish compared to what was done so far. It's worth
hearing people out, in hopefully a welcoming manner.


### Future design changes

Since the project is still young, these points are subject to change.
Feel free to leave feedback on them and make suggestions.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
