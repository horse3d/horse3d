
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hash.h"
#include "secrandom.h"

static char global_hash_secret[16];


#define MAPTYPE_BYTES 0
#define MAPTYPE_STRING 1
#define MAPTYPE_NUMBER 2
#define MAPTYPE_STRINGTOSTRING 3


struct hashmap_bucket {
    char *bytes; uint64_t byteslen;
    uint64_t number;
    struct hashmap_bucket *next, *prev;
};


struct hashmap {
    int type;
    int bucket_count;
    struct hashmap_bucket **buckets;
};


uint64_t hash_StringHash(const char *s) {
    return hash_ByteHash(s, strlen(s));
}


struct hashmap *hash_NewBytesMap(int buckets) {
    struct hashmap *map = malloc(sizeof(*map));
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


struct hashmap *hash_NewStringMap(int buckets) {
    struct hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = MAPTYPE_STRING;
    return map;
}

struct hashmap *hash_NewStringToStringMap(int buckets) {
    struct hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = MAPTYPE_STRINGTOSTRING;
    return map;
}

struct hashmap *hash_NewIntMap(int buckets) {
    struct hashmap *map = hash_NewBytesMap(buckets);
    if (!map)
        return NULL;
    map->type = MAPTYPE_NUMBER;
    return map;
}


static int _hash_MapGetBucket(
        struct hashmap *map, const char *bytes, int byteslen) {
    uint64_t hash = hash_ByteHash(bytes, byteslen);
    uint64_t bucket = (uint64_t)(hash % ((uint64_t)map->bucket_count));
    return bucket;
}


static int _hash_MapSet(
        struct hashmap *map, const char *bytes,
        int byteslen, uint64_t result) {
    int i = _hash_MapGetBucket(map, bytes, byteslen);
    struct hashmap_bucket *bk = malloc(sizeof(*bk));
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
    struct hashmap_bucket *prevbk = NULL;
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
        struct hashmap *map, const char *bytes,
        uint64_t byteslen, uint64_t *result) {
    int i = _hash_MapGetBucket(map, bytes, byteslen);
    if (!map->buckets[i])
        return 0;
    struct hashmap_bucket *bk = map->buckets[i];
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
        struct hashmap *map, const char *bytes,
        uint64_t byteslen) {
    if (!map || !bytes)
        return 0; 
   int i = _hash_MapGetBucket(map, bytes, byteslen);
    if (!map->buckets[i])
        return 0;
    struct hashmap_bucket *prevbk = NULL;
    struct hashmap_bucket *bk = map->buckets[i];
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


int hash_BytesMapSet(struct hashmap *map, const char *bytes,
                     size_t byteslen, uint64_t number) {
    if (!map || map->type != MAPTYPE_BYTES || !bytes)
        return 0;
    _hash_MapUnset(map, bytes, byteslen);
    return _hash_MapSet(
        map, bytes, byteslen, number
    );
}


int hash_BytesMapGet(struct hashmap *map, const char *bytes,
                     size_t byteslen, uint64_t *number) {
    if (map->type != MAPTYPE_BYTES)
        return 0;
    return _hash_MapGet(
        map, bytes, byteslen, number
    );
}


int hash_BytesMapUnset(struct hashmap *map, const char *bytes,
                       size_t byteslen) {
    if (map->type != MAPTYPE_BYTES)
        return 0;
    return _hash_MapUnset(map, bytes, byteslen);
}


int hash_StringMapSet(struct hashmap *map, const char *s, uint64_t number) {
    if (map->type != MAPTYPE_STRING)
        return 0;
    _hash_MapUnset(map, s, strlen(s));
    return _hash_MapSet(
        map, s, strlen(s), number
    );
}


int hash_StringMapGet(struct hashmap *map, const char *s, uint64_t *number) {
    if (map->type != MAPTYPE_STRING)
        return 0;
    return _hash_MapGet(
        map, s, strlen(s), number
    );
}


int hash_StringMapUnset(struct hashmap *map, const char *s) {
    if (map->type != MAPTYPE_STRING)
        return 0;
    return _hash_MapUnset(map, s, strlen(s));
}


int hash_IntMapSet(struct hashmap *map, int64_t key, uint64_t number) {
    if (map->type != MAPTYPE_NUMBER)
        return 0;
    _hash_MapUnset(map, (char*)&key, sizeof(key));
    return _hash_MapSet(
        map, (char*)&key, sizeof(key), number
    );
}


int hash_IntMapGet(struct hashmap *map, int64_t key, uint64_t *number) {
    if (map->type != MAPTYPE_NUMBER)
        return 0;
    return _hash_MapGet(
        map, (char*)&key, sizeof(key), number
    );
}

int hash_IntMapUnset(struct hashmap *map, int64_t key) {
    if (map->type != MAPTYPE_NUMBER)
        return 0;
    return _hash_MapUnset(map, (char*)&key, sizeof(key));
}

int siphash(const uint8_t *in, const size_t inlen, const uint8_t *k,
            uint8_t *out, const size_t outlen);

uint64_t hash_ByteHash(const char *bytes, uint64_t byteslen) {
    uint64_t hashval = 0;
    siphash((uint8_t*)bytes, byteslen, (uint8_t*)&global_hash_secret,
            (uint8_t*)&hashval, sizeof(hashval));
    return hashval;
}

void hash_ClearMap(struct hashmap *map) {
    if (!map)
        return;
    if (map->buckets) {
        int i = 0;
        while (i < map->bucket_count) {
            struct hashmap_bucket *bk = map->buckets[i];
            while (bk) {
                if (map->type == MAPTYPE_STRINGTOSTRING) {
                    char *svalue = (char*)(uintptr_t)bk->number;
                    if (svalue)
                        free(svalue);
                }
                if (bk->bytes)
                    free(bk->bytes);
                struct hashmap_bucket *nextbk = bk->next;
                free(bk);
                bk = nextbk;
            }
            map->buckets[i] = NULL;
            i++;
        }
    }
}

void hash_FreeMap(struct hashmap *map) {
    if (!map)
        return;
    hash_ClearMap(map);
    if (map->buckets)
        free(map->buckets);
    free(map);
}

__attribute__((constructor)) static void hash_SetHashSecrets() {
    if (!secrandom_GetBytes(
            global_hash_secret, sizeof(global_hash_secret)
            )) {
        fprintf(stderr,
            "hash.c: FAILED TO INITIALIZE GLOBAL "
            "HASH SECRETS. ABORTING.\n"
        );
        exit(0);
    } 
}

int hash_STSMapSet(
        struct hashmap *map, const char *key, const char *value
        ) {
    if (map->type != MAPTYPE_STRINGTOSTRING)
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

const char *hash_STSMapGet(struct hashmap *map, const char *key) {
    if (map->type != MAPTYPE_STRINGTOSTRING)
        return NULL;
    uint64_t number = 0;
    if (!_hash_MapGet(
            map, key, strlen(key), &number
            ))
        return NULL;
    return (const char *)(uintptr_t)number;
}

int hash_STSMapUnset(struct hashmap *map, const char *key) {
    if (map->type != MAPTYPE_STRINGTOSTRING)
        return 0;
    uint64_t number = 0;
    if (_hash_MapGet(
            map, key, strlen(key), &number
            ) && number > 0) {
        free((void*)(uintptr_t)number);
    }
    return _hash_MapUnset(map, key, strlen(key));
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

int hashset_Add(hashset *set, const void *itemdata, size_t itemdatasize) {
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
