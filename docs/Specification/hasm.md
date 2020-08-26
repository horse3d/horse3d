
# hasm bytecode

The `horse assembler` bytecode language is what is used by the
builtin [horsevm](../Misc%20Tooling/horsevm.md). *This spec is currently
extremely unfinished, but you can find the implementation
in the [core package sources](
    ../Contributing.md#corehorse64org-package
) inside the `horse64/bytecode.h` file.*

**Generate `hasm` of program:**

Get `hasm` printed out with:

- `horsec get_asm my_program_file.h64`.

*Note: this will not output the bytecode of just that one file,
but the entire generated program.*


## Storage Types

| Storage Type     | Referenced as | Created by ... |
|------------------|---------------|----------------|
|Global function   | `f<id>`       | `BEGINFUNC`    |
|Global class      | `c<id>`       | `BEGINCLASS`   |
|Global variable   | `g<id>`       | FIXME          |
|Varattr (on class)| `a<id>`       | `VARATTR`      |


---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
