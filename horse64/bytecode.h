#ifndef HORSE64_BYTECODE_H_
#define HORSE64_BYTECODE_H_

#include <stdint.h>

typedef enum instructiontype {
    H64INST_INVALID = 0,
    H64INST_STACKGROW = 1,
    H64INST_STACKSETCONST = 2
} instructiontype;

typedef enum valuetype {
    H64VALTYPE_INVALID = 0,
    H64VALTYPE_INT64 = 1,
    H64VALTYPE_FLOAT64 = 2
} valuetype;

__attribute__((packed)) typedef struct valuecontent {
    uint8_t valuetype;
    union {
        int64_t int_value;
        double float_value;
    };
} valuecontent;

__attribute__((packed)) typedef struct h64instruction {
    uint16_t type;
    union {
        struct stackgrow {
            int size;
        } stackgrow;
        struct stacksetconst {
            valuecontent content;
        } stacksetconst;
    };
} h64instruction;

typedef struct h64program {
    int globals_count;
    valuecontent *globals;
    
} h64program;


#endif  // HORSE64_BYTECODE_H_
