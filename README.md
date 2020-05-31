
Horse64
=======

![GitHub Workflow Status Badge (branch master)](https://img.shields.io/github/workflow/status/horse64/horse64/Basic%20build%20and%20test/master?label=build%20and%20tests&style=flat-square)

Welcome to *Horse64*, an **clean & comprehensive programming
language** for your applications.

[![logo](misc/logo-readme.png)](https://horse64.org)

```
func main {
    print("Hello World!")
}
```

**Features:**

- **Elegant.** Very readable syntax with simple dynamic typing,
  quick to get started in.

- **Cross-platform.** Supports Windows and Linux, with easy support
  feasible for any platform supporting OpenGL in the future.

- **Self-contained tooling.** Single-binary compiler, no complex
  toolchains or giant IDEs. (unless you want them)

- **Very maintainble.** Brings some order with static
  imports and identifier checks, while keeping lean dynamic typing.
  Ideal for maintainable large code bases despite its ease of use.

- **Easy to ship.** Single-binary output with no non-system dependencies.
  No need to add an interpreter or separate libaries, making it
  suitable for cloud & micro service use.

- **Multimedia included.** Support for 3D via OpenGL, and
  complex User Interfaces out of the box.

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

* [Help forum on Reddit](https://reddit.com/r/Horse64)

* [Help chat on Discord](https://discord.gg/pevKEKY)

  [![chat icon](misc/logo_README_chat.png)](https://discord.gg/pevKEKY)


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
https://horse64.org/INVALID-LINK-FIXME)
Please note this is an advanced task and in no way necessary to
make your application.
However, if you want to contribute to Horse64's compiler,
this might be your first step to enable you to do so.
