
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

- **Simple.** Readable syntax with intuitive dynamic typing,
  quick to get started in.

- **Cross-platform.** Supports Windows and Linux, with easy support
  feasible for any platform supporting OpenGL in the future.

- **Self-contained tooling.** Single-binary compiler, no complex
  toolchains or giant IDEs. (unless you want them)

- **Maintainable code.** Helps keep your code orderly with static
  imports and compile-time identifier checks.
  Ideal for maintainable large code bases, with the accessibility
  full dynamic typing.

- **Easy to ship.** Single-binary output with no non-system dependencies.
  No need to add an interpreter or separate libaries, making it
  suitable for cloud & micro service use.

- **Multimedia included.** Support for 3D via OpenGL, and
  complex User Interfaces with the official `multimedia` library.

- **Open.** *Horse64* is, excluding its logo, all free software
  under BSD-like licensing. [Check the license here.](LICENSE.md)

- **Reliable.** Comfortable garbage-collection gives you
  stability without manual hand-holding.

- **Flexible.** Supports unicode identifiers, any indents
  with no significant whitespace, and LSP for wide editor choices.

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


Advanced: Build it Yourself
---------------------------

Since Horse64 is [open-source](LICENSE.md), you can
[build it yourself by following the custom build instructions.](
    ./docs/horse/horsec.md#manual-build   
)
Please note this is an advanced task and in no way necessary to
make your application.
However, if you want to contribute to Horse64's compiler,
this might be your first step to enable you to do so.
