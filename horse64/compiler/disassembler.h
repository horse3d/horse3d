// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_COMPILER_DISASSEMBLER_H_
#define HORSE64_COMPILER_DISASSEMBLER_H_

typedef struct h64program h64program;
typedef struct h64instructionany h64instructionany;

int disassembler_DumpToStdout(h64program *p);

char *disassembler_InstructionToStr(
    h64instructionany *inst
);

#endif  // HORSE64_COMPILER_DISASSEMBLER_H_
