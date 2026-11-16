/*
 * Unit quaternions for rigid-body attitude.
 *
 * Not an NR topic; Soft cites:
 *   - Hamilton (quaternion algebra)
 *   - Shoemake, Animating Rotation with Quaternion Curves, SIGGRAPH 1985
 *   - Hairer et al., Geometric Numerical Integration (renormalisation after RK)
 */
#include "squirrelnum.h"

#include <math.h>

void sqn_quat_identity(SqnQuat *q) {
    if (!q) return;
    q->w = 1.0;
    q->x = q->y = q->z = 0.0;
}

double sqn_quat_norm(const SqnQuat *q) {
    if (!q) return 0.0;
    return sqrt(q->w * q->w + q->x * q->x + q->y * q->y + q->z * q->z);
}

void sqn_quat_normalize(SqnQuat *q) {
    if (!q) return;
    double n = sqn_quat_norm(q);
    if (n < 1e-30) {
        sqn_quat_identity(q);
        return;
    }
    q->w /= n;
    q->x /= n;
    q->y /= n;
    q->z /= n;
}

void sqn_quat_mul(const SqnQuat *a, const SqnQuat *b, SqnQuat *out) {
    SqnQuat r;
    r.w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
    r.x = a->w * b->x + a->x * b->w + a->y * b->z - a->z * b->y;
    r.y = a->w * b->y - a->x * b->z + a->y * b->w + a->z * b->x;
    r.z = a->w * b->z + a->x * b->y - a->y * b->x + a->z * b->w;
    *out = r;
}

void sqn_quat_deriv(const SqnQuat *q, double wx, double wy, double wz,
                    SqnQuat *dq) {
    SqnQuat wq = {0.0, wx, wy, wz};
    SqnQuat prod;
    sqn_quat_mul(q, &wq, &prod);
    dq->w = 0.5 * prod.w;
    dq->x = 0.5 * prod.x;
    dq->y = 0.5 * prod.y;
    dq->z = 0.5 * prod.z;
}

void sqn_quat_from_euler_zyx(double yaw, double pitch, double roll, SqnQuat *q) {
    double cy = cos(yaw * 0.5), sy = sin(yaw * 0.5);
    double cp = cos(pitch * 0.5), sp = sin(pitch * 0.5);
    double cr = cos(roll * 0.5), sr = sin(roll * 0.5);
    q->w = cr * cp * cy + sr * sp * sy;
    q->x = sr * cp * cy - cr * sp * sy;
    q->y = cr * sp * cy + sr * cp * sy;
    q->z = cr * cp * sy - sr * sp * cy;
    sqn_quat_normalize(q);
}

void sqn_quat_to_euler_zyx(const SqnQuat *q, double *yaw, double *pitch,
                           double *roll) {
    double sinp = 2.0 * (q->w * q->y - q->z * q->x);
    if (sinp > 1.0) sinp = 1.0;
    if (sinp < -1.0) sinp = -1.0;
    if (pitch) *pitch = asin(sinp);
    if (roll)
        *roll = atan2(2.0 * (q->w * q->x + q->y * q->z),
                      1.0 - 2.0 * (q->x * q->x + q->y * q->y));
    if (yaw)
        *yaw = atan2(2.0 * (q->w * q->z + q->x * q->y),
                     1.0 - 2.0 * (q->y * q->y + q->z * q->z));
}
