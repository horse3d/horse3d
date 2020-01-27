
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <math.h>
#include <mathc/mathc.h>
#include <string.h>

#include "scriptcore.h"
#include "scriptcoremath.h"


static double normangle(double n) {
    if (n < 0)
        n = 2 * M_PI - fmod((-n), 2 * M_PI);
    n = fmod((n + M_PI), 2 * M_PI) - M_PI;
    return n;
}

static double posnormangle(double n) {
    if (n < 0)
        n = 2 * M_PI - fmod((-n), 2 * M_PI);
    n = fmod(n, 2 * M_PI);
    return n;
}

void polygon3d_normal(
        const double pos1[3], const double pos2[3],
        const double pos3[3],
        double result[3]
        ) {
    double vecu[3];
    vecu[0] = pos2[0] - pos1[0];
    vecu[1] = pos2[1] - pos1[1];
    vecu[2] = pos2[2] - pos1[2];
    double vecv[3];
    vecv[0] = pos3[0] - pos1[0];
    vecv[1] = pos3[1] - pos1[1];
    vecv[2] = pos3[2] - pos1[2];
    result[0] = (vecu[1] * vecv[2]) - (vecu[2] * vecv[1]);
    result[1] = (vecu[2] * vecv[0]) - (vecu[0] * vecv[2]);
    result[2] = (vecu[0] * vecv[1]) - (vecu[1] * vecv[0]);
}

static inline void _quaternion_mult(
        double result[4], double q1[4], double q2[4]
        ) {
    double vnew[4];
    vnew[0] = q2[0] * q1[0] - q2[1] * q1[1] - q2[2] * q1[2] - q2[3] * q1[3];
    vnew[1] = q2[0] * q1[1] + q2[1] * q1[0] - q2[2] * q1[3] + q2[3] * q1[2];
    vnew[2] = q2[0] * q1[2] + q2[1] * q1[3] + q2[2] * q1[0] - q2[3] * q1[1];
    vnew[3] = q2[0] * q1[3] - q2[1] * q1[2] + q2[2] * q1[1] + q2[3] * q1[0];
    memcpy(result, vnew, sizeof(vnew));
}

double *vec3_rotate_quat(
        double *result, double *vec, double *quat
        ) {
    double q[4];
    memcpy(q, quat, sizeof(q));
    quat_normalize(q, q);
    double qswapped[4];
    qswapped[0] = q[3];
    qswapped[1] = q[0];
    qswapped[2] = q[1];
    qswapped[3] = q[2];

    double input_vec[4];
    memcpy(&input_vec[1], vec, sizeof(*vec) * 3);
    input_vec[0] = 0;
    double qswapped_conjugated[4];
    qswapped_conjugated[0] = q[3];
    qswapped_conjugated[1] = -q[0];
    qswapped_conjugated[2] = -q[1];
    qswapped_conjugated[3] = -q[2];

    double inbetweenresult[4];
    _quaternion_mult(
        inbetweenresult, qswapped, input_vec
    );
    double result_vec[4];
    _quaternion_mult(
        result_vec, inbetweenresult, qswapped_conjugated
    );

    memcpy(result, &result_vec[1], sizeof(*result_vec) * 3);
    return result;
}

double *quat_from_euler(double *result, double *eulerangles) {
    const double horiangle = (
        normangle(eulerangles[0])
    );
    const double vertiangle = (
        normangle(eulerangles[1])
    );
    const double rollangle = (
        normangle(eulerangles[2])
    );

    // Calculate horizontal and vertical rotation:
    const double cy = cos(horiangle * 0.5);
    const double sy = sin(horiangle * 0.5);
    const double cp = cos(0);
    const double sp = sin(0);
    const double cr = cos(vertiangle * 0.5);
    const double sr = sin(vertiangle * 0.5);
    double q[4];
    q[3] = cy * cp * cr + sy * sp * sr;
    q[0] = (cy * cp * sr - sy * sp * cr);
    q[1] = (sy * cp * sr + cy * sp * cr);
    q[2] = (sy * cp * cr - cy * sp * sr);
    quat_normalize(q, q);

    // Move along the roll axis:
    double yaxis[3];
    memset(yaxis, 0, sizeof(yaxis));
    yaxis[1] = 1.0;
    vec3_rotate_quat(yaxis, yaxis, q);
    vec3_normalize(yaxis, yaxis);

    // Circular roll:
    double q2[4];
    quat_from_axis_angle(q2, yaxis, rollangle);
    quat_normalize(q2, q2);
    quat_multiply(q, q2, q);
    quat_normalize(q, q);

    memcpy(result, q, sizeof(q));

    return result;
}
