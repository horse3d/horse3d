#ifndef HORSE3D_HASH_H_
#define HORSE3D_HASH_H_

#include <stdint.h>
#include <stdlib.h>


typedef struct hashmap hashmap;


uint64_t hash_ByteHash(const char *bytes, uint64_t byteslen);


void hash_FreeMap(struct hashmap *map);

void hash_ClearMap(struct hashmap *map);


struct hashmap *hash_NewBytesMap(int buckets);
int hash_BytesMapSet(struct hashmap *map, const char *bytes,
                     size_t byteslen, uint64_t number);
int hash_BytesMapGet(struct hashmap *map, const char *bytes,
                     size_t byteslen, uint64_t *number);
int hash_BytesMapUnset(struct hashmap *map, const char *bytes,
                       size_t byteslen);


struct hashmap *hash_NewStringMap(int buckets);
int hash_StringMapSet(struct hashmap *map, const char *s, uint64_t number);
int hash_StringMapGet(struct hashmap *map, const char *s, uint64_t *number);
int hash_StringMapUnset(struct hashmap *map, const char *s);


struct hashmap *hash_NewIntMap(int buckets);
int hash_IntMapSet(struct hashmap *map, int64_t key, uint64_t number);
int hash_IntMapGet(struct hashmap *map, int64_t key, uint64_t *number);
int hash_IntMapUnset(struct hashmap *map, int64_t key);


struct hashmap *hash_NewStringToStringMap(int buckets);
int hash_STSMapSet(
    struct hashmap *map, const char *key, const char *value
);
const char *hash_STSMapGet(
    struct hashmap *map, const char *key
);
int hash_STSMapUnset(struct hashmap *map, const char *key);

typedef struct hashset hashset;
hashset *hashset_New(int buckets);
int hashset_Contains(
    hashset *set,
    const void *itemdata, size_t itemdatasize
);
int hashset_Add(hashset *set, const void *itemdata, size_t itemdatasize);
void hashset_Remove(hashset *set, const void *itemdata, size_t itemdatasize);
void hashset_Free(hashset *set);

#endif  // HORSE3D_HASH_H_
