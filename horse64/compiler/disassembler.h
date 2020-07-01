#ifndef HORSE64_COMPILER_DISASSEMBLER_H_
#define HORSE64_COMPILER_DISASSEMBLER_H_

int disassembler_DumpToStdout(h64program *p);

char *disassembler_InstructionToStr(
    h64instructionany *inst
);

#endif  // HORSE64_COMPILER_DISASSEMBLER_H_
