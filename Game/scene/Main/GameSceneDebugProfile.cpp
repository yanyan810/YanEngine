#include "GameSceneDebugProfile.h"

#include <cmath>

const char* ToDebugPhaseName(GameSceneDebugPhase phase) {
    switch (phase) {
    case GameSceneDebugPhase::IntroVideo:
        return "IntroVideo";
    case GameSceneDebugPhase::Battle:
        return "Battle";
    case GameSceneDebugPhase::OutroVideo:
        return "OutroVideo";
    case GameSceneDebugPhase::Unknown:
    default:
        return "Unknown";
    }
}

std::vector<DebugAction> BuildGameSceneDebugActions(GameSceneDebugPhase phase) {
    std::vector<DebugAction> actions = {
        { "Move" },
        { "Retreat" },
        { "DodgeAway" },
        { "Down" },
        { "Jump" },
        { "AttackWeak" },
        { "AttackTilt" },
        { "AttackSmash" },
        { "AttackNeutralSpecial" },
        { "AttackSideSpecial" },
        { "AttackUpSpecial" },
        { "AttackDownSpecial" },
        { "Guard" },
        { "Wait" },
    };

    if (phase == GameSceneDebugPhase::IntroVideo) {
        actions.push_back({ "SkipIntro" });
    }

    return actions;
}

DebugMapBounds BuildGameSceneDebugMapBounds() {
    DebugMapBounds bounds;
    bounds.enabled = true;
    bounds.min = { -20.0f, -1.0f, -15.0f };
    bounds.max = { 20.0f, 12.0f, 20.0f };
    return bounds;
}

std::string BuildGameSceneStableStateKey(const DebugGameState& state) {
    const Vector3 pos = state.playerPosition;
    return std::to_string(state.playerHp) + ":" +
        std::to_string(state.enemyHp) + ":" +
        std::to_string(state.enemyCount) + ":" +
        std::to_string(static_cast<int>(std::floor(pos.x))) + ":" +
        std::to_string(static_cast<int>(std::floor(pos.z)));
}

std::string BuildGameSceneProgressKey(const DebugGameState& state, GameSceneDebugPhase phase) {
    return state.sceneName + ":" +
        std::to_string(static_cast<int>(phase)) + ":" +
        std::to_string(state.enemyHp) + ":" +
        std::to_string(state.enemyCount);
}
