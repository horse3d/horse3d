// Copyright (c) 2020, ellie/@ell1e & Horse64 Team (see AUTHORS.md),
// also see LICENSE.md file.
// SPDX-License-Identifier: BSD-2-Clause

#include "compileconfig.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hash.h"
#include "nonlocale.h"
#include "secrandom.h"


static char global_hashsecret[16];


#define HASHTYPE_BYTES 0
#define HASHTYPE_STRING 1
#define HASHTYPE_NUMBER 2
#define HASHTYPE_STRINGTOSTRING 3

typedef struct hashmap_bucket hashmap_bucket;

typedef struct hashmap_bucket {
    char *bytes; uint64_t byteslen;
    uint64_t number;
    hashmap_bucket *next, *prev;
} hashmap_bucket;


typedef struct hashmap {
    int fixedhashsecretset;
    char fixedhashsecret[16];

    int type;
    int bucket_count;
    hashmap_bucket **buckets;
} hashmap;


uint64_t hash_StringHash(const char *s, uint8_t *secret) {
    if (!secret)
        secret = (uint8_t*)global_hashsecret;
    return hash_ByteHash(s, strlen(s), secret);
}


hashmap *hash_NewBytesMap(int buckets) {
    hashmap *map = malloc(sizeof(*map));
    if (!map)
        return NULL;
    memset(map, 0, sizeof(*map));
    map->bucket_count = buckets;
    map->buckets = malloc(sizeof(void*) * buckets);
    if (!map->buckets) {
        free(map);
        return NULL;
    }
    memset(map->buckets, 0, sizeof(void*) * buckets);
    return map;
}

hashmap *hash_NewStringMap(int buckets) {
    hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = HASHTYPE_STRING;
    return map;
}

hashmap *hash_NewStringToStringMap(int buckets) {
    hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = HASHTYPE_STRINGTOSTRING;
    return map;
}

hashmap *hash_NewIntMap(int buckets) {
    hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = HASHTYPE_NUMBER;
    return map;
}


static int _hash_MapGetBucket(
        hashmap *map, const char *bytes, int byteslen) {
    uint64_t hash = hash_ByteHash(
        bytes, byteslen,
        (uint8_t*)(map->fixedhashsecretset ? map->fixedhashsecret : NULL)
    );
    uint64_t bucket = (uint64_t)(hash % ((uint64_t)map->bucket_count));
    return bucket;
}

static int _hash_MapSet(
        hashmap *map, const char *bytes,
        int byteslen, uint64_t result) {
    int i = _hash_MapGetBucket(map, bytes, byteslen);
    hashmap_bucket *bk = malloc(sizeof(*bk));
    if (!bk)
        return 0;
    memset(bk, 0, sizeof(*bk));
    bk->bytes = malloc(byteslen);
    if (!bk->bytes) {
        free(bk);
        return 0;
    }
    memcpy(bk->bytes, bytes, byteslen);
    bk->byteslen = byteslen;
    hashmap_bucket *prevbk = NULL;
    if (map->buckets[i]) {
        prevbk = map->buckets[i];
        while (prevbk && prevbk->next)
            prevbk = prevbk->next;
    }
    bk->number = result;
    bk->prev = prevbk;
    if (!prevbk) {
        map->buckets[i] = bk;
    } else {
        prevbk->next = bk;
    }
    return 1;
}


static int _hash_MapGet(
        hashmap *map, const char *bytes,
        uint64_t byteslen, uint64_t *result) {
    int i = _hash_MapGetBucket(map, bytes, byteslen);
    if (!map->buckets[i])
        return 0;
    hashmap_bucket *bk = map->buckets[i];
    while (bk) {
        if (bk->byteslen == byteslen &&
                memcmp(bk->bytes, bytes, byteslen) == 0) {
            *result = bk->number;
            return 1;
        }
        bk = bk->next;
    }
    return 0;
}


static int _hash_MapUnset(
        hashmap *map, const char *bytes,
        uint64_t byteslen) {
    if (!map || !bytes)
        return 0; 
   int i = _hash_MapGetBucket(map, bytes, byteslen);
    if (!map->buckets[i])
        return 0;
    hashmap_bucket *prevbk = NULL;
    hashmap_bucket *bk = map->buckets[i];
    while (bk) {
        if (bk->byteslen == byteslen &&
                bk->bytes &&
                memcmp(bk->bytes, bytes, byteslen) == 0) {
            if (prevbk)
                prevbk->next = bk->next;
            if (prevbk && prevbk->next)
                prevbk->next->prev = prevbk;
            if (!prevbk)
                map->buckets[i] = bk->next;
            if (bk->bytes)
                free(bk->bytes);
            free(bk);
            return 1;
        }
        prevbk = bk;
        bk = bk->next;
    }
    return 0;
}


int hash_BytesMapSet(
        hashmap *map, const char *bytes,
        size_t byteslen, uint64_t number) {
    if (!map || map->type != HASHTYPE_BYTES || !bytes)
        return 0;
    _hash_MapUnset(map, bytes, byteslen);
    return _hash_MapSet(
        map, bytes, byteslen, number
    );
}

struct bytemapiterateentry {
    char *bytes;
    uint64_t byteslen;
    uint64_t number;
};

static int _hash_MapIterateEx(
        hashmap *map,
        int (*cb)(hashmap *map, const char *bytes,
                  uint64_t byteslen, uint64_t number, void *ud),
        void *ud
        ) {
    if (!map)
        return 0;

    struct bytemapiterateentry *entries = NULL;
    int alloc_size = 0;
    int found_entries = 0;

    int i = 0;
    while (i < map->bucket_count) {
        hashmap_bucket *bk = map->buckets[i];
        while (bk) {
            if (alloc_size <= found_entries) {
                alloc_size *= 2;
                if (alloc_size < found_entries + 8)
                    alloc_size = found_entries + 8;
                struct bytemapiterateentry *new_entries = realloc(
                    entries, sizeof(*entries) * alloc_size
                );
                if (!new_entries) {
                    allocfail: ;
                    int k = 0;
                    while (k < found_entries) {
                        if (entries[k].bytes)
                            free(entries[k].bytes);
                        k++;
                    }
                    free(entries);
                    return 0;
                }
                entries = new_entries;
            }
            memset(&entries[found_entries],
                   0, sizeof(entries[found_entries]));
            if (bk->byteslen > 0) {
                entries[found_entries].bytes = malloc(bk->byteslen);
                if (!entries[found_entries].bytes)
                    goto allocfail;
                memcpy(entries[found_entries].bytes,
                       bk->bytes, bk->byteslen);
                entries[found_entries].byteslen = bk->byteslen;
            }
            entries[found_entries].number = bk->number;
            found_entries++;
            bk = bk->next;
        }
        i++;
    }
    int haderror = 0;
    i = 0;
    while (i < found_entries) {
        if (!cb(map, entries[i].bytes, entries[i].byteslen,
                entries[i].number, ud)) {
            haderror = 1;
            break;
        }
        i++;
    }
    i = 0;
    while (i < found_entries) {
        if (entries[i].bytes)
            free(entries[i].bytes);
        i++;
    }
    free(entries);
    return !haderror;
}

int hash_BytesMapIterate(
        hashmap *map,
        int (*cb)(hashmap *map, const char *bytes,
                  uint64_t byteslen, uint64_t number, void *ud),
        void *ud
        ) {
    if (!map || map->type != HASHTYPE_BYTES)
        return 0;
    return _hash_MapIterateEx(
        map, cb, ud
    );
}

int hash_BytesMapGet(
        hashmap *map, const char *bytes,
        size_t byteslen, uint64_t *number) {
    if (map->type != HASHTYPE_BYTES)
        return 0;
    return _hash_MapGet(
        map, bytes, byteslen, number
    );
}


int hash_BytesMapUnset(
        hashmap *map, const char *bytes,
        size_t byteslen) {
    if (map->type != HASHTYPE_BYTES)
        return 0;
    return _hash_MapUnset(map, bytes, byteslen);
}


int hash_StringMapSet(
        hashmap *map, const char *s, uint64_t number
        ) {
    if (map->type != HASHTYPE_STRING)
        return 0;
    _hash_MapUnset(map, s, strlen(s) + 1);
    return _hash_MapSet(
        map, s, strlen(s) + 1, number
    );
}


struct stringmapiterateinfo {
    void *ud;
    int (*cb)(hashmap *map, const char *key, uint64_t number,
              void *ud);
};

static int _hashstringmap_iterate(
        hashmap *map, const char *bytes,
        __attribute__((unused)) uint64_t byteslen,
        uint64_t number, void *ud
        ) {
    struct stringmapiterateinfo *iterinfo = (
        (struct stringmapiterateinfo*)ud
    );
    return iterinfo->cb(map, bytes, number, iterinfo->ud);
}

int hash_StringMapIterate(
        hashmap *map,
        int (*callback)(
            hashmap *map, const char *key, uint64_t number,
            void *userdata
        ),
        void *userdata
        ) {
    if (!map || map->type != HASHTYPE_STRING)
        return 0;

    struct stringmapiterateinfo iinfo;
    memset(&iinfo, 0, sizeof(iinfo));
    iinfo.ud = userdata;
    iinfo.cb = callback;

    return _hash_MapIterateEx(
        map, &_hashstringmap_iterate, &iinfo
    );
}


int hash_StringMapGet(
        hashmap *map, const char *s, uint64_t *number
        ) {
    if (map->type != HASHTYPE_STRING)
        return 0;
    return _hash_MapGet(
        map, s, strlen(s) + 1, number
    );
}


int hash_StringMapUnset(hashmap *map, const char *s) {
    if (map->type != HASHTYPE_STRING)
        return 0;
    return _hash_MapUnset(map, s, strlen(s) + 1);
}

int hash_IntMapSet(
        hashmap *map, int64_t key, uint64_t number
        ) {
    if (map->type != HASHTYPE_NUMBER)
        return 0;
    _hash_MapUnset(map, (char*)&key, sizeof(key));
    return _hash_MapSet(
        map, (char*)&key, sizeof(key), number
    );
}


int hash_IntMapGet(hashmap *map, int64_t key, uint64_t *number) {
    if (map->type != HASHTYPE_NUMBER)
        return 0;
    return _hash_MapGet(
        map, (char*)&key, sizeof(key), number
    );
}

int hash_IntMapUnset(hashmap *map, int64_t key) {
    if (map->type != HASHTYPE_NUMBER)
        return 0;
    return _hash_MapUnset(map, (char*)&key, sizeof(key));
}

struct intmapiterateinfo {
    void *ud;
    int (*cb)(hashmap *map, int64_t key, uint64_t number,
              void *ud);
};

static int _hashintmap_iterate(
        hashmap *map, const char *bytes,
        __attribute__((unused)) uint64_t byteslen,
        uint64_t number, void *ud
        ) {
    struct intmapiterateinfo *iterinfo = (
        (struct intmapiterateinfo*)ud
    );
    int64_t n;
    assert(sizeof(n) == byteslen);
    memcpy(&n, bytes, sizeof(n));
    return iterinfo->cb(map, n, number, iterinfo->ud);
}

int hash_IntMapIterate(
        hashmap *map,
        int (*callback)(
            hashmap *map, int64_t key, uint64_t number,
            void *userdata
        ),
        void *userdata
        ) {
    if (!map || map->type != HASHTYPE_NUMBER)
        return 0;

    struct intmapiterateinfo iinfo;
    memset(&iinfo, 0, sizeof(iinfo));
    iinfo.ud = userdata;
    iinfo.cb = callback;

    return _hash_MapIterateEx(
        map, &_hashintmap_iterate, &iinfo
    );
}


int siphash(const uint8_t *in, const size_t inlen, const uint8_t *k,
            uint8_t *out, const size_t outlen);

uint64_t hash_ByteHash(
        const char *bytes, uint64_t byteslen,
        uint8_t *secret) {
    if (!secret)
        secret = (uint8_t*)global_hashsecret;
    uint64_t hashval = 0;
    siphash((uint8_t*)bytes, byteslen, secret,
            (uint8_t*)&hashval, sizeof(hashval));
    return hashval;
}

void hash_ClearMap(hashmap *map) {
    if (!map)
        return;
    if (map->buckets) {
        int i = 0;
        while (i < map->bucket_count) {
            hashmap_bucket *bk = map->buckets[i];
            while (bk) {
                if (map->type == HASHTYPE_STRINGTOSTRING) {
                    char *svalue = (char*)(uintptr_t)bk->number;
                    if (svalue)
                        free(svalue);
                }
                if (bk->bytes)
                    free(bk->bytes);
                hashmap_bucket *nextbk = bk->next;
                free(bk);
                bk = nextbk;
            }
            map->buckets[i] = NULL;
            i++;
        }
    }
}

void hash_FreeMap(hashmap *map) {
    if (!map)
        return;
    hash_ClearMap(map);
    if (map->buckets)
        free(map->buckets);
    free(map);
}

__attribute__((constructor)) static void hashSetHashSecrets() {
    if (!secrandom_GetBytes(
            global_hashsecret, sizeof(global_hashsecret)
            )) {
        h64fprintf(stderr,
            "hash.c: FAILED TO INITIALIZE GLOBAL "
            "HASH SECRETS. ABORTING.\n"
        );
        exit(0);
    } 
}

int hash_STSMapSet(
        hashmap *map, const char *key, const char *value
        ) {
    if (map->type != HASHTYPE_STRINGTOSTRING)
        return 0;
    uint64_t number = (uintptr_t)strdup(value);
    if (number == 0)
        return 0;
    hash_STSMapUnset(map, key);
    if (!_hash_MapSet(
            map, key, strlen(key), number
            )) {
        free((char*)number);
        return 0;
    }
    return 1;
}

const char *hash_STSMapGet(hashmap *map, const char *key) {
    if (map->type != HASHTYPE_STRINGTOSTRING)
        return NULL;
    uint64_t number = 0;
    if (!_hash_MapGet(
            map, key, strlen(key), &number
            ))
        return NULL;
    return (const char *)(uintptr_t)number;
}

int hash_STSMapUnset(hashmap *map, const char *key) {
    if (map->type != HASHTYPE_STRINGTOSTRING)
        return 0;
    uint64_t number = 0;
    if (_hash_MapGet(
            map, key, strlen(key), &number
            ) && number > 0) {
        free((void*)(uintptr_t)number);
    }
    return _hash_MapUnset(map, key, strlen(key));
}

struct stsmapiterateentry {
    char *key, *value;
};

int hash_STSMapIterate(
        hashmap *map,
        int (*cb)(hashmap *map, const char *key,
                  const char *value, void *ud),
        void *ud
        ) {
    if (!map || map->type != HASHTYPE_STRINGTOSTRING)
        return 0;

    struct stsmapiterateentry *entries = NULL;
    int alloc_size = 0;
    int found_entries = 0;

    int i = 0;
    while (i < map->bucket_count) {
        hashmap_bucket *bk = map->buckets[i];
        while (bk) {
            if (alloc_size <= found_entries) {
                alloc_size *= 2;
                if (alloc_size < found_entries + 8)
                    alloc_size = found_entries + 8;
                struct stsmapiterateentry *new_entries = realloc(
                    entries, sizeof(*entries) * alloc_size
                );
                if (!new_entries) {
                    allocfail: ;
                    int k = 0;
                    while (k < found_entries) {
                        if (entries[k].key)
                            free(entries[k].key);
                        if (entries[k].value)
                            free(entries[k].value);
                        k++;
                    }
                    free(entries);
                    return 0;
                }
                entries = new_entries;
            }
            memset(&entries[found_entries],
                   0, sizeof(entries[found_entries]));
            if (bk->bytes != NULL) {
                entries[found_entries].key = strdup(
                    (const char*)bk->bytes
                );
                if (!entries[found_entries].key)
                    goto allocfail;
                entries[found_entries].value = NULL;
                if (bk->number != 0) {
                    entries[found_entries].value = strdup(
                        (const char*)(uintptr_t)bk->number
                    );
                    if (!entries[found_entries].value) {
                        free(entries[found_entries].key);
                        goto allocfail;
                    }
                }
                found_entries++;
            }
            bk = bk->next;
        }
        i++;
    }
    i = 0;
    while (i < found_entries) {
        if (!cb(map, entries[i].key, entries[i].value, ud))
            break;
        i++;
    }
    i = 0;
    while (i < found_entries) {
        if (entries[i].key)
            free(entries[i].key);
        if (entries[i].value)
            free(entries[i].value);
        i++;
    }
    free(entries);
    return 1;
}

void hashmap_SetFixedHashSecret(
        hashmap *map, uint8_t *secret
        ) {
    map->fixedhashsecretset = 1;
    memcpy(
        map->fixedhashsecret, secret, sizeof(*map->fixedhashsecret)
    );
}


typedef struct hashset {
   hashmap *map;
} hashset;


hashset *hashset_New(int buckets) {
    hashset *set = malloc(sizeof(*set));
    if (!set)
        return NULL;
    memset(set, 0, sizeof(*set));
    set->map = hash_NewBytesMap(buckets);
    if (!set->map) {
        free(set);
        return NULL;
    }
    return set;
}

int hashset_Contains(
        hashset *set,
        const void *itemdata, size_t itemdatasize
        ) {
    uint64_t result;
    if (!hash_BytesMapGet(
            set->map, (const char *)itemdata, itemdatasize,
            &result))
        return 0;
    return 1;
}

int hashset_Add(
        hashset *set, const void *itemdata, size_t itemdatasize
        ) {
    if (hashset_Contains(set, itemdata, itemdatasize))
        return 1;
    return hash_BytesMapSet(
        set->map, (const char *)itemdata, itemdatasize, (uint64_t)1
    );
}

void hashset_Remove(
        hashset *set,
        const void *itemdata, size_t itemdatasize
        ) {
    hash_BytesMapUnset(
        set->map, (const char *)itemdata, itemdatasize
    );
}

void hashset_Free(hashset *set) {
    if (!set)
        return;
    if (set->map)
        hash_FreeMap(set->map);
    free(set);
}
