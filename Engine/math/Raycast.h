#pragma once
#include "AABB.h"
#include <algorithm>
#include <cmath>

// Direction need not be normalized. Distance is in world units; inside starts hit at zero.
// On a miss, distance is unchanged.
inline bool RaycastAABB(const Vector3& origin, const Vector3& direction,
    const AABB& box, float maxDistance, float& distance) {
    const float length = std::hypot(direction.x, direction.y, direction.z);
    if (!std::isfinite(length) || length <= 0.0f || !std::isfinite(maxDistance) || maxDistance < 0.0f) return false;
    const float o[]{origin.x, origin.y, origin.z};
    const float d[]{direction.x / length, direction.y / length, direction.z / length};
    const float lo[]{box.min.x, box.min.y, box.min.z};
    const float hi[]{box.max.x, box.max.y, box.max.z};
    float enter = 0.0f;
    float exit = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(o[axis]) || !std::isfinite(lo[axis]) || !std::isfinite(hi[axis]) || lo[axis] > hi[axis]) return false;
        if (d[axis] == 0.0f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
            continue;
        }
        float entryDistance = (lo[axis] - o[axis]) / d[axis];
        float exitDistance = (hi[axis] - o[axis]) / d[axis];
        if (entryDistance > exitDistance) std::swap(entryDistance, exitDistance);
        enter = std::max(enter, entryDistance);
        exit = std::min(exit, exitDistance);
        if (enter > exit) return false;
    }
    distance = enter;
    return true;
}

// Bound all eight transformed corners; works with off-center meshes and rotation/scale.
inline AABB TransformAABB(const AABB& box, const Matrix4x4& world) {
    AABB result{};
    for (int corner = 0; corner < 8; ++corner) {
        const Vector3 point{(corner & 1) ? box.max.x : box.min.x,
            (corner & 2) ? box.max.y : box.min.y, (corner & 4) ? box.max.z : box.min.z};
        const Vector3 transformed{
            point.x * world.m[0][0] + point.y * world.m[1][0] + point.z * world.m[2][0] + world.m[3][0],
            point.x * world.m[0][1] + point.y * world.m[1][1] + point.z * world.m[2][1] + world.m[3][1],
            point.x * world.m[0][2] + point.y * world.m[1][2] + point.z * world.m[2][2] + world.m[3][2]};
        if (corner == 0) { result.min = result.max = transformed; continue; }
        result.min = {std::min(result.min.x, transformed.x), std::min(result.min.y, transformed.y), std::min(result.min.z, transformed.z)};
        result.max = {std::max(result.max.x, transformed.x), std::max(result.max.y, transformed.y), std::max(result.max.z, transformed.z)};
    }
    return result;
}
