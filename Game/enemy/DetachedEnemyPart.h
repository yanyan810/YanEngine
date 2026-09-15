#pragma once
#include "EnemyParts.h"

struct DetachedPartSettings {
    float launchPower = 6.0f;
    float upwardPower = 2.0f;
    float gravity = -9.8f;
    float lifeTime = 5.0f;
    float bounce = .25f;
    float angularVelocityScale = 1.0f;
    float groundHeight = 0.0f;
};
inline float DetachedPartWeight(EnemyPartType type) {
    if (type == EnemyPartType::Head) return 1.1f;
    if (type == EnemyPartType::Body) return .5f;
    if (type == EnemyPartType::LeftLeg || type == EnemyPartType::RightLeg) return .8f;
    return 1.0f;
}
struct DetachedPartMotion {
    Vector3 position{}, velocity{}, rotation{}, angularVelocity{}, scale{1,1,1};
    AABB bounds{};
    Vector3 pivot{};
    DetachedPartSettings settings{};
    float age = 0;
    bool settled = false;
    bool Active() const { return age < settings.lifeTime; }
    Vector3 Translation() const {
        const auto rs = Matrix4x4::MakeAffineMatrix(scale, rotation, {});
        return position - EnemyPartTransformPoint(pivot, rs);
    }
    void Initialize(const AABB& localBounds, const Vector3& translation,
        const Vector3& rotate, const Vector3& size, const Vector3& direction,
        EnemyPartType type, const Vector3& spin, const DetachedPartSettings& config) {
        bounds = localBounds; pivot = (bounds.min+bounds.max)*.5f; rotation = rotate; scale = size; settings = config;
        position = EnemyPartTransformPoint((bounds.min+bounds.max)*.5f,
            Matrix4x4::MakeAffineMatrix(scale, rotation, translation));
        const float length = std::hypot(direction.x,direction.y,direction.z);
        const Vector3 unit = std::isfinite(length) && length > 0 ? direction*(1.0f/length) : Vector3{};
        velocity = (unit*settings.launchPower + Vector3{0,settings.upwardPower,0})*DetachedPartWeight(type);
        angularVelocity = spin*settings.angularVelocityScale;
        age = 0; settled = false;
    }
    void Update(float dt) {
        if (!std::isfinite(dt) || dt <= 0 || !Active()) return;
        float remaining = std::min(dt, settings.lifeTime-age);
        age = std::min(settings.lifeTime, age+dt);
        // Bounded small steps keep contact stable at different rendering rates.
        while (remaining > 0 && !settled) {
            const float h = std::min(remaining, 1.0f/120.0f);
            remaining -= h;
            position = position + velocity*h + Vector3{0,.5f*settings.gravity*h*h,0};
            velocity.y += settings.gravity*h;
            rotation = rotation + angularVelocity*h;
            const auto world = Matrix4x4::MakeAffineMatrix(scale, rotation, Translation());
            const float bottom = TransformAABB(bounds, world).min.y;
            if (bottom < settings.groundHeight) {
                position.y += settings.groundHeight-bottom;
                if (velocity.y < 0) {
                    velocity.y *= -settings.bounce;
                    velocity.x *= .7f; velocity.z *= .7f;
                    angularVelocity = angularVelocity*.7f;
                    if (velocity.y < .5f) { velocity = {}; angularVelocity = {}; settled = true; }
                }
            }
        }
    }
};
