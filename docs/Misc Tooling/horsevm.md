
# HorseVM Runtime

The bytecode VM running [hasm](../Specification/hasm.md) as generated
by [horsec](../horsec/horsec.md) is called `horsevm`. It is part of
the `core.horse64.org` package, and implemented in C.

Binaries produced by `horsec` will include this bytecode VM, such
that the resulting program runs without dependencies. There is no need
to install any tooling on a target machine to run a Horse64 program.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)
