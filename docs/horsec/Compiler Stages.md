
# Compiler Stages

The stages of [horsec](./horsec.md), the official compiler for horse64,
are as follows:

- #1 Lexer (horse64/compiler/lexer.c)
- #2 AST Parser (horse64/compiler/astparse.c)
- #3 Scope Resolution (horse64/compiler/scoperesolver.c)
  
  This also applies storage (horse64/compiler/varstorage.c)
  to assign memory storage.
- #4 Code Generation (horse64/compiler/codegen.c)
- #5 Assembly into a binary

These stages are applied on a per file basis, initially on the file
you specify as starting point. Any `import` statement in that file will
make the AST parser request any imported files so that they also become
known, and `horse64/compiler/compileproject.c` handles then applying
all the stages to all such discovered files.

To get the output of each respective stages, use:

- `horsec get_tokens` for the raw lexer tokens
- `horsec get_ast` for the raw AST parser tree
- `horsec get_resolved_ast` for a scope resolved AST tree
- `horsec get_asm` to print the bytecode from code generation
= `horsec compile` to get an actual binary
