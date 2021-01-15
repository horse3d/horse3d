// Copyright (c) 2020-2021, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause


#include "compileconfig.h"

#include <assert.h>
#include <stdio.h>

#include "bytecode.h"
#include "valuecontentstruct.h"
#include "vmcontainerstruct.h"
#include "vmmap.h"

#define GENERICMAP_MIGRATE_HASHED 16
#define GENERICMAP_DEFAULT_BUCKETS 64


genericmap *vmmap_New() {
    genericmap *map = malloc(sizeof(*map));
    if (!map)
        return NULL;
    memset(map, 0, sizeof(*map));
    map->flags |= GENERICMAP_FLAG_LINEAR;
    return map;
}

int _vmmap_AddToBucket(
        genericmapbucket *buck, uint32_t hash,
        valuecontent *key, valuecontent *value
        ) {
    valuecontent *entries = realloc(
        buck->entry,
        sizeof(*buck->entry) * (buck->entry_count + 1)
    );
    if (!entries)
        return 0;
    buck->entry = entries;
    valuecontent *keys = realloc(
        buck->key,
        sizeof(*buck->key) * (buck->entry_count + 1)
    );
    if (!entries)
        return 0;
    buck->key = keys;
    uint32_t *entries_hash = realloc(
        buck->entry_hash,
        sizeof(*buck->entry_hash) * (buck->entry_count + 1)
    );
    if (!entries_hash)
        return 0;
    buck->entry_hash = entries_hash;
    memcpy(
        &buck->key[buck->entry_count],
        key, sizeof(*key)
    );
    ADDREF_HEAP(&buck->key[buck->entry_count]);
    memcpy(
        &buck->entry[buck->entry_count],
        value, sizeof(*value)
    );
    ADDREF_HEAP(&buck->entry[buck->entry_count]);
    buck->entry_hash[buck->entry_count] = hash;
    buck->entry_count++;
    return 1;
}

void _vmmap_ClearBuckets(genericmap *map) {
    if ((map->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int32_t i = 0;
        while (i < map->linear.entry_count) {
            DELREF_HEAP(&map->linear.entry[i]);
            valuecontent_Free(&map->linear.entry[i]);
            DELREF_HEAP(&map->linear.key[i]);
            valuecontent_Free(&map->linear.key[i]);
            i++;
        }
        free(map->linear.entry);
        map->linear.entry = NULL;
        free(map->linear.key);
        map->linear.key = NULL;
        return;
    }
    int32_t i = 0;
    while (i < map->hashed.bucket_count) {
        genericmapbucket *b = &map->hashed.bucket[i];
        int k = 0;
        while (k < b->entry_count) {
            DELREF_HEAP(&b->key[k]);
            valuecontent_Free(&b->key[k]);
            DELREF_HEAP(&b->entry[k]);
            valuecontent_Free(&b->entry[k]);
            k++;
        }
        free(b->key);
        b->key = NULL;
        free(b->entry);
        b->entry = NULL;
        b->entry_count = 0;
        i++;
    }
}

int vmmap_Contains(
        genericmap *m, valuecontent *key
        ) {
    return vmmap_Get(m, key, NULL);
}

int vmmap_Get(
        genericmap *m, valuecontent *key, valuecontent *value
        ) {
    uint32_t hash = valuecontent_Hash(key);
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int i = 0;
        while (i < m->linear.entry_count) {
            if (m->linear.entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    key, &m->linear.key[i])) {
                if (value)
                    memcpy(value, &m->linear.entry[i], sizeof(*value));
                return 1;
            }
            i++;
        }
    } else {
        uint32_t bucket = (
            hash % m->hashed.bucket_count
        );
        genericmapbucket *b = &m->hashed.bucket[bucket];
        int i = 0;
        while (i < b->entry_count) {
            if (b->entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    key, &b->key[i])) {
                return 1;
            }
            i++;
        }
    }
    return 0;
}

int _vmmap_RemoveByHash(
        genericmap *m, uint32_t hash, valuecontent *key
        ) {
    int found = 0;
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int i = 0;
        while (i < m->linear.entry_count) {
            if (m->linear.entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    key, &m->linear.key[i])) {
                found = 1;
                DELREF_HEAP(&m->linear.entry[i]);
                valuecontent_Free(&m->linear.entry[i]);
                if (i + 1 < m->linear.entry_count)
                    memmove(
                        &m->linear.entry[i],
                        &m->linear.entry[i + 1],
                        sizeof(*m->linear.entry) *
                            (m->linear.entry_count - i - 1)
                    );
                m->linear.entry_count--;
                continue;
            }
            i++;
        }
    } else {
        uint32_t bucket = (
            hash % m->hashed.bucket_count
        );
        genericmapbucket *b = &m->hashed.bucket[bucket];
        int i = 0;
        while (i < b->entry_count) {
            if (b->entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    key, &b->key[i])) {
                found = 1;
                DELREF_HEAP(&b->entry[i]);
                valuecontent_Free(&b->entry[i]);
                if (i + 1 < b->entry_count)
                    memmove(
                        &b->entry[i],
                        &b->entry[i + 1],
                        sizeof(*b->entry) *
                            (b->entry_count - i - 1)
                    );
                b->entry_count--;
                continue;
            }
            i++;
        }
    }
    return found;
}

int vmmap_Remove(genericmap *m, valuecontent *key) {
    if (!m)
        return 0;
    uint32_t hash = valuecontent_Hash(key);
    return _vmmap_RemoveByHash(m, hash, key);
}

int vmmap_Set(
        genericmap *m, valuecontent *key, valuecontent *value
        ) {
    if (!m)
        return 0;
    uint32_t hash = valuecontent_Hash(key);
    vmmap_Remove(m, key);
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int64_t c = vmmap_Count(m);
        if (c + 1 < GENERICMAP_MIGRATE_HASHED) {
            // Add to linear map:
            if (c + 1 > m->linear.entry_alloc) {
                int16_t new_alloc = (c + 1) * 2;
                if (new_alloc < GENERICMAP_MIGRATE_HASHED)
                    new_alloc = GENERICMAP_MIGRATE_HASHED;
                valuecontent *entries = realloc(
                    m->linear.entry,
                    sizeof(*m->linear.entry) * new_alloc
                );
                if (!entries)
                    return 0;
                m->linear.entry = entries;
                valuecontent *keys = realloc(
                    m->linear.key,
                    sizeof(*m->linear.key) * new_alloc
                );
                if (!keys)
                    return 0;
                m->linear.key = keys;
                uint32_t *hashes = realloc(
                    m->linear.entry_hash,
                    sizeof(*m->linear.entry_hash) * new_alloc
                );
                if (!hashes)
                    return 0;
                m->linear.entry_hash = hashes;
                m->linear.entry_alloc = new_alloc;
            }
            m->linear.entry_hash[m->linear.entry_count] = hash;
            memcpy(
                &m->linear.key[m->linear.entry_count],
                key, sizeof(*key)
            );
            ADDREF_HEAP(&m->linear.key[m->linear.entry_count]);
            memcpy(
                &m->linear.entry[m->linear.entry_count],
                value, sizeof(*value)
            );
            ADDREF_HEAP(&m->linear.entry[m->linear.entry_count]);
            m->linear.entry_count++;
            return 1;
        } else {
            m->flags &= ~GENERICMAP_FLAG_LINEAR;
            // Since linear space and bucket are a union,
            // we need to create the bucket space separately and
            // then copy it over:
            genericmap m2 = {0};
            m2.hashed.bucket_count = GENERICMAP_DEFAULT_BUCKETS;
            m2.hashed.bucket = malloc(
                sizeof(*m2.hashed.bucket) * m2.hashed.bucket_count
            );
            if (!m2.hashed.bucket)
                return 0;
            memset(
                m2.hashed.bucket, 0,
                sizeof(*m2.hashed.bucket) * m2.hashed.bucket_count
            );
            // Now, insert into our new buckets:
            int i = 0;
            while (i < m->linear.entry_count) {
                uint32_t bucket = (
                    m->linear.entry_hash[i] % m2.hashed.bucket_count
                );
                if (!_vmmap_AddToBucket(
                        &m2.hashed.bucket[bucket],
                        m->linear.entry_hash[i],
                        &m->linear.key[i],
                        &m->linear.entry[i])) {
                    _vmmap_ClearBuckets(&m2);
                    free(m2.hashed.bucket);
                    return 0;
                }
                i++;
            }
            // If it worked, free our old linear space:
            free(m->linear.entry_hash);
            i = 0;
            while (i < m->linear.entry_count) {
                DELREF_HEAP(&m->linear.key[i]);
                valuecontent_Free(&m->linear.key[i]);
                DELREF_HEAP(&m->linear.entry[i]);
                valuecontent_Free(&m->linear.entry[i]);
                i++;
            }
            free(m->linear.key);
            free(m->linear.entry);
            // Now copy in our bucket space:
            memcpy(&m->hashed, &m2.hashed, sizeof(m2.hashed));
        }
    }
    // If we arrive here, we need to add to regular buckets:
    uint32_t bucket = (
        hash % m->hashed.bucket_count
    );
    if (!_vmmap_AddToBucket(
            &m->hashed.bucket[bucket],
            hash, key, value)) {
        return 0;
    }
    return 1;
}