
# Horse64 Origin and Design

## Origin of Horse64

Horse64 was designed as an alternative to the popular **Python** scripting
language. It shares the striving for readable, simple syntax, and
beginner-friendly design, while attempting a more grounded design
with more stability for bigger projects.

It's intended main usage is productive work on backend services,
end-user applications, and terminal tools. It works for multimedia
and 3D as well!


## Design Twist: Static Scope

The main difference of Horse64 to most scripting languages is
that Horse64 has a fully static, compile-time evaluated scope, which
usually is only found in statically typed or type inferred languages.

This gives you:

- All the simplicity of truly dynamic, annotation-free typing in
  **all** cases
- Vastly improved compile-time error detection
- No potential for bugs related to dynamic scope modification as
  is common in scripting languages.

Read more on [the practical differences to scripting languages](
    Specifications/Horse64.md#overview
), if you're interested.


## General Design Philosophy

Horse64 attempts to keep the language design simple, while versatile
enough to take on most types of projects. The standard library
attempts to cover all bases, to give a fully usable universal core.

If there were to be fixed design principles, it'd probably be some
similar to those (**work in progress**):

- **Simplicity over bloat:**
  It is preferred to make rare tasks slightly more complicated,
  to making very common tasks too easy to do in too many different ways.
  *(This should apply mostly to the language core, less so to the
  standard library.)*

- **Readability over conciseness:**
  Keeping the language approachable is usually preferred to saving
  advanced coders letters to type out, unless the impact is too grave.

- **Self-contained tooling over perfect implementation:**
  Having a less optimal implementation is preferred over adding in
  large dependencies to the core tooling.
  *(This refers e.g. to external GC libraries, LLVM, etc.)*

- **Common style is encouraged:**
  Everyone using Horse64 is encouraged to stick to the
  [Common Style Guide](Common Style Guide.md).

Since the project is still young, these points are subject to change.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
