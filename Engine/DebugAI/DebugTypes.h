#pragma once

#include "../math/Vector3.h"

#include <string>
#include <vector>

struct DebugAction {
    std::string name;
    std::string targetId;
    int intParam = 0;
    float floatParam = 0.0f;
};

struct DebugMapBounds {
    Vector3 min = { -1000.0f, -1000.0f, -1000.0f };
    Vector3 max = { 1000.0f, 1000.0f, 1000.0f };
    bool enabled = false;
};

struct DebugEnemyState {
    std::string type;
    int hp = 0;
    Vector3 position = {};
    bool pendingSpawn = false;
    float spawnDelay = 0.0f;
};

struct DebugGameState {
    std::string sceneName;
    unsigned long long frameNumber = 0;

    int playerHp = 0;
    int enemyHp = 0;
    int enemyCount = 0;
    Vector3 playerPosition = {};
    float fps = 60.0f;
    std::string gamePhase;
    std::vector<DebugEnemyState> enemies;

    std::vector<DebugAction> availableActions;
    DebugMapBounds mapBounds;

    std::string stableStateKey;
    std::string progressKey;
};

enum class DebugIssueSeverity {
    Info,
    Warning,
    Error,
};

struct DebugIssue {
    DebugIssueSeverity severity = DebugIssueSeverity::Warning;
    std::string message;
    unsigned long long frameNumber = 0;
    std::string sceneName;
    DebugAction lastAction;
    std::string replayPath;
};

