#include "BasicCombatDebugBot.h"

#include <cmath>
#include <limits>

namespace {

float Abs(float value) {
    return std::fabs(value);
}

float DistanceSq2D(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

bool IsCombatEntity(const DebugEntityState& entity) {
    return entity.alive &&
        !entity.pending &&
        entity.category != "PendingSpawn" &&
        (entity.category == "Enemy" || entity.category == "Boss" || entity.type == "Boss");
}

}

bool BasicCombatDebugBot::ChooseAction(const DebugGameState& state, DebugAction& outAction) {
    if (state.availableActions.empty()) {
        return false;
    }

    if (TryChooseWallEscape_(state, outAction)) {
        return true;
    }

    if (TryChooseEnemyAction_(state, outAction)) {
        return true;
    }

    if (HasAction_(state, "Wait")) {
        outAction = { "Wait" };
        return true;
    }

    outAction = state.availableActions.front();
    return true;
}

bool BasicCombatDebugBot::HasAction_(const DebugGameState& state, const char* actionName) const {
    for (const DebugAction& action : state.availableActions) {
        if (action.name == actionName) {
            return true;
        }
    }
    return false;
}

bool BasicCombatDebugBot::TryChooseWallEscape_(const DebugGameState& state, DebugAction& outAction) const {
    if (!state.mapBounds.enabled || !HasAction_(state, "Move")) {
        return false;
    }

    constexpr float kWallMarginX = 2.5f;
    constexpr float kWallMarginZ = 2.5f;
    const Vector3& pos = state.playerPosition;

    int horizontal = 0;
    int depth = 0;

    if (pos.x <= state.mapBounds.min.x + kWallMarginX) {
        horizontal = +1;
    } else if (pos.x >= state.mapBounds.max.x - kWallMarginX) {
        horizontal = -1;
    }

    if (pos.z <= state.mapBounds.min.z + kWallMarginZ) {
        depth = +1;
    } else if (pos.z >= state.mapBounds.max.z - kWallMarginZ) {
        depth = -1;
    }

    if (horizontal == 0 && depth == 0) {
        return false;
    }

    outAction = { "Move" };
    outAction.intParam = horizontal;
    outAction.floatParam = static_cast<float>(depth);
    outAction.holdFrames = 8;
    return true;
}

bool BasicCombatDebugBot::TryChooseEnemyAction_(const DebugGameState& state, DebugAction& outAction) const {
    const DebugEntityState* nearest = nullptr;
    float nearestDistanceSq = std::numeric_limits<float>::max();

    for (const DebugEntityState& entity : state.entities) {
        if (!IsCombatEntity(entity)) {
            continue;
        }

        const float distanceSq = DistanceSq2D(state.playerPosition, entity.position);
        if (distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            nearest = &entity;
        }
    }

    if (nearest == nullptr) {
        return false;
    }

    const float dx = nearest->position.x - state.playerPosition.x;
    const float dz = nearest->position.z - state.playerPosition.z;
    constexpr float kAttackRangeX = 2.4f;
    constexpr float kAttackRangeZ = 2.8f;

    if (Abs(dx) <= kAttackRangeX && Abs(dz) <= kAttackRangeZ) {
        if (HasAction_(state, "AttackWeak")) {
            outAction = { "AttackWeak" };
            outAction.targetId = nearest->id;
            outAction.intParam = dx > 0.2f ? +1 : (dx < -0.2f ? -1 : 0);
            outAction.holdFrames = 12;
            return true;
        }
        if (HasAction_(state, "AttackTilt")) {
            outAction = { "AttackTilt" };
            outAction.targetId = nearest->id;
            outAction.intParam = dx >= 0.0f ? +1 : -1;
            outAction.holdFrames = 12;
            return true;
        }
    }

    if (HasAction_(state, "Move")) {
        outAction = { "Move" };
        outAction.targetId = nearest->id;
        outAction.intParam = dx > 0.4f ? +1 : (dx < -0.4f ? -1 : 0);
        outAction.floatParam = dz > 0.4f ? +1.0f : (dz < -0.4f ? -1.0f : 0.0f);
        outAction.holdFrames = 8;
        return true;
    }

    return false;
}
