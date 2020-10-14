// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#ifndef HORSE64_HASH_H_
#define HORSE64_HASH_H_

#include <stdint.h>
#include <stdlib.h>


typedef struct hashmap hashmap;


uint64_t hash_ByteHash(
    const char *bytes, uint64_t byteslen, uint8_t *hash
);


void hash_FreeMap(hashmap *map);

void hash_ClearMap(hashmap *map);


hashmap *hash_NewBytesMap(int buckets);
int hash_BytesMapSet(
    hashmap *map, const char *bytes,
    size_t byteslen, uint64_t number
);
int hash_BytesMapGet(
    hashmap *map, const char *bytes,
    size_t byteslen, uint64_t *number
);
int hash_BytesMapUnset(
    hashmap *map, const char *bytes,
    size_t byteslen
);
int hash_BytesMapIterate(
    hashmap *map,
    int (*callback)(
        hashmap *map, const char *bytes,
        uint64_t byteslen, uint64_t number,
        void *userdata
    ),
    void *userdata
);

hashmap *hash_NewStringMap(int buckets);
int hash_StringMapSet(
    hashmap *map, const char *s, uint64_t number
);
int hash_StringMapGet(
    hashmap *map, const char *s, uint64_t *number
);
int hash_StringMapUnset(hashmap *map, const char *s);
int hash_StringMapIterate(
    hashmap *map,
    int (*callback)(
        hashmap *map, const char *key, uint64_t number,
        void *userdata
    ),
    void *userdata
);

hashmap *hash_NewIntMap(int buckets);
int hash_IntMapSet(
    hashmap *map, int64_t key, uint64_t number
);
int hash_IntMapGet(
    hashmap *map, int64_t key, uint64_t *number
);
int hash_IntMapUnset(hashmap *map, int64_t key);
int hash_IntMapIterate(
    hashmap *map,
    int (*cb)(hashmap *map, int64_t key,
              uint64_t value, void *ud),
    void *ud
);

hashmap *hash_NewStringToStringMap(int buckets);
int hash_STSMapSet(
    hashmap *map, const char *key, const char *value
);
const char *hash_STSMapGet(
    hashmap *map, const char *key
);
int hash_STSMapUnset(hashmap *map, const char *key);
int hash_STSMapIterate(
    hashmap *map,
    int (*cb)(hashmap *map, const char *key,
              const char *value, void *ud),
    void *ud
);

void hashmap_SetFixedHashSecret(
    hashmap *map, uint8_t *secret
);

typedef struct hashset hashset;
hashset *hashset_New(int buckets);
int hashset_Contains(
    hashset *set,
    const void *itemdata, size_t itemdatasize
);
int hashset_Add(
    hashset *set, const void *itemdata, size_t itemdatasize
);
void hashset_Remove(
    hashset *set, const void *itemdata, size_t itemdatasize
);
void hashset_Free(hashset *set);

#endif  // HORSE64_HASH_H_
