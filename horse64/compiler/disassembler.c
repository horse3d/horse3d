
#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "compiler/disassembler.h"
#include "compiler/globallimits.h"

typedef struct dinfo {
    void (*pr)(const char *s);
    int tostdout;
} dinfo;

static inline int disassembler_Write(
        dinfo *di, char *str, ...
        ) {
    int buflen = 4096 + strlen(str);
    char *buffer = malloc(buflen);
    if (!buffer) {
        return 0;
    }
    va_list args;
    va_start(args, str);
    vsnprintf(buffer, buflen - 1, str, args);
    buffer[buflen - 1] = '\0';
    va_end(args);
    if (di->tostdout)
        printf("%s", buffer);
    else if (di->pr)
        di->pr(buffer);
    return 1;
}

char *disassembler_DumpValueContent(valuecontent *vs) {
    char buf[128];
    switch (vs->type) {
    case H64VALTYPE_INT64:
        snprintf(buf, sizeof(buf) - 1,
            "%" PRId64, vs->int_value);
        return strdup(buf);
    case H64VALTYPE_FLOAT64:
        snprintf(buf, sizeof(buf) - 1,
            "%f", vs->float_value);
        return strdup(buf);
    default:
        return strdup("<unknown valuecontent type>");
    }
}

int disassembler_PrintInstruction(
        dinfo *di, h64instructionany *inst
        ) {
    switch (inst->type) {
    case H64INST_SETCONST: ;
        h64instruction_setconst *inst_setconst =
            (h64instruction_setconst*)inst;
        char *s = disassembler_DumpValueContent(
            &inst_setconst->content
        );
        if (!s)
            return 0;
        if (!disassembler_Write(di,
                "    %s t%d %s\n",
                bytecode_InstructionTypeToStr(inst->type),
                inst_setconst->slot,
                s)) {
            free(s);
            return 0;
        }
        free(s);
        return 1;
    case H64INST_GETGLOBAL: ;
        h64instruction_getglobal *inst_getglobal =
            (h64instruction_getglobal *)inst;
        if (!disassembler_Write(di,
                "    %s t%d g" PRId64 "\n",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getglobal->slotto,
                (int64_t)inst_getglobal->globalfrom)) {
            return 0;
        }
        return 1;
    case H64INST_GETFUNC: ;
        h64instruction_getfunc *inst_getfunc =
            (h64instruction_getfunc *)inst;
        if (!disassembler_Write(di,
                "    %s t%d f" PRId64 "\n",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getfunc->slotto,
                (int64_t)inst_getfunc->funcfrom)) {
            return 0;
        }
        return 1;
    case H64INST_GETCLASS: ;
        h64instruction_getclass *inst_getclass =
            (h64instruction_getclass *)inst;
        if (!disassembler_Write(di,
                "    %s t%d c" PRId64 "\n",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getclass->slotto,
                (int64_t)inst_getclass->classfrom)) {
            return 0;
        }
        return 1;
    case H64INST_VALUECOPY: ;
        h64instruction_valuecopy *inst_vcopy =
            (h64instruction_valuecopy *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d\n",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_vcopy->slotto,
                (int)inst_vcopy->slotfrom)) {
            return 0;
        }
        return 1;
    case H64INST_BINOP: ;
        h64instruction_binop *inst_binop =
            (h64instruction_binop *)inst;
        if (!disassembler_Write(di,
                "    %s t%d %s t%d t%d\n",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_binop->slotto,
                operator_OpTypeToStr(inst_binop->optype),
                (int)inst_binop->arg1slotfrom,
                (int)inst_binop->arg2slotfrom)) {
            return 0;
        }
        return 1;
    default:
        if (!disassembler_Write(di,
                "    %s <unknownargs>\n",
                bytecode_InstructionTypeToStr(inst->type))) {
            return 0;
        }
        return 1;
    }
}

int disassembler_Dump(
        dinfo *di, h64program *p
        ) {
    assert(p != NULL);
    int i = 0;
    while (i < p->func_count) {
        char clsinfo[64] = "";
        if (p->func[i].associated_class_index >= 0) {
            snprintf(
                clsinfo, sizeof(clsinfo) - 1,
                "cls%" PRId64,
                (int64_t)p->func[i].associated_class_index
            );
        }
        char cfuncref[H64LIMIT_IDENTIFIERLEN * 2] = "";
        if (p->func[i].iscfunc) {
            snprintf(cfuncref, sizeof(cfuncref) - 1,
                " %s", p->func[i].cfunclookup);
        }
        char linebuf[1024 + H64LIMIT_IDENTIFIERLEN] = "";
        snprintf(linebuf, sizeof(linebuf) - 1,
            "%sFUNC%s %" PRId64 " %d %d%s%s",
            (p->func[i].iscfunc ? "C" : ""),
            (p->func[i].iscfunc ? "REF" : ""),
            (int64_t)i,
            p->func[i].input_stack_size,
            p->func[i].inner_stack_size,
            cfuncref, clsinfo
        );
        char symbolinfo[H64LIMIT_IDENTIFIERLEN * 2 + 1024] = "";
        if (p->symbols) {
            h64funcsymbol *fsymbol = h64debugsymbols_GetFuncSymbolById(
                p->symbols, i
            );
            h64modulesymbols *msymbols = (
                h64debugsymbols_GetModuleSymbolsByFuncId(
                    p->symbols, i
                ));
            if (fsymbol && msymbols && !p->func[i].iscfunc) {
                char spaces[1024] = "";
                while (strlen(spaces) < sizeof(spaces) - 1 &&
                        strlen(spaces) < strlen(linebuf)) {
                    spaces[strlen(spaces) + 1] = '\0';
                    spaces[strlen(spaces)] = ' ';
                }
                snprintf(
                    symbolinfo, sizeof(symbolinfo) - 1,
                    "    # name=\"%s\" module=\"%s\" library=\"%s\"\n"
                    "%s    # arg_count=%d closure_bound=%d",
                    (fsymbol->name ? fsymbol->name : "(unnamed)"),
                    (msymbols->module_path ? msymbols->module_path :
                     "$$builtin"),
                    (msymbols->library_name ? msymbols->library_name :
                     ""), spaces,
                    fsymbol->arg_count, fsymbol->closure_bound_count);
            }
        }
        if (!disassembler_Write(di,
                "%s%s\n", linebuf, symbolinfo))
            return 0;
        if (p->func[i].iscfunc) {
            i++;
            continue;
        }
        char *instp = (char *)p->func[i].instructions;
        int64_t lenleft = (int64_t)p->func[i].instructions_bytes;
        while (lenleft > 0) {
            if (!disassembler_PrintInstruction(
                    di, (h64instructionany*)instp
                    )) {
                return 0;
            }
            size_t ilen = h64program_PtrToInstructionSize(instp);
            instp += (int64_t)ilen;
            lenleft -= (int64_t)ilen;
        }
        if (!disassembler_Write(di,
                "ENDFUNC\n"
                ))
            return 0;
        i++;
    }
    return 1;
}

int disassembler_DumpToStdout(h64program *p) {
    dinfo di;
    memset(&di, 0, sizeof(di));
    di.tostdout = 1;
    return disassembler_Dump(&di, p);
}
