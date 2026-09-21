#pragma once
#include "Raycast.h"
#include <array>
#include <algorithm>
#include <limits>

enum class EnemyPartType { None, Head, Body, LeftArm, RightArm, LeftLeg, RightLeg };
inline const char* EnemyPartName(EnemyPartType type) {
    switch(type) {
    case EnemyPartType::Head: return "Head";
    case EnemyPartType::Body: return "Body";
    case EnemyPartType::LeftArm: return "LeftArm";
    case EnemyPartType::RightArm: return "RightArm";
    case EnemyPartType::LeftLeg: return "LeftLeg";
    case EnemyPartType::RightLeg: return "RightLeg";
    default: return "None";
    }
}
enum class EnemyPartDamageState { Normal, LightDamage, HeavyDamage, Critical, Destroyed };
inline const char* EnemyPartDamageStateName(EnemyPartDamageState state) {
    switch (state) {
    case EnemyPartDamageState::Normal: return "Normal";
    case EnemyPartDamageState::LightDamage: return "LightDamage";
    case EnemyPartDamageState::HeavyDamage: return "HeavyDamage";
    case EnemyPartDamageState::Critical: return "Critical";
    default: return "Destroyed";
    }
}
inline float EnemyPartMaxHp(EnemyPartType type) {
    switch (type) {
    case EnemyPartType::Head: return 50.0f;
    case EnemyPartType::Body: return 100.0f;
    case EnemyPartType::LeftArm: case EnemyPartType::RightArm: return 60.0f;
    case EnemyPartType::LeftLeg: case EnemyPartType::RightLeg: return 70.0f;
    default: return 0.0f;
    }
}
struct EnemyPart {
    EnemyPartType type = EnemyPartType::None;
    AABB bounds{};
    float flashRemaining = 0.0f;
    float maxHp = EnemyPartMaxHp(type);
    float hp = maxHp;
    float DamageRate() const { return maxHp > 0 ? std::clamp(1.0f - hp / maxHp, 0.0f, 1.0f) : 1.0f; }
    EnemyPartDamageState DamageState() const {
        if (hp <= 0) return EnemyPartDamageState::Destroyed;
        const float rate = DamageRate();
        if (rate >= .75f) return EnemyPartDamageState::Critical;
        if (rate >= .50f) return EnemyPartDamageState::HeavyDamage;
        if (rate >= .25f) return EnemyPartDamageState::LightDamage;
        return EnemyPartDamageState::Normal;
    }
};
using EnemyParts = std::array<EnemyPart, 6>;
// Return actual HP lost (overkill and already-destroyed hits are clamped).
inline float DamageEnemyPart(EnemyParts& parts, EnemyPartType type, float damage) {
    if (!std::isfinite(damage) || damage <= 0 || type == EnemyPartType::None) return 0;
    for (auto& part : parts) {
        if (part.type != type) continue;
        const float before = part.hp;
        part.hp = std::max(0.0f, before - damage);
        return before - part.hp;
    }
    return 0;
}
struct EnemyPartHit {
    bool hit = false;
    float distance = 0.0f;
    Vector3 position{};
    EnemyPartType part = EnemyPartType::None;
};
inline Vector3 EnemyPartTransformPoint(const Vector3& p, const Matrix4x4& m) {
    return {p.x*m.m[0][0]+p.y*m.m[1][0]+p.z*m.m[2][0]+m.m[3][0],
        p.x*m.m[0][1]+p.y*m.m[1][1]+p.z*m.m[2][1]+m.m[3][1],
        p.x*m.m[0][2]+p.y*m.m[1][2]+p.z*m.m[2][2]+m.m[3][2]};
}
inline EnemyParts MakeEnemyParts(const AABB& model) {
    // Boss bind pose: local Y is up, Z is arm span, X is depth.
    // Left/right are the enemy's own sides (+Z = left), not camera-relative.
    const auto box = [&](float y0,float y1,float z0,float z1) {
        const Vector3 size=model.max-model.min;
        return AABB{{model.min.x,model.min.y+size.y*y0,model.min.z+size.z*z0},
            {model.max.x,model.min.y+size.y*y1,model.min.z+size.z*z1}};
    };
    return {{{EnemyPartType::Head,box(.82f,1,.38f,.62f)},
        {EnemyPartType::Body,box(.45f,.82f,.38f,.62f)},
        {EnemyPartType::LeftArm,box(.70f,.86f,.62f,1)},
        {EnemyPartType::RightArm,box(.70f,.86f,0,.38f)},
        {EnemyPartType::LeftLeg,box(0,.45f,.50f,.64f)},
        {EnemyPartType::RightLeg,box(0,.45f,.36f,.50f)}}};
}
inline bool RaycastEnemyParts(const EnemyParts& parts, const Matrix4x4& world,
    const Vector3& origin,const Vector3& direction,float range,EnemyPartHit& hit) {
    hit = {};
    const float length=std::hypot(direction.x,direction.y,direction.z);
    if (!std::isfinite(length)||length<=0||!std::isfinite(range)||range<0) return false;
    // Reject singular transforms before inversion (including collapsed scale axes).
    const float determinant=world.m[0][0]*(world.m[1][1]*world.m[2][2]-world.m[1][2]*world.m[2][1])
        -world.m[0][1]*(world.m[1][0]*world.m[2][2]-world.m[1][2]*world.m[2][0])
        +world.m[0][2]*(world.m[1][0]*world.m[2][1]-world.m[1][1]*world.m[2][0]);
    if (!std::isfinite(determinant)||std::abs(determinant)<1e-8f) return false;
    const auto inverse=Matrix4x4::Inverse(world);
    const Vector3 unit=direction*(1.0f/length);
    const Vector3 localOrigin=EnemyPartTransformPoint(origin,inverse);
    const Vector3 localDirection{unit.x*inverse.m[0][0]+unit.y*inverse.m[1][0]+unit.z*inverse.m[2][0],
        unit.x*inverse.m[0][1]+unit.y*inverse.m[1][1]+unit.z*inverse.m[2][1],
        unit.x*inverse.m[0][2]+unit.y*inverse.m[1][2]+unit.z*inverse.m[2][2]};
    const float factor=std::hypot(localDirection.x,localDirection.y,localDirection.z);
    if (!std::isfinite(factor)||factor<=0) return false;
    float closest=range;
    for (const auto& part:parts) {
        // Removed body parts must not occlude targets behind their former position.
        if (part.DamageState() == EnemyPartDamageState::Destroyed) continue;
        float localDistance;
        if (!RaycastAABB(localOrigin,localDirection,part.bounds,closest*factor,localDistance)) continue;
        const float distance=localDistance/factor;
        if (hit.hit && distance>=closest) continue;
        closest=distance;
        hit={true,distance,origin+unit*distance,part.type};
    }
    return hit.hit;
}
