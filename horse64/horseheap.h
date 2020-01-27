#ifndef HORSE3D_HORSEHEAP_H_
#define HORSE3D_HORSEHEAP_H_


typedef struct horseheap horseheap;

void *horseheap_Malloc(horseheap *hheap, size_t new_size);

void *horseheap_Realloc(
    horseheap *hheap, void *ptr, size_t new_size
);

int horseheap_EnsureHeadroom(horseheap *hheap);

void horseheap_Free(horseheap *hheap, void *ptr);

horseheap *horseheap_New();

void horseheap_Destroy(horseheap *hheap);

int horseheap_EnsureCanAllocSize(horseheap *heap, size_t size);

#endif  // HORSE3D_HORSEHEAP_H_
