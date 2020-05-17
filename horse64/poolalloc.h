#ifndef HORSE64_POOLALLOC_H_
#define HORSE64_POOLALLOC_H_

typedef struct poolalloc poolalloc;

poolalloc *poolalloc_New(int itemsize);

void poolalloc_Destroy(poolalloc *pool);

void *poolalloc_malloc(poolalloc *pool, int can_use_emergency_margin);

#endif  // HORSE64_POOLALLOC_H_
