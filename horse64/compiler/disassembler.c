// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytecode.h"
#include "debugsymbols.h"
#include "compiler/disassembler.h"
#include "compiler/globallimits.h"
#include "compiler/operator.h"
#include "widechar.h"

typedef struct dinfo dinfo;
struct dinfo {
    int (*pr)(dinfo *di, const char *s, void *userdata);
    int tostdout;
    void *userdata;
};

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
    if (di->tostdout) {
        printf("%s", buffer);
    } else if (di->pr) {
        if (!di->pr(di, buffer, di->userdata)) {
            free(buffer);
            return 0;
        }
    }
    free(buffer);
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
    case H64VALTYPE_BOOL:
        if (vs->int_value)
            return strdup("true");
        return strdup("false");
    case H64VALTYPE_NONE:
        return strdup("none");
    case H64VALTYPE_CONSTPREALLOCSTR:
    case H64VALTYPE_SHORTSTR: ;
        int alloclen = -1;
        int totallen = -1;
        if (vs->type == H64VALTYPE_CONSTPREALLOCSTR) {
            totallen = (int)vs->constpreallocstr_len;
        } else {
            totallen = (int)vs->shortstr_len;
        }
        alloclen = totallen * 12 + 3;  // 1x int32 char -> \uXXXXXXXXXX
        char *outbuf = malloc(alloclen);
        outbuf[0] = '\"';
        int outfill = 1;
        int k = 0;
        while (k < totallen) {
            uint64_t c = 0;
            if (vs->type == H64VALTYPE_CONSTPREALLOCSTR) {
                c = vs->constpreallocstr_value[k];
            } else {
                c = vs->shortstr_value[k];
            }
            assert(c < INT32_MAX);
            if (outfill + 16 >= alloclen) {
                char *newoutbuf = realloc(
                    outbuf, outfill + 64
                );
                if (!newoutbuf) {
                    free(outbuf);
                    return NULL;
                }
                outbuf = newoutbuf;
                alloclen = outfill + 64;
            }
            if (c == '"' || c == '\\') {
                outbuf[outfill] = '\\';
                outfill++;
            }
            if (c < 32) {
                char numesc[6];
                snprintf(numesc, sizeof(numesc) - 1,
                    "%x", (int)c);
                outbuf[outfill] = '\\';
                outfill++;
                outbuf[outfill] = 'x';
                outfill++;
                if (strlen(numesc) < 2) {
                    outbuf[outfill] = '0';
                    outfill++;
                }
                memcpy(outbuf + outfill, numesc, strlen(numesc));
                outfill += strlen(numesc);
            } else if (c >= 127) {
                char numesc[15];
                snprintf(numesc, sizeof(numesc) - 1,
                    "\\u" "%" PRId64, (int64_t)c);
                memcpy(outbuf + outfill, numesc, strlen(numesc));
                outfill += strlen(numesc);
            } else {
                outbuf[outfill] = c;
                outfill++;
            }
            k++;
        }
        outbuf[outfill] = '"';
        outfill++;
        outbuf[outfill] = '\0';
        char *result = strdup(outbuf);  // shrink allocation
        free(outbuf);
        return result;
    default:
        return strdup("<unknown valuecontent type>");
    }
}

int disassembler_PrintInstruction(
        dinfo *di, h64instructionany *inst, ptrdiff_t offset
        ) {
    switch (inst->type) {
    case H64INST_SETCONST: {
        h64instruction_setconst *inst_setconst =
            (h64instruction_setconst*)inst;
        char *s = disassembler_DumpValueContent(
            &inst_setconst->content
        );
        if (!s)
            return 0;
        if (!disassembler_Write(di,
                "    %s t%d %s",
                bytecode_InstructionTypeToStr(inst->type),
                inst_setconst->slot,
                s)) {
            free(s);
            return 0;
        }
        free(s);
        break;
    }
    case H64INST_GETGLOBAL: {
        h64instruction_getglobal *inst_getglobal =
            (h64instruction_getglobal *)inst;
        if (!disassembler_Write(di,
                "    %s t%d g%" PRId64 "",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getglobal->slotto,
                (int64_t)inst_getglobal->globalfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_SETGLOBAL: {
        h64instruction_setglobal *inst_setglobal =
            (h64instruction_setglobal *)inst;
        if (!disassembler_Write(di,
                "    %s g%" PRId64 " t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int64_t)inst_setglobal->globalto,
                (int)inst_setglobal->slotfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_SETBYINDEXEXPR: {
        h64instruction_setbyindexexpr *inst_setbyindexexpr =
            (h64instruction_setbyindexexpr *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_setbyindexexpr->slotobjto,
                (int)inst_setbyindexexpr->slotindexto,
                (int)inst_setbyindexexpr->slotvaluefrom)) {
            return 0;
        }
        break;
    }
    case H64INST_SETBYATTRIBUTENAME: {
        h64instruction_setbyattributename *inst_setbyattributename =
            (h64instruction_setbyattributename *)inst;
        if (!disassembler_Write(di,
                "    %s t%d n%" PRId64 " t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_setbyattributename->slotobjto,
                (int64_t)inst_setbyattributename->nameidx,
                (int)inst_setbyattributename->slotvaluefrom)) {
            return 0;
        }
        break;
    }
    case H64INST_SETBYATTRIBUTEIDX: {
        h64instruction_setbyattributeidx *inst_setbyattributeidx =
            (h64instruction_setbyattributeidx *)inst;
        if (!disassembler_Write(di,
                "    %s t%d a%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_setbyattributeidx->slotobjto,
                (int)inst_setbyattributeidx->varattrto,
                (int)inst_setbyattributeidx->slotvaluefrom)) {
            return 0;
        }
        break;
    }
    case H64INST_GETFUNC: {
        h64instruction_getfunc *inst_getfunc =
            (h64instruction_getfunc *)inst;
        if (!disassembler_Write(di,
                "    %s t%d f%" PRId64 "",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getfunc->slotto,
                (int64_t)inst_getfunc->funcfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_GETCLASS: {
        h64instruction_getclass *inst_getclass =
            (h64instruction_getclass *)inst;
        if (!disassembler_Write(di,
                "    %s t%d c%" PRId64 "",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getclass->slotto,
                (int64_t)inst_getclass->classfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_VALUECOPY: {
        h64instruction_valuecopy *inst_vcopy =
            (h64instruction_valuecopy *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_vcopy->slotto,
                (int)inst_vcopy->slotfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_BINOP: {
        h64instruction_binop *inst_binop =
            (h64instruction_binop *)inst;
        if (!disassembler_Write(di,
                "    %s t%d %s t%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_binop->slotto,
                operator_OpTypeToStr(inst_binop->optype),
                (int)inst_binop->arg1slotfrom,
                (int)inst_binop->arg2slotfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_UNOP: {
        h64instruction_unop *inst_unop =
            (h64instruction_unop *)inst;
        if (!disassembler_Write(di,
                "    %s t%d %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_unop->slotto,
                operator_OpTypeToStr(inst_unop->optype),
                (int)inst_unop->argslotfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_CALL: {
        h64instruction_call *inst_call =
            (h64instruction_call *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d %d %d %d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_call->returnto,
                (int)inst_call->slotcalledfrom,
                (int)inst_call->posargs,
                (int)inst_call->kwargs,
                (int)inst_call->flags)) {
            return 0;
        }
        break;
    }
    case H64INST_CALLIGNOREIFNONE: {
        h64instruction_callignoreifnone *inst_calliin =
            (h64instruction_callignoreifnone *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d %d %d %d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_calliin->returnto,
                (int)inst_calliin->slotcalledfrom,
                (int)inst_calliin->posargs,
                (int)inst_calliin->kwargs,
                (int)inst_calliin->flags)) {
            return 0;
        }
        break;
    }
    case H64INST_SETTOP: {
        h64instruction_settop *inst_settop =
            (h64instruction_settop *)inst;
        if (!disassembler_Write(di,
                "    %s %d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_settop->topto)) {
            return 0;
        }
        break;
    }
    case H64INST_CALLSETTOP: {
        h64instruction_callsettop *inst_callsettop =
            (h64instruction_callsettop *)inst;
        if (!disassembler_Write(di,
                "    %s %d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_callsettop->topto)) {
            return 0;
        }
        break;
    }
    case H64INST_RETURNVALUE: {
        h64instruction_returnvalue *inst_returnvalue =
            (h64instruction_returnvalue *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_returnvalue->returnslotfrom)) {
            return 0;
        }
        break;
    }
    case H64INST_JUMPTARGET: {
        h64instruction_jumptarget *inst_jumptarget =
            (h64instruction_jumptarget *)inst;
        if (!disassembler_Write(di,
                "    %s j%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_jumptarget->jumpid)) {
            return 0;
        }
        break;
    }
    case H64INST_CONDJUMP: {
        h64instruction_condjump *inst_condjump =
            (h64instruction_condjump *)inst;
        if (!disassembler_Write(di,
                "    %s %s%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (inst_condjump->jumpbytesoffset >= 0 ? "+" : ""),
                (int)inst_condjump->jumpbytesoffset,
                inst_condjump->conditionalslot)) {
            return 0;
        }
        break;
    }
    case H64INST_JUMP: {
        h64instruction_jump *inst_jump =
            (h64instruction_jump *)inst;
        if (!disassembler_Write(di,
                "    %s %s%d",
                bytecode_InstructionTypeToStr(inst->type),
                (inst_jump->jumpbytesoffset >= 0 ? "+" : ""),
                (int)inst_jump->jumpbytesoffset)) {
            return 0;
        }
        break;
    }
    case H64INST_GETATTRIBUTEBYNAME: {
        h64instruction_getattributebyname *inst_getattributebyname =
            (h64instruction_getattributebyname *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d n%" PRId64,
                bytecode_InstructionTypeToStr(inst->type),
                inst_getattributebyname->slotto,
                inst_getattributebyname->objslotfrom,
                inst_getattributebyname->nameidx)) {
            return 0;
        }
        break;
    }
    case H64INST_PUSHRESCUEFRAME: {
        h64instruction_pushrescueframe *inst_pushrescueframe =
            (h64instruction_pushrescueframe *)inst;
        char sloterrorbuf[64] = "none";
        if (inst_pushrescueframe->sloterrorto >= 0) {
            snprintf(
                sloterrorbuf, sizeof(sloterrorbuf) - 1,
                "t%d", inst_pushrescueframe->sloterrorto
            );
        }
        char catchjumpbuf[64] = "none";
        if ((inst_pushrescueframe->mode & RESCUEMODE_JUMPONRESCUE) != 0) {
            snprintf(
                catchjumpbuf, sizeof(catchjumpbuf) - 1,
                "%s%d",
                (inst_pushrescueframe->jumponrescue >= 0 ? "+" : ""),
                (int)inst_pushrescueframe->jumponrescue
            );
        }
        char finallyjumpbuf[64] = "none";
        if ((inst_pushrescueframe->mode & RESCUEMODE_JUMPONFINALLY) != 0) {
            snprintf(
                finallyjumpbuf, sizeof(finallyjumpbuf) - 1,
                "%s%d",
                (inst_pushrescueframe->jumponfinally >= 0 ? "+" : ""),
                (int)inst_pushrescueframe->jumponfinally
            );
        }
        if (!disassembler_Write(di,
                "    %s cf%d %d %s %s %s",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_pushrescueframe->frameid,
                (int)inst_pushrescueframe->mode,
                sloterrorbuf, catchjumpbuf, finallyjumpbuf
                )) {
            return 0;
        }
        break;
    }
    case H64INST_ADDRESCUETYPEBYREF: {
        h64instruction_addrescuetypebyref *inst_addrescuetypebyref =
            (h64instruction_addrescuetypebyref *)inst;
        if (!disassembler_Write(di,
                "    %s cf%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_addrescuetypebyref->frameid,
                (int)inst_addrescuetypebyref->slotfrom
                )) {
            return 0;
        }
        break;
    }
    case H64INST_ADDRESCUETYPE: {
        h64instruction_addrescuetype *inst_addrescuetype =
            (h64instruction_addrescuetype *)inst;
        if (!disassembler_Write(di,
                "    %s cf%d c%" PRId64,
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_addrescuetype->frameid,
                (int64_t)inst_addrescuetype->classid
                )) {
            return 0;
        }
        break;
    }
    case H64INST_POPRESCUEFRAME: {
        h64instruction_poprescueframe *inst_poprescueframe =
            (h64instruction_poprescueframe *)inst;
        if (!disassembler_Write(di,
                "    %s cf%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_poprescueframe->frameid
                )) {
            return 0;
        }
        break;
    }
    case H64INST_JUMPTOFINALLY: {
        h64instruction_jumptofinally *inst_jumptofinally =
            (h64instruction_jumptofinally *)inst;
        if (!disassembler_Write(di,
                "    %s cf%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_jumptofinally->frameid
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWLIST: {
        h64instruction_newlist *inst_newlist =
            (h64instruction_newlist *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newlist->slotto
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWSET: {
        h64instruction_newset *inst_newset =
            (h64instruction_newset *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newset->slotto
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWVECTOR: {
        h64instruction_newvector *inst_newvector =
            (h64instruction_newvector *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newvector->slotto
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWMAP: {
        h64instruction_newmap *inst_newmap =
            (h64instruction_newmap *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newmap->slotto
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWINSTANCEBYREF: {
        h64instruction_newinstancebyref *inst_newibyref =
            (h64instruction_newinstancebyref *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newibyref->slotto,
                (int)inst_newibyref->classtypeslotfrom
                )) {
            return 0;
        }
        break;
    }
    case H64INST_NEWINSTANCE: {
        h64instruction_newinstance *inst_newinst =
            (h64instruction_newinstance *)inst;
        if (!disassembler_Write(di,
                "    %s t%d c%" PRId64,
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_newinst->slotto,
                (int64_t)inst_newinst->classidcreatefrom
                )) {
            return 0;
        }
        break;
    }
    case H64INST_GETCONSTRUCTOR: {
        h64instruction_getconstructor *inst_getct =
            (h64instruction_getconstructor *)inst;
        if (!disassembler_Write(di,
                "    %s t%d t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_getct->slotto,
                (int)inst_getct->objslotfrom
                )) {
            return 0;
        }
        break;
    }
    case H64INST_AWAITITEM: {
         h64instruction_awaititem *inst_awi =
            (h64instruction_awaititem *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_awi->objslotawait
                )) {
            return 0;
        }
        break;
    }
    case H64INST_CREATEPIPE: {
         h64instruction_createpipe *inst_createpipe =
            (h64instruction_createpipe *)inst;
        if (!disassembler_Write(di,
                "    %s t%d",
                bytecode_InstructionTypeToStr(inst->type),
                (int)inst_createpipe->objpipeto
                )) {
            return 0;
        }
        break;
    }
    case H64INST_HASATTRJUMP: {
         h64instruction_hasattrjump *inst_hattrj =
            (h64instruction_hasattrjump *)inst;
        if (!disassembler_Write(di,
                "    %s %s%d t%d n%" PRId64,
                bytecode_InstructionTypeToStr(inst->type),
                (inst_hattrj->jumpbytesoffset >= 0 ? "+" : ""),
                (int)inst_hattrj->jumpbytesoffset,
                (int)inst_hattrj->slotvaluecheck,
                (int64_t)inst_hattrj->nameidxcheck
                )) {
            return 0;
        }
        break;
    }
    default:
        if (!disassembler_Write(di,
                "    %s <unknownargs>",
                bytecode_InstructionTypeToStr(inst->type))) {
            return 0;
        }
        break;
    }
    if (offset >= 0) {
        if (!disassembler_Write(di,
                "  # offset=%" PRId64 "\n",
                (int64_t)offset)) {
            return 0;
        }
    } else {
        if (!disassembler_Write(di,
                "\n"
                )) {
            return 0;
        }
    }
    return 1;
}

static int disassembler_AppendToStrCallback(
        ATTR_UNUSED dinfo *di, const char *print_s,
        void *userdata
        ) {
    char **s = userdata;
    int oldlen = ((*s) != NULL ? strlen(*s) : 0);
    char *new_s = realloc(
        *s,
        oldlen + strlen(print_s) + 1
    );
    if (!new_s) {
        return 0;
    }
    *s = new_s;
    memcpy((*s) + oldlen, print_s, strlen(print_s) + 1);
    return 1;
}

char *disassembler_InstructionToStr(
        h64instructionany *inst
        ) {
    dinfo di;
    memset(&di, 0, sizeof(di));
    char *s = NULL;
    di.pr = &disassembler_AppendToStrCallback;
    di.userdata = &s;
    int result = disassembler_PrintInstruction(&di, inst, -1);
    if (!result) {
        free(s);
        return NULL;
    }

    // Trim trailing comments:
    char in_escaped = '\0';
    int i = 0;
    while (i < (int)strlen(s)) {
        if (in_escaped == '\0' && (
                s[i] == '\'' ||
                s[i] == '"')) {
            in_escaped = s[i];
        } else if (in_escaped != '\0' &&
                s[i] == in_escaped) {

        } else if (in_escaped == '\0' && s[i] == '#') {
            s[i] = '\0';
            break;
        }
        i++;
    }

    // Trim away whitespace:
    i = (int)strlen(s) - 1;
    while (i >= 0 && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' ||
            s[i] == '\r')) {
        s[i] = '\0';
        i--;
    }
    int leadingwhitespace = 0;
    i = 0;
    while (i < (int)strlen(s) && (s[i] == '\t' || s[i] == ' ')) {
        i++;
        leadingwhitespace++;
    }
    if (leadingwhitespace > 0)
        memmove(
            s, s + leadingwhitespace,
            strlen(s) + 1 - leadingwhitespace
        );
    return s;
}

int disassembler_Dump(
        dinfo *di, h64program *p
        ) {
    assert(p != NULL);
    if (p->main_func_index >= 0) {
        if (!disassembler_Write(di,
                "MAINFUNC f%d\n", p->main_func_index))
            return 0;
    }
    if (p->add_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " add\n",
                p->add_name_index))
            return 0;
    }
    if (p->as_str_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " as_str\n",
                p->as_str_name_index))
            return 0;
    }
    if (p->to_str_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " to_str\n",
                p->to_str_name_index))
            return 0;
    }
    if (p->len_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " len\n",
                p->len_name_index))
            return 0;
    }
    if (p->init_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " init\n",
                p->init_name_index))
            return 0;
    }
    if (p->on_destroy_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " on_destroy\n",
                p->on_destroy_name_index))
            return 0;
    }
    if (p->equals_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " equals\n",
                p->equals_name_index))
            return 0;
    }
    if (p->to_hash_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " to_hash\n",
                p->to_hash_name_index))
            return 0;
    }
    if (p->add_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " to_hash\n",
                p->add_name_index))
            return 0;
    }
    if (p->del_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " del\n",
                p->del_name_index))
            return 0;
    }
    if (p->is_a_name_index >= 0) {
        if (!disassembler_Write(di,
                "NAMEIDX n%" PRId64 " is_a\n",
                p->is_a_name_index))
            return 0;
    }
    int64_t i = 0;
    while (i < p->classes_count) {
        char symbolinfo[H64LIMIT_IDENTIFIERLEN * 2 + 1024] = "";
        if (p->symbols) {
            h64classsymbol *csymbol = h64debugsymbols_GetClassSymbolById(
                p->symbols, i
            );
            h64modulesymbols *msymbols = (
                h64debugsymbols_GetModuleSymbolsByClassId(
                    p->symbols, i
                ));
            if (csymbol && msymbols) {
                snprintf(
                    symbolinfo, sizeof(symbolinfo) - 1,
                    "\n    # Name: \"%s\" Module: %s\n"
                    "    # Library: \"%s\"",
                    (csymbol->name ? csymbol->name : "(unnamed)"),
                    (msymbols->module_path ? msymbols->module_path :
                     "$$builtin"),
                    (msymbols->library_name ? msymbols->library_name :
                     "")
                );
            }
        }
        char baseclass_str[32] = "none";
        if (p->classes[i].base_class_global_id >= 0)
            snprintf(
                baseclass_str, sizeof(baseclass_str) - 1,
                "c%" PRId64, (int64_t)p->classes[i].base_class_global_id
            );
        char linebuf[1024 + H64LIMIT_IDENTIFIERLEN] = "";
        snprintf(linebuf, sizeof(linebuf) - 1,
            "BEGINCLASS %" PRId64 " %s %d %d",
            (int64_t)i, baseclass_str,
            (int)p->classes[i].varattr_count,
            p->classes[i].is_error
        );
        if (!disassembler_Write(di,
                "%s%s\n", linebuf, symbolinfo))
            return 0;
        int k = 0;
        while (k < (int)p->classes[i].varattr_count) {
            if (!disassembler_Write(di,
                    "    VARATTR n%" PRId64 "\n",
                    (int64_t)p->classes[i].varattr_global_name_idx[k]
                    ))
                return 0;
            k++;
        }
        k = 0;
        while (k < (int)p->classes[i].funcattr_count) {
            if (!disassembler_Write(di,
                    "    FUNCATTR n%" PRId64 " f%" PRId64 "\n",
                    (int64_t)p->classes[i].funcattr_global_name_idx[k],
                    (int64_t)p->classes[i].funcattr_func_idx[k]
                    ))
                return 0;
            k++;
        }
        if (!disassembler_Write(di,
                "ENDCLASS\n"))
            return 0;
        i++;
    }
    i = 0;
    while (i < p->func_count) {
        char clsinfo[64] = "";
        if (p->func[i].associated_class_index >= 0) {
            snprintf(
                clsinfo, sizeof(clsinfo) - 1,
                " c%" PRId64,
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
            "BEGINFUNC%s %" PRId64 " %d %d%s%s",
            (p->func[i].iscfunc ? "CREF" : ""),
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
                snprintf(
                    symbolinfo, sizeof(symbolinfo) - 1,
                    "\n    # Name: \"%s\" Module: %s\n"
                    "    # Library: \"%s\"\n"
                    "    # Argument count: %d  Closure bounds: %d",
                    (fsymbol->name ? fsymbol->name : "(unnamed)"),
                    (msymbols->module_path ? msymbols->module_path :
                     "$$builtin"),
                    (msymbols->library_name ? msymbols->library_name :
                     ""),
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
                    di, (h64instructionany*)instp,
                    (ptrdiff_t)(instp - (char *)p->func[i].instructions)
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
