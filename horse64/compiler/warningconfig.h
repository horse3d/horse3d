#ifndef HORSE64_COMPILER_WARNINGCONFIG_H_
#define HORSE64_COMPILER_WARNINGCONFIG_H_


typedef struct h64compilewarnconfig {
    int warn_shadowing_vardefs;
    int warn_unrecognized_escape_sequences;
} h64compilewarnconfig;


void warningconfig_Init(h64compilewarnconfig *wconfig);

int warningconfig_CheckOption(
    h64compilewarnconfig *wconfig, const char *option
);

#endif  // HORSE64_COMPILER_WARNINGCONFIG_H_
