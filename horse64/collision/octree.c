
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "octree.h"

typedef struct octreeitem octreeitem;
typedef struct octreenode octreenode;

struct octreeitem_assigninfo {
    int correspondingitemindex;
    octreenode *assignednode;
};


struct octreeitem {
    double center_x, center_y, center_z;
    double size_x, size_y, size_z;
    void *ptr;
    octreeitem_assigninfo *ainfo;
};


struct octreenode {
    octreeitem *items;
    int item_count;

    int divided_x, divided_y, divided_z;
    double divided_at;
    octreenode *child_pos, *child_neg;
};


struct octree {
    octreenode *node;
};


octree *octree_New() {
    octree *ot = malloc(sizeof(*ot));
    if (!ot)
        return NULL;
    memset(ot, 0, sizeof(*ot));
    return ot;
}


static void _octree_FreeNode(octreenode *node) {
    if (node->child_pos)
        _octree_FreeNode(node->child_pos);
    if (node->child_neg)
        _octree_FreeNode(node->child_neg);
    if (node->items)
        free(node->items);
    free(node);
}


void octree_Destroy(octree *ot) {
    if (ot->node)
        _octree_FreeNode(ot->node);
    free(ot);
}


static void _octree_FindDivide(
        octreenode *node,
        int *xdivide_result,
        int *ydivide_result,
        int *zdivide_result,
        double *dividepos_result
        ) {
    if (node->item_count <= 1) {
        // Nonsensical to divide, just pick something trivial:
        *xdivide_result = 1;
        *ydivide_result = 0;
        *zdivide_result = 0;
        *dividepos_result = 0;
        if (node->item_count == 1) {
            *dividepos_result = (
                node->items[0].center_x -
                node->items[0].size_x * 0.51
            );
        }
        return;
    }

    int bestxdivide = -1;
    int bestydivide = -1;
    int bestzdivide = -1;
    double bestdividescore = 0;
    double bestdividepos = 0;

    int xdivide = 1;
    int ydivide = 0;
    int zdivide = 0;
    int k = 0;
    while (k < 2) {
        xdivide = 0;
        ydivide = 0;
        zdivide = 0;
        if (k == 0)
            xdivide = 1;
        else if (k == 1)
            xdivide = 0;
        else
            zdivide = 0;
        int i = 0;
        while (i < node->item_count) {
            double divide = (
                xdivide ? (
                node->items[0].center_x -
                node->items[0].size_x * 0.51
                ) : (
                node->items[0].center_y -
                node->items[0].size_y * 0.51)
            );
            int negitems = 0;
            int positems = 0;
            int splititems = 0;
            int i2 = 0;
            while (i2 < node->item_count) {
                if (xdivide ? (
                        node->items[0].center_x -
                        node->items[0].size_x * 0.51 >
                        divide) : (ydivide ? (
                        node->items[0].center_y -
                        node->items[0].size_y * 0.51 >
                        divide
                        ) : (
                        node->items[0].center_z -
                        node->items[0].size_z * 0.51 >
                        divide
                        ))) {
                    positems++;
                } else if (xdivide ? (
                        node->items[0].center_x +
                        node->items[0].size_x * 0.51 <
                        divide) : (ydivide ? (
                        node->items[0].center_y +
                        node->items[0].size_y * 0.51 <
                        divide
                        ) : (
                        node->items[0].center_z +
                        node->items[0].size_z * 0.51 <
                        divide
                        ))) {
                    negitems++;
                } else {
                    splititems++;
                }
                i2++;
            }
            int sharednegpositems = positems;
            if (sharednegpositems > negitems)
                sharednegpositems = negitems;
            double score = (
                -abs(positems - negitems) * 5 -
                splititems * 2 +
                sharednegpositems * 3
            );
            if ((bestxdivide < 0 || bestdividescore < score) &&
                    (positems + negitems) >= MAXNODEITEMS / 2) {
                bestxdivide = xdivide;
                bestydivide = ydivide;
                bestzdivide = zdivide;
                bestdividescore = score;
                bestdividepos = divide;
            }
            i++;
        }
        k++;
    }
    if (bestxdivide < 0) {
        *xdivide_result = 1;
        *ydivide_result = 0;
        *zdivide_result = 0;
        *dividepos_result = 0;
        return;
    }
    *xdivide_result = bestxdivide;
    *ydivide_result = bestydivide;
    *zdivide_result = bestzdivide;
    *dividepos_result = bestdividepos;
}


static int _octree_DivideNode(
    octreenode *node
);


static int _octree_SinkitemIntoNode(
    octreenode *node, octreeitem *item
);


static int _octree_SinkitemIntoNode(
        octreenode *node, octreeitem *item
        ) {
    if (!node->child_neg &&
            node->item_count + 1 > MAXNODEITEMS) {
        _octree_DivideNode(node);
    }
    int tried_to_sink = 0;
    int sink_success = 0;
    if (node->divided_x) {
        if (item->center_x +
                item->size_x * 0.501 <
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_neg, item
            );
        } else if (item->center_x -
                item->size_x * 0.501 >
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_pos, item
            );
        }
    } else if (node->divided_y) {
        if (item->center_y +
                item->size_y * 0.501 <
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_neg, item
            );
        } else if (item->center_y -
                item->size_y * 0.501 >
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_pos, item
            );
        }
    } else if (node->divided_z) {
        if (item->center_z +
                item->size_z * 0.501 <
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_neg, item
            );
        } else if (item->center_z -
                item->size_z * 0.501 >
                node->divided_at) {
            tried_to_sink = 1;
            sink_success = _octree_SinkitemIntoNode(
                node->child_pos, item
            );
        }
    }
    if (!sink_success) {
        if (tried_to_sink)
            return 0;
    }
    return 1;
}


static void _octree_QueryNode(
        octree* ot, octreenode *node,
        double center_x, double center_y, double center_z,
        double size_x, double size_y, double size_z,
        void (*resultCallback)(octree *node, void *objptr, void *userdata),
        void *userdata
        ) {
    // Get the nodes on this level:
    int i = 0;
    while (i < node->item_count) {
        if (node->items[i].center_x + node->items[i].size_x / 2 <
                center_x - size_x / 2 ||
                node->items[i].center_x - node->items[i].size_x / 2 >
                center_x + size_x / 2 ||
                node->items[i].center_y + node->items[i].size_y / 2 <
                center_y - size_y / 2 ||
                node->items[i].center_y - node->items[i].size_y / 2 >
                center_y + size_y / 2 ||
                node->items[i].center_z + node->items[i].size_z / 2 <
                center_z - size_z / 2 ||
                node->items[i].center_z - node->items[i].size_z / 2 >
                center_z + size_z / 2) {
            i++;
            continue;
        }
        resultCallback(ot, node->items[i].ptr, userdata);
        i++;
    }
    // Abort if no levels below to descend:
    if (!node->child_neg)
        return;
    // See where we might need to descend:
    if (node->divided_x) {
        if (center_x - size_x / 2 <= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_neg, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
        if (center_x + size_x / 2 >= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_pos, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
    } else if (node->divided_y) {
        if (center_y - size_y / 2 <= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_neg, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
        if (center_y + size_y / 2 >= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_pos, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
    } else if (node->divided_z) {
        if (center_z - size_z / 2 <= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_neg, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
        if (center_z + size_z / 2 >= node->divided_at) {
            _octree_QueryNode(
                ot, node->child_pos, center_x, center_y, center_z,
                size_x, size_y, size_z, resultCallback, userdata
            );
        }
    } else {
        fprintf(stderr,
            "octree.c: INVALID OCTREE NODE divided but "
            "no axis marked for division...?"
        );
        fflush(stderr);
    }
}


void octree_Query(
        octree *ot,
        double center_x, double center_y, double center_z,
        double size_x, double size_y, double size_z,
        void (*resultCallback)(octree *node, void *objptr, void *userdata),
        void *userdata
        ) {
    if (!ot->node)
        return;
    _octree_QueryNode(
        ot, ot->node, center_x, center_y, center_z,
        size_x, size_y, size_z, resultCallback, userdata
    );
}


static int _octree_DivideNode(
        octreenode *node
        ) {
    if (node->child_neg)
        return 1;
    int xdivide_result = 0;
    int ydivide_result = 0;
    int zdivide_result = 0;
    double dividepos_result = 0;
    _octree_FindDivide(
        node, &xdivide_result, &ydivide_result, &zdivide_result,
        &dividepos_result
    );
    if (!xdivide_result && !ydivide_result && !zdivide_result)
        return 0;  // shouldn't happen, abort.
    octreenode *childneg = malloc(sizeof(*childneg));
    if (!childneg)
        return 0;
    octreenode *childpos = malloc(sizeof(*childpos));
    if (!childpos) {
        free(childneg);
        return 0;
    }
    memset(childneg, 0, sizeof(*childneg));
    memset(childpos, 0, sizeof(*childpos));
    node->child_neg = childneg;
    node->child_pos = childpos;
    node->divided_x = xdivide_result;
    node->divided_y = ydivide_result;
    node->divided_z = zdivide_result;
    node->divided_at = dividepos_result;
    int i = 0;
    while (i < node->item_count) {
        int sink_success = 0;
        if (xdivide_result) {
            if (node->items[i].center_x +
                    node->items[i].size_x * 0.501 <
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_neg, &node->items[i]
                );
            } else if (node->items[i].center_x -
                    node->items[i].size_x * 0.501 >
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_pos, &node->items[i]
                );
            }
        } else if (ydivide_result) {
            if (node->items[i].center_y +
                    node->items[i].size_y * 0.501 <
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_neg, &node->items[i]
                );
            } else if (node->items[i].center_y -
                    node->items[i].size_y * 0.501 >
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_pos, &node->items[i]
                );
            }
        } else if (zdivide_result) {
            if (node->items[i].center_z +
                    node->items[i].size_z * 0.501 <
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_neg, &node->items[i]
                );
            } else if (node->items[i].center_z -
                    node->items[i].size_z * 0.501 >
                    dividepos_result) {
                sink_success = _octree_SinkitemIntoNode(
                    node->child_pos, &node->items[i]
                );
            }
        }
        if (sink_success) {
            if (i < node->item_count - 1) {
                memmove(
                    node->items + i,
                    node->items + i + 1,
                    sizeof(*node->items) * (
                        node->item_count - 1 - i
                    )
                );
                int k = i;
                while (k < node->item_count - 1) {
                    if (node->items[k].ainfo)
                        node->items[k].ainfo->correspondingitemindex = k;
                    k++;
                }
            }
            node->item_count--;
        } else {
            i++;
        }
    }
    return 1;
}


void octree_RemoveItem(
        octree *ot, void *objptr,
        octreeitem_assigninfo *iteminfo
        ) {
    if (!iteminfo || !ot ||
            !iteminfo->assignednode ||
            iteminfo->correspondingitemindex < 0) {
        if (iteminfo)
            free(iteminfo);
        return;
    }
    octreenode *node = iteminfo->assignednode;
    assert(iteminfo->correspondingitemindex >= 0 &&
           iteminfo->correspondingitemindex < node->item_count);
    int i = iteminfo->correspondingitemindex;
    assert(node->items[i].ptr == objptr);
    if (i < node->item_count - 1) {
        memmove(
            node->items + i,
            node->items + i + 1,
            sizeof(*node->items) * (
                node->item_count - 1 - i
            )
        );
        int k = i;
        while (k < node->item_count - 1) {
            if (node->items[k].ainfo)
                node->items[k].ainfo->correspondingitemindex = k;
            k++;
        }
    }
    node->item_count--;
    free(iteminfo);
}


octreeitem_assigninfo *octree_AddItem(
        octree *ot,
        double center_x, double center_y, double center_z,
        double size_x, double size_y, double size_z,
        void *ptr
        ) {
    octreeitem i;
    memset(&i, 0, sizeof(i));
    i.center_x = center_x;
    i.center_y = center_y;
    i.center_z = center_z;
    i.size_x = size_x;
    i.size_y = size_y;
    i.size_z = size_z;
    i.ptr = ptr;
    i.ainfo = malloc(sizeof(*i.ainfo));
    if (!i.ainfo) {
        return NULL;
    }
    memset(i.ainfo, 0, sizeof(*i.ainfo));
    i.ainfo->correspondingitemindex = -1;
    if (!ot->node) {
        ot->node = malloc(sizeof(*ot->node));
        if (!ot->node) {
            free(i.ainfo);
            return NULL;
        }
        memset(ot->node, 0, sizeof(*ot->node));
    }
    if (!_octree_SinkitemIntoNode(ot->node, &i)) {
        free(i.ainfo);
        return NULL;
    }
    return i.ainfo;
}
