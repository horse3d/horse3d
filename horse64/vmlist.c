// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bytecode.h"
#include "gcvalue.h"
#include "vmlist.h"

genericlist *vmlist_New() {
    genericlist *l = malloc(sizeof(*l));
    if (!l)
        return NULL;
    memset(l, 0, sizeof(*l));

    l->first_block = malloc(sizeof(*l->first_block));
    if (!l->first_block) {
        free(l);
        return NULL;
    }
    l->first_block->next_block = NULL;
    l->first_block->entry_count = 0;
    l->last_block = l->first_block;
    return l;
}

int vmlist_Add(
        genericlist *l, valuecontent *vc
        ) {
    assert(l != NULL);
    assert(vc != NULL);
    assert(l->last_block != NULL);
    if (l->last_block->entry_count >= LISTBLOCK_SIZE) {
        listblock *newblock = malloc(sizeof(*newblock));
        if (!newblock)
            return 0;
        newblock->entry_count = 0;
        newblock->next_block = NULL;
        l->last_block->next_block = newblock;
        l->last_block = newblock;
    }
    assert(l->last_block->next_block == NULL &&
           l->last_block->entry_count < LISTBLOCK_SIZE);
    memcpy(
        &l->last_block->entry_values[l->last_block->entry_count],
        vc, sizeof(*vc)
    );
    ADDREF_HEAP(vc);
    l->last_block->entry_count++;
    l->list_total_entry_count++;
    return 1;
}
