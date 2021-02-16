
# Horse64 Grammar

This document attempts to describe the formal syntax of Horse64
as exact as possible.


## Grammar

**Important correctness note:** this grammar is manually maintained,
and might not always match the actual parser which you can find in the
[horsec source code](../Contributing.md#corehorse64org-package) in
the file `horse64/compiler/astparser.c`. It is intended to
match as closely as possible, therefore please [report an issue](
    ../Contributing.md#report-bugs
) if you observe any difference in the wild.

**Important note on precedence:** this grammar ignores operator precedence.
For the `operatorexpr` expansion of the `expr ::= ...` grammar rule,
you must pick the expansions with the **right-most** occurrence of the
**highest precedence number** operator that is possible to get the correct
result.
You must also always expand to an `operatorexpr` if possible, and only
to other `expr` expansions if that is not possible.
For precedence numbers, check the [operators semantics section](
    Horse64.md#operators
).

**Grammar formatting notes:**

- The grammar lists the expansion rules, starting with a `program`
  item, that produce the concrete values possible for program.

- Lists of chained expanded elements are specified as
  `(element_1, element_2, ...)`. Special characters are specified
  in single quotes, like `','` for a literal comma.

- Literal words like for keywords are specified with double quotes,
  e.g. `"import"` for the the `import` keyword.

- Multiple expansion choices are separated by a single pipe, `|`.

- Optional items are followed by a question mark, e.g. `item?`.

### Grammar listing

```

# Top-level structure:

program ::= (toplvlstmt_1, toplvlstmt_2, ...)
toplvlstmt ::= vardefnoasyncstmt | funcdefstmt | importstmt |
               classdefstmt


# Top-level statements:

vardefnoasyncstmt ::= "var" identifier | "var" identifier '=' expr
funcdefstmt ::= "func" identifier (funcprop_1, funcprop_2, ...)
                codeblock
importstmt ::= "import" identifierdotchain importlibinfo?
classdefstmt ::= "class" identifier
                 extendinfo? (classprop_1, classprop_2, ...)
                 classcodeblock


# Code blocks and general statements:

codeblock ::= '{' (stmt_1, stmt_2, ...) '}'
stmt ::= toplevelstmt | callstmt | assignstmt |
         ifstmt | whilestmt | forstmt | withstmt |
         dorescuefinallystmt | vardefstmt | asyncstmt |
         awaitstmt
vardefprops ::= "protect"? "equals"? "deprecated"?
vardefstmt ::= "var" identifier vardefprops? |
               "var" identifier vardefprops? '=' "async"? expr |
               "const" identifier vardefprops? |
               "const" identifier vardefporps? '=' "async"? expr
callstmt ::= callexpr
assignstmt ::= lvalueexpr '=' expr |
               lvalueexpr assignbinop expr |
               lvalueexpr '=' "await" expr
ifstmt ::= "if" expr codeblock elseifblocklist? elseblock?
whilestmt ::= "while" expr codeblock
forstmt ::= "for" identifier "in" expr codeblock
withstmt ::= "with" withitemlist codeblock
dorescuefinallystmt ::= "do" codeblock rescueblock? finallyblock?
returnstmt ::= "return" | "return" expr
asyncstmt ::= "async" callexpr
awaitstmt ::= "await" expr


# Detail rules for top-level statements:

importlibinfo ::= "from" identifierdotchain
identifierdotchain ::= identifierwithdot identifier
identifierwithdot ::= identifier '.'

classcodeblock ::= '{' (classattrstmt_1, classattrstmt_2, ...) '}'

classattrstmt ::= vardefnoasyncstmt | funcdefstmt

extendinfo ::= "extends" expr

classprop ::= "async" | "noasync" | "deprecated"
funcprop ::= "async" | "noasync" | "deprecated"


# Detail rules for general statements:

elseifblocklist ::= (elseifblock_1, elseifblock_2, ...)
elseifblock ::= "elseif" expr codeblock
elseblock ::= "else" codeblock

withitemlist ::= (withitem_1, withitem_2, ...) withlastitem
withitem ::= expr "as" identifier ','
withlastitem ::= expr "as" identifier

rescueblock ::= "rescue" rescuelist codeblock
rescuelist ::= (rescueitem_1, rescueitem_2, ...) rescuelastitem
rescueitem ::= expr "as" identifier ','
rescuelastitem ::= expr "as" identifier

finallyblock ::= "finally" codeblock


# Inline expressions:

expr ::= '(' expr ')' | callexpr
         callexpr | literalexpr | operatorexpr

callexpr ::= expr '(' commaexprlist kwarglist? ')'

commaexprlist ::= (commaitem_1, commaitem_2, ...) commalastitem
commaitem ::= expr ','
commalastitem ::= expr

kwarglist ::= (kwargitem_1, kwargitem_2, ...) kwarglastitem
kwargitem ::= identifier '=' expr ','
kwarglastitem ::= identifier '=' expr

literalexpr ::= "none" | "yes" | "no" | numberliteral |
                stringliteral | containerexpr

containerexpr ::= setexpr | mapexpr | listexpr | vectorexpr
listexpr ::= '[' commaexprlist ']'
setexpr ::= '{' commaexprlist '}'
mapexpr ::= '{' mapitemlist '}'
mapitemlist ::= (mapitem_1, mapitem_2, ...) maplastitem
mapitem ::= expr '->' expr ','
maplastitem ::= expr '->' expr
vectorexpr ::= '[' vectoritemlist ']'
vectoritemlist ::= (vectoritem_1, vectoritem_2, ...) vectorlastitem
vectoritem ::= numberliteral ':' expr ','
vectorlastitem ::= numberliteral ':' expr

operatorexpr ::= binopexpr | unopexpr
binopexpr ::= expr binop expr
unopexpr ::= unop expr

givenexpr ::= "given" expr "then" '(' expr "else" expr ')'

```

### A few missing rules in writing

`assignbinop` can be `+=`, `-=`, `*=`, and `/=`. Assignments
with these assignment math operators are just a short hand,
e.g. `lvalue += expr` means `lvalue = lvalue + expr`.

`binopexpr` ignores that the index by expression binary operator
has a closing element `']'` afterwards.
You must also exclude the call binary operator `(` from the expansion
choices of `binop`, since that one is handled by `callexpr`.

`lvalueexpr` is a special expression that can be either a `binopexpr`
using the index by expression operator, or the attribute by identifier
operator, or a plain `identifier`. It cannot be any other expression.

`vectorexpr` has some rules omitted above for brevity, e.g.
the numbers need to start with `1` and count up:
`[1: <expr>, 2: <expr>, ...]`. You can also optionally
specify xyzw for the first three items, e.g. `[x: <expr>, y: <expr>]`.

`numberliteral` can be anything that matches any of these regexes:
```
-?[0-9]+(\.[0-9]+)?
0x[0-9a-f]+
0b[01]+
```

`stringliteral` can be anything that matches any of these regexes:
```
"([^"]|\\")"
'([^']\\')'
```

`identifiers` can be any utf-8 sequence starting with
either: 1. `_`, 2. `a-z`, `A-Z`, or 3. any non-whitespace
code point outside of the ASCII range.
This sequence is resumed by more utf-8 characters of that same set of
choices, or additionally 4. `0-9` any digit, and terminated once
the character falls out of that range.

For any `callexpr`, the last positional argument (the last item
in the comma separated list preceding optional following keyword
arguments) may be prefixed by `unpack`.


### Whitespace rules

Whitespace in Horse64 must be inserted wherever
an item would otherwise merge with a previous one, e.g. between
a keyword followed by an identifier. You can pick a space,
line break, or tab, for those situations. Other uses of whitespace
are optional since Horse64 doesn't have significant whitespace of
any kind.


---
This documentation is CC-BY-SA-4.0 licensed.
( https://creativecommons.org/licenses/by-sa/4.0/ )
Copyright (C) 2020-2021 Horse64 Team (See AUTHORS.md)
