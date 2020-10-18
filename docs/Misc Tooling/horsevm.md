
# HorseVM Runtime

The bytecode VM running [hasm](../Specification/hasm.md) as generated
by [horsec](../horsec/horsec.md) is called `horsevm`. It is part of
the `core.horse64.org` package, and implemented in C.

Binaries produced by `horsec` will include this bytecode VM, such
that the resulting program runs without dependencies. There is no need
to install any tooling on a target machine to run a Horse64 program.

## Runtime behavior

An overview over horsevm's runtime behavior:

| Feature                             | horsevm                 |
|-------------------------------------|-------------------------|
| Just-in-time compilation            | no                      |
| Recurse deeply beyond libc stack    | yes, up to full heap    |
| True concurrent OS threads          | yes, with `async`       |
| Concurrency isn't limited by GIL    | yes                     |
| Co-routine support                  | no, replaced by `async` |
| Shared memory between threads       | no                      |
| Memory safety of references         | yes                     |
| Direct C calls / FFI                | no, only C extensions   |
| Memory model                        | GC + ref-counting       |
| Local variable management           | register stack based    |
| Numbers optimized with faster type  | yes                     |
| String objects cause GC load [1]    | partial                 |
| Object instances cause GC load      | yes                     |
| List/arrays cause GC load           | yes                     |
| Threaded Garbage Collection         | no                      |
| Vararg support                      | yes                     |
| No string lookups for attributes    | yes                     |
| Operator overloading                | only for `==`/`!=`      |
| Runtime module loading & `eval()`   | no                      |
| Module and class introspection      | only very limited       |
| Transparent big int support         | no                      |


- Footnote [1]: strings are always reference counted, hence will
  not pile up work for the garbage collector later. However,
  reference counting itself also isn't necessarily cheap, and
  they will increase the graph of discoverable elements.


### Garbage Collection Implementation

Garbage collection is a background mechanism managed autonomously
by [horsevm](../Misc%20Tooling/horsevm.md) to free more complicated
memory structures.

See the [section on data lifetime](#data-lifetime-and-scopes) for when
a value is freed in general: anything passed by value, or only referenced
by one trivial reference, will be freed immediately once possible.

Only for longer reference chains or cycles, the garbage collector comes
into play. Basically, the "immediate" memory handling will usually
not detect that these chains or cycles are no longer referenced and in
use from anywhere, and leave them linger in memory. To deal with this,
the garbage collector runs occasionally to find these remains and clean
them up.

#### How does the Garbage Collector impact me?

The garbage collector is fully self-managed. You don't need to care
about when, or if it runs, since *horsevm* does all this for you.
Sadly, nothing is free, so you might experience the following downsides:

1. If a lot of such lingering constructs need to be cleaned up, micro
   stutter can happen caused by the garbage collector runs.

2. Memory usage of your program will be unnecessarily higher if you
   create a constant stream of lingering objects, since the garbage
   collector will "lag behind" with cleaning them up. The amount of
   this unnecessary use depends on how much lingering references you
   create.

3. If you rely on `on_destroy` of object instances to run additional
   clean up affecting external resources (like files), if the object
   is part of a lingering group then your `on_destroy` clean up run
   will also be delayed.

4. In general, the garbage collector's mere existence will cause an
   additional memory overhead, as well as CPU overhead when it runs.

The garbage collector is an integral part of Horse64 which makes
handling objects a lot easier, but sadly comes with above downsides.
Since the language was designed around this mechanism, there is no
way to avoid these downides: they are just listed here for
transparency's sake.

#### How does the Garbage Collector work in detail?

This will likely be outdated soon at any point where it is written
down, so we recommend you check the source code for details.

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)
