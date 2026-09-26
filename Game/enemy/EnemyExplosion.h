#pragma once
#include "EnemyDefinition.h"
#include "EnemyParts.h"
#include <optional>

struct EnemyExplosion {
    Vector3 center{};
    float radius = 4;
    float damage = 25;
};

struct EnemyBulletHitResult {
    float damage = 0; // Actual HP lost to the bullet, for shot diagnostics.
    std::optional<EnemyExplosion> explosion;
};

inline bool IsBomberDetonationHit(EnemyType type, EnemyPartType part, float damage, bool dead) {
    return type == EnemyType::Bomber && part == EnemyPartType::Body &&
        !dead && std::isfinite(damage) && damage > 0;
}

// Spherical blast against an upright movement cylinder, including vertical separation.
// Full damage once per actor; visual scale does not alter movement collision dimensions.
inline float EnemyExplosionDamage(const EnemyExplosion& blast, const Vector3& feet,
    float radius, float height) {
    if (!std::isfinite(blast.radius) || blast.radius <= 0 ||
        !std::isfinite(blast.damage) || blast.damage <= 0) return 0;
    // 足元の一点ではなく当たり判定の円柱までの最短距離を使い、体の端に届いた爆風も命中扱いにする。
    const float horizontal = std::max(0.0f,
        std::hypot(feet.x - blast.center.x, feet.z - blast.center.z) - radius);
    const float vertical = blast.center.y - std::clamp(blast.center.y, feet.y, feet.y + height);
    return std::hypot(horizontal, vertical) <= blast.radius ? blast.damage : 0;
}
