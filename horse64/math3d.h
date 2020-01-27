#ifndef HORSE3D_MATH3D_H_
#define HORSE3D_MATH3D_H_

#include <math.h>

double *vec3_rotate_quat(
    double *result, double *vec, double *quat
);

double *quat_from_euler(
    double *result, double *eulerangles
);

void polygon3d_normal(
    const double pos1[3], const double pos2[3],
    const double pos3[3],
    double result[3]
);

static int isNaN(double f) {
    return (isnan(f) || (f != f));
}

#endif  // HORSE3D_MATH3D_H_
