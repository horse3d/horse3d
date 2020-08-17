
# Horse64 Origin and Design

## Origin of Horse64

Horse64 was designed by [@ell1e](https://github.com/ell1e) as
an alternative to the popular **Python** scripting language.
Like Python, it strives for readable, simple syntax, and
beginner-friendly design, but it tries to be more grounded
with less surprises in bigger projects.

It's intended main usage is productive work on backend services,
end-user applications, networking, and terminal tools.
Use for multimedia and basic 3D is also possible.


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

Read more on [the practical differences to scripting languages](
    Specifications/Horse64.md#overview
), if you're interested.


## General Design Philosophy

Horse64 attempts to keep the language design simple, while versatile
enough to take on most types of projects. The standard library
attempts to cover most bases, to give a fully usable universal experience.

If there were to be design principles underlying Horse64,
it'd probably be similar to these (**work in progress**):

- **Simplicity over flexibility:**
  It is preferred to make rarer tasks more complicated,
  to making very common tasks too easy to do in too many different ways.
  If the language is as a result a bad match for some uses then that's okay.
  *(This should apply mostly to the language core, less so to the
  standard library.)*

- **Readability over conciseness:**
  Keeping the language approachable is in overall preferred to saving
  advanced programmers letters to type out, unless the impact is too grave.

- **Self-contained tooling over perfection:**
  Having a less optimal implementation is preferred over adding in
  large dependencies to core tooling. This keeps the project lean,
  independently maintainable, and portable.
  *(Large dependencies could be e.g. external GC libraries, LLVM, etc.)*

- **Common shared style is encouraged:**
  Everyone using Horse64 is encouraged to stick to the
  [Common Style Guide](./Common%20Style%20Guide.md).

Since the project is still young, these points are subject to change.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
