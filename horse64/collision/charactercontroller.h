#ifndef HORSE3D_CHARACTERCONTROLLER_H_
#define HORSE3D_CHARACTERCONTROLLER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "colworld.h"
#include "../world.h"

typedef struct h3dcolcharacter h3dcolcharacter;

h3dcolcharacter *charactercontroller_New(
    h3dobject *obj, double step_height_scalar
);

void charactercontroller_Update(h3dcolcharacter *colchar);

void charactercontroller_Destroy(h3dcolcharacter *colchar);

void charactercontroller_ApplyForce(
    h3dcolcharacter *colchar, double *force
);

int charactercontroller_GetOnFloor(
    h3dcolcharacter *colchar
);

int charactercontroller_GetOnSlope(
    h3dcolcharacter *colchar
);

double charactercontroller_ScaleBasedForcesFactor(
    h3dcolcharacter *charobj
);

void charactercontroller_UpdateMassAfterScaling(
    h3dcolcharacter *colchar
);

#ifdef __cplusplus
}
#endif

#endif  // HORSE3D_CHARACTERCONTROLLER_H_
