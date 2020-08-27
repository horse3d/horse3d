
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
| String objects cause GC load [1]    | no                      |
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
  reference counting itself also isn't necessarily cheap.


---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)
