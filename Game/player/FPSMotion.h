#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace FPSMotion {
struct Settings {
    float cameraHeight = 1.6f;
    float mouseSensitivity = 0.0025f; // radians per mouse pixel (not per second)
    float moveSpeed = 5.0f;          // world units per second
};
inline void Look(Transform& transform, float dx, float dy, const Settings& settings) {
    constexpr float pitchLimit = 89.0f * std::numbers::pi_v<float> / 180.0f;
    transform.rotate.y = std::remainder(transform.rotate.y + dx * settings.mouseSensitivity,
        2.0f * std::numbers::pi_v<float>);
    transform.rotate.x = std::clamp(transform.rotate.x + dy * settings.mouseSensitivity,
        -pitchLimit, pitchLimit);
}
inline void Move(Transform& transform, float right, float forward, float dt, const Settings& settings) {
    const float length = std::sqrt(right * right + forward * forward);
    if (length == 0.0f || dt <= 0.0f) return;
    right /= length;
    forward /= length;
    // Matches Camera's MakeAffineMatrix / RotateXYZ convention. Pitch never moves Y.
    const float sine = std::sin(transform.rotate.y);
    const float cosine = std::cos(transform.rotate.y);
    const float distance = settings.moveSpeed * dt;
    transform.translate.x += (right * cosine + forward * sine) * distance;
    transform.translate.z += (-right * sine + forward * cosine) * distance;
}
}
