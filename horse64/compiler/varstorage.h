#ifndef HORSE64_COMPILER_VARSTORAGE_H_
#define HORSE64_COMPILER_VARSTORAGE_H_

typedef struct h64compileproject h64compileproject;
typedef struct h64ast h64ast;
typedef struct jsonvalue jsonvalue;

int varstorage_AssignLocalStorage(
    h64compileproject *pr, h64ast *ast
);

typedef struct h64localstorageassign {
    int valuetemporaryid;
    int valueboxtemporaryid;
    h64scopedef *vardef;
    int closurevarparameter;

    int use_start_token_index, use_end_token_index;
} h64localstorageassign;

typedef struct h64storageextrainfo {
    int lowest_guaranteed_free_temp;

    int closureboundvars_count;
    h64scopedef **closureboundvars;

    int lstoreassign_count;
    h64localstorageassign *lstoreassign;
} h64storageextrainfo;

void varstorage_FreeExtraInfo(
    h64storageextrainfo *einfo
);

jsonvalue *varstorage_ExtraInfoToJSON(
    h64storageextrainfo *einfo
);

#endif  // HORSE64_COMPILER_VARSTORAGE_H_
