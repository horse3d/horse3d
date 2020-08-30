
Horse64
=======

![GitHub Workflow Status Badge (branch master)](https://img.shields.io/github/workflow/status/horse64/horse64/Basic%20build%20and%20test/master?label=build%20and%20tests&style=flat-square)

Welcome to *Horse64*, a **simple & versatile programming
language** for your applications.

[![logo](misc/logo-readme.png)](https://horse64.org)

```
func main {
    print("Hello World!")
}
```

**Features:**

- **Simple.** Horse64 strives for [approachable design](
  ./docs/Design.md#overview), including its intuitive dynamic typing.

- **Self-contained.** Single-binary compiler, no complex
  toolchains or giant IDEs. (Unless you want them!) It's cross-platform
  too.

- **Solid to build on.** Helps keep your code orderly with static
  imports and compile-time identifier checks.
  Ideal for maintainable large code bases, with the accessibility
  full dynamic typing.

- **Multimedia included.** Support for 3D via OpenGL, and
  complex User Interfaces with the official `multimedia` library.

- **Open.** *Horse64* is, excluding its logo, all free software
  under BSD-like licensing. [Check the license here.](LICENSE.md)
  We also have an open [specification](./docs/Specification/Horse64.md).


Get Started
-----------

[Download Horse64.](https://horse64.org/download)


Help & Docs
-----------

* [Documentation, read it!](https://horse64.org/docs)

  [![docs screenshot](misc/README_image_docs.png)](
    https://horse64.org/docs
  )

* [Help & community chat on Matrix](
    https://matrix.to/#/+horse64:matrix.org
  )

  [![chat icon](misc/logo_README_chat.png)](
    https://matrix.to/#/+horse64:matrix.org
  )


Should I switch to Horse64?
---------------------------

This is a quick guideline for experienced programmers:

**You may want** to use Horse64 if Python/Ruby/JavaScript's clean
syntax appeals to you, but their extremely dynamic nature gives you
headaches in larger projects with either deployment, or development.

**You may NOT want** to use Horse64 if low-level memory control and
pure performance is your main concern, since Horse64 relies on bytecode
and not on compiled machine code.

There is also a more [detailed comparison](
    ./docs/Design.md#overview
) available on what Horse64 offers, and what it doesn't offer.


Advanced: Build it Yourself
---------------------------

Since Horse64 is [open-source](LICENSE.md), you can
[build it yourself](
    ./docs/horsec/horsec.md#manual-build   
) if you want to.
Please note this is an advanced task and in no way necessary to
build your application.
However, if you want to contribute to Horse64's compiler,
this might be your first step to enable you to do so.
