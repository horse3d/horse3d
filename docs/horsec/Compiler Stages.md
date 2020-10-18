
# Compiler Stages

The internal stages used by [horsec](./horsec.md), the official
compiler for horse64, are as follows:

- Stage 1: Lexer (horse64/compiler/lexer.c)

- Stage 2: AST Parser (horse64/compiler/astparse.c)

- Stage 3: Scope Resolution (horse64/compiler/scoperesolver.c)
  
  This stage also applies storage (horse64/compiler/varstorage.c)
  to assign memory storage and collects info for the final graph
  checks (horse64/compiler/threadablechecker.c).

- Stage 4: Code Generation (horse64/compiler/codegen.c)
  which spits out [hasm bytecode](../Specification/hasm.md).

  Will also apply the final graph checks, which e.g. check
  `async`/`noasync` relations. (horse64/compiler/threadablechecker.c)

- Stage 5: Assemble into a binary


These stages are applied on a per file basis, initially on the file
you specify as starting point. Any `import` statement in that file will
make the AST parser request any imported files so that they also become
known, and `horse64/compiler/compileproject.c` handles then applying
all the stages to all such discovered files.

To get the output of each respective stages, use:

- `horsec get_tokens` for the raw lexer tokens (stage 1)

- `horsec get_ast` for the raw AST parser tree (stage 2)

- `horsec get_resolved_ast` for a scope resolved AST tree (stage 3)

   **Important:** *if any file passes stage 3 without errors then
   it may still cause compilation to fail for a combined program!
   Some of the advanced checks, e.g. for `async` and `noasync`, are
   only applied in stage 4 and when processing the full project.*

- `horsec get_asm` to print the [bytecode](../Specification/hasm.md) from
   code generation. (stage 4)

   Please note the bytecode printed is for the entire program, not just
   one file. Therefore, this does all the checks a final compilation
   also goes through, and a program passing this with no errors should
   also produce a binary with no errors. (If it doesn't, please report it
   as a bug.)

- `horsec compile` to get an actual binary

---
*This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020  Horse64 Team (See AUTHORS.md)*
