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
        h64vmthread *vt,
        genericmap *m, valuecontent *key, int *oom
        ) {
    return vmmap_Get(vt, m, key, NULL, oom);
}

int vmmap_IteratePairs(
        genericmap *m, void *userdata,
        int (*cb)(void *udata, valuecontent *key, valuecontent *value)
        ) {
    assert(m != NULL);
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int i = 0;
        while (i < m->linear.entry_count) {
            valuecontent *key = &m->linear.key[i];
            valuecontent *value = &m->linear.entry[i];
            if (!cb(userdata, key, value))
                return 0;
            i++;
        }
    } else {
        int k = 0;
        while (k < m->hashed.bucket_count) {
            genericmapbucket *b = &m->hashed.bucket[k];
            int i = 0;
            while (i < b->entry_count) {
                valuecontent *key = &b->key[i];
                valuecontent *value = &b->entry[i];
                if (!cb(userdata, key, value))
                    return 0;
                i++;
            }
            k++;
        }
    }
    return 1;
}

int vmmap_Get(
        h64vmthread *vt,
        genericmap *m, valuecontent *key, valuecontent *value,
        int *oom
        ) {
    uint32_t hash = valuecontent_Hash(key);
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int i = 0;
        while (i < m->linear.entry_count) {
            int inneroom = 0;
            if (unlikely(m->linear.entry_hash[i] == hash) &&
                    likely(valuecontent_CheckEquality(
                    vt, key, &m->linear.key[i], &inneroom))) {
                if (value)
                    memcpy(value, &m->linear.entry[i], sizeof(*value));
                return 1;
            }
            if (unlikely(inneroom)) {
                *oom = 1;
                return 0;
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
            int inneroom = 0;
            if (likely(b->entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    vt, key, &b->key[i], &inneroom))) {
                return 1;
            }
            if (unlikely(inneroom)) {
                *oom = 1;
                return 0;
            }
            i++;
        }
    }
    *oom = 0;
    return 0;
}

int _vmmap_RemoveByHash(
        h64vmthread *vt,
        genericmap *m, uint32_t hash, valuecontent *key,
        int *oom
        ) {
    int found = 0;
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        int i = 0;
        while (i < m->linear.entry_count) {
            int inneroom = 0;
            if (m->linear.entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    vt, key, &m->linear.key[i],
                    &inneroom)) {
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
                m->contentrevisionid++;
                continue;
            }
            if (unlikely(inneroom)) {
                *oom = 1;
                return 0;
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
            int inneroom = 0;
            if (b->entry_hash[i] == hash &&
                    valuecontent_CheckEquality(
                    vt, key, &b->key[i], &inneroom)) {
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
                m->contentrevisionid++;
                continue;
            }
            if (unlikely(inneroom)) {
                *oom = 1;
                return 0;
            }
            i++;
        }
    }
    *oom = 0;
    return found;
}

int vmmap_Remove(h64vmthread *vt,
        genericmap *m, valuecontent *key, int *oom) {
    if (!m) {
        *oom = 0;
        return 0;
    }
    uint32_t hash = valuecontent_Hash(key);
    return _vmmap_RemoveByHash(vt, m, hash, key, oom);
}

valuecontent *vmmap_GetKeyByIdx(genericmap *m, int64_t idx) {
    if (!m)
        return NULL;
    int64_t c = vmmap_Count(m);
    if (idx < 1 || idx > c)
        return NULL;
    if ((m->flags & GENERICMAP_FLAG_LINEAR) != 0) {
        return &m->linear.key[idx];
    }
    int64_t cmp_idx = 0;
    int64_t i = 0;
    while (i < m->hashed.bucket_count) {
        genericmapbucket *bucket = &(
            m->hashed.bucket[i]
        );
        if (cmp_idx + bucket->entry_count < idx) {
            cmp_idx += bucket->entry_count;
            i++;
            continue;
        }
        int64_t k = 0;
        while (k < bucket->entry_count) {
            cmp_idx++;
            if (cmp_idx == idx)
                return &bucket->key[k];
            k++;
        }
        i++;
    }
    // This should be unreachable since we checked idx boundaries.
    fprintf(stderr, "vmmap_GetKeyByIdx: corrupt map.");
    assert(0);
    return NULL;
}

int vmmap_Set(
        h64vmthread *vt,
        genericmap *m, valuecontent *key, valuecontent *value
        ) {
    if (!m)
        return 0;
    uint32_t hash = valuecontent_Hash(key);
    int inneroom = 0;
    if (!vmmap_Remove(vt, m, key, &inneroom)) {
        if (unlikely(inneroom)) {
            return 0;
        }
    }
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
            m->contentrevisionid++;
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
    m->contentrevisionid++;
    return 1;
}