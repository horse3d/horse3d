#ifndef HORSE_UIPLANE_H_
#define HORSE_UIPLANE_H_


typedef struct h3duiobject h3duiobject;

typedef struct h3duiplane {
    uint64_t id;
    int parented;
    h3duiobject *parentobject;

    int allow_fractional_positions;
    int objects_count;
    h3duiobject **objects;
    double width, height;
    double offset_x, offset_y;

    int has3dplacement;
    double pos3d[3];
    double quat3d[4];
    double width3d, height3d;
} h3duiplane;

h3duiplane *uiplane_New();

void uiplane_Destroy(h3duiplane *plane);

int uiplane_AddObject(h3duiplane *plane, h3duiobject *obj);

void uiplane_RemoveObject(h3duiplane *plane, h3duiobject *obj);

void uiplane_UpdateObjectSort(h3duiplane *plane, h3duiobject *obj);

extern h3duiplane *defaultplane;

int uiplane_Draw(
    h3duiplane *plane, double additionaloffsetx, double additionaloffsety
);

int uiplane_DrawScreenPlanes();

void uiplane_ResizeScreenPlanes(int width, int height);

h3duiplane *uiplane_GetPlaneById(uint64_t id);

int uiplane_MarkAsUnparented(h3duiplane *plane);

int uiplane_MarkAsParented(h3duiplane *plane);

#endif  // HORSE_UIPLANE_H_
