
#include <stdlib.h>
#include <string.h>

#include "compiler/warningconfig.h"


void warningconfig_Init(h64compilewarnconfig *wconfig) {
    memset(wconfig, 0, sizeof(*wconfig));
    wconfig->warn_shadowing_vardefs = 1;
    wconfig->warn_unrecognized_escape_sequences = 1;
}

int warningconfig_CheckOption(
        h64compilewarnconfig *wconfig, const char *option
        ) {
    if (!option || strlen(option) < strlen("-W") ||
            option[0] != '-' || option[1] != 'W')
        return 0;

    int enable_warning = 1;
    char warning_name[64];
    unsigned int copylen = strlen(option + 2) + 1;
    if (copylen > sizeof(warning_name)) copylen = sizeof(warning_name);
    memcpy(warning_name, option + 2, copylen);
    warning_name[sizeof(warning_name) - 1] = '\0';
    if (strlen(warning_name) > strlen("no-") &&
            memcmp(warning_name, "no-", strlen("no-")) == 0) {
        enable_warning = 0;
        memmove(warning_name, warning_name + strlen("no-"),
                sizeof(warning_name) - strlen("no-"));
    }

    if (strcmp(warning_name, "shadowing-vardefs") == 0) {
        wconfig->warn_shadowing_vardefs = enable_warning;
        return 1;
    } else if (strcmp(warning_name, "unrecognized-escape-sequences") == 0) {
        wconfig->warn_unrecognized_escape_sequences = enable_warning;
        return 1;
    } else if (strcmp(warning_name, "all") == 0) {
        wconfig->warn_shadowing_vardefs = enable_warning;
        wconfig->warn_unrecognized_escape_sequences = enable_warning;
        return 1;
    }
    return 0;
}
