#ifndef OCTREE_H_
#define OCTREE_H_


#define MAXNODEITEMS 8


typedef struct octree octree;
typedef struct octreeitem_assigninfo octreeitem_assigninfo;


octree *octree_New();

octreeitem_assigninfo *octree_AddItem(
    octree *ot,
    double center_x, double center_y, double center_z,
    double size_x, double size_y, double size_z,
    void *objptr
);

void octree_RemoveItem(
    octree *ot, void *objptr,
    octreeitem_assigninfo *objinfo
);

void octree_Destroy(octree *ot);

void octree_Query(
    octree *ot,
    double center_x, double center_y, double center_z,
    double size_x, double size_y, double size_z,
    void (*resultCallback)(octree *node, void *objptr, void *userdata),
    void *userdata
);

#endif  // OCTREE_H_
