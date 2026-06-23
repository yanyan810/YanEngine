#include "TestSceneKnockbackPreview.h"

#include "Enemy.h"
#include "EnemyManager.h"
#include "Object3d.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kRadToDeg = 57.29577951308232f;

float Length3(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
    const float len = Length3(v);
    if (len <= 1.0e-6f) {
        return fallback;
    }
    return { v.x / len, v.y / len, v.z / len };
}

float SolveTimeToY(float startY, float velocityY, float gravity, float targetY) {
    const float g = std::max(gravity, 0.0001f);
    const float height = startY - targetY;
    const float disc = velocityY * velocityY + 2.0f * g * height;
    if (disc < 0.0f) {
        return 0.0f;
    }
    return std::max(0.0f, (velocityY + std::sqrt(disc)) / g);
}

float SolvePositiveBoundaryTime(float start, float velocity, float boundary) {
    if (std::abs(velocity) <= 1.0e-6f) {
        return -1.0f;
    }
    const float t = (boundary - start) / velocity;
    return t > 0.0f ? t : -1.0f;
}

} // namespace

MeleeKind TestSceneKnockbackPreview::KindFromIndex(int index) {
    if (index == 0) {
        return MeleeKind::Normal;
    }
    if (index == 1) {
        return MeleeKind::Land;
    }
    return MeleeKind::Rush;
}

TestSceneKnockbackPreview::Metrics TestSceneKnockbackPreview::Calculate(
    const Player& player,
    const EnemyManager& enemyManager,
    MeleeKind kind,
    float percent,
    bool outOfBoundsEnabled,
    float outLeftX,
    float outRightX,
    float outBottomY) {
    return Calculate(
        player,
        enemyManager,
        enemyManager.BossAttackIndex(kind),
        percent,
        outOfBoundsEnabled,
        outLeftX,
        outRightX,
        outBottomY);
}

TestSceneKnockbackPreview::Metrics TestSceneKnockbackPreview::Calculate(
    const Player& player,
    const EnemyManager& enemyManager,
    size_t attackIndex,
    float percent,
    bool outOfBoundsEnabled,
    float outLeftX,
    float outRightX,
    float outBottomY) {

    Metrics m{};
    const EnemyManager::BossHitTuning& tuning = enemyManager.BossAttackAt(attackIndex).hit;
    m.power = tuning.baseKnockback + percent * tuning.knockbackScale;

    Vector3 dir = tuning.knockbackDir;
    if (const Enemy* boss = enemyManager.GetBoss()) {
        const float dirX = (player.GetX() >= boss->GetPos3D().x) ? 1.0f : -1.0f;
        dir.x = std::abs(dir.x) * dirX;
    }
    m.direction = NormalizeOr(dir, { 1.0f, 0.0f, 0.0f });
    m.velocity = {
        m.direction.x * m.power,
        m.direction.y * m.power,
        m.direction.z * m.power,
    };

    const Vector3 start = player.GetPos3D();
    const float gravity = player.GetGravity();
    m.airTimeSec = SolveTimeToY(start.y, m.velocity.y, gravity, 0.0f);
    m.travelX = m.velocity.x * m.airTimeSec;
    m.travelZ = m.velocity.z * m.airTimeSec;
    m.groundDistance = std::sqrt(m.travelX * m.travelX + m.travelZ * m.travelZ);
    m.straightDistance = std::sqrt(
        m.travelX * m.travelX +
        start.y * start.y +
        m.travelZ * m.travelZ);
    m.landingPos = {
        start.x + m.travelX,
        0.0f,
        start.z + m.travelZ,
    };

    const float horizontalSpeed = std::sqrt(m.velocity.x * m.velocity.x + m.velocity.z * m.velocity.z);
    m.launchAngleDeg = std::atan2(m.velocity.y, horizontalSpeed) * kRadToDeg;
    m.signedScreenAngleDeg = std::atan2(m.velocity.y, m.velocity.x) * kRadToDeg;
    if (m.velocity.y > 0.0f) {
        const float apexAdd = (m.velocity.y * m.velocity.y) / (2.0f * std::max(gravity, 0.0001f));
        m.maxHeightY = start.y + apexAdd;
    } else {
        m.maxHeightY = start.y;
    }

    if (outOfBoundsEnabled) {
        float firstOutTime = -1.0f;

        const float sideTime = SolvePositiveBoundaryTime(
            start.x,
            m.velocity.x,
            m.velocity.x >= 0.0f ? outRightX : outLeftX);
        if (sideTime >= 0.0f) {
            firstOutTime = sideTime;
        }

        const float bottomTime = SolveTimeToY(start.y, m.velocity.y, gravity, outBottomY);
        if (bottomTime > 0.0f && (firstOutTime < 0.0f || bottomTime < firstOutTime)) {
            firstOutTime = bottomTime;
        }

        if (firstOutTime >= 0.0f && firstOutTime < m.airTimeSec) {
            m.reachesOutBeforeLanding = true;
            m.outTimeSec = firstOutTime;
            m.outPos = {
                start.x + m.velocity.x * firstOutTime,
                start.y + m.velocity.y * firstOutTime - 0.5f * gravity * firstOutTime * firstOutTime,
                start.z + m.velocity.z * firstOutTime,
            };
            const float outDx = m.outPos.x - start.x;
            const float outDz = m.outPos.z - start.z;
            m.outDistance = std::sqrt(outDx * outDx + outDz * outDz);
        }
    }

    return m;
}

void TestSceneKnockbackPreview::SetLineSegment(
    Object3d& line,
    const Vector3& start,
    const Vector3& end,
    float thickness,
    float dt) {
    const Vector3 v{
        end.x - start.x,
        end.y - start.y,
        end.z - start.z,
    };
    const float length = Length3(v);
    const Vector3 mid{
        start.x + v.x * 0.5f,
        start.y + v.y * 0.5f,
        start.z + v.z * 0.5f,
    };

    line.SetTranslate(mid);
    line.SetRotate({ 0.0f, 0.0f, std::atan2(v.y, v.x) });
    line.SetScale({ std::max(length * 0.5f, 0.001f), thickness, thickness });
    line.Update(dt);
}
