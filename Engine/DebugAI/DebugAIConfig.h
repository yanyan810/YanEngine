#pragma once

#include <string>

struct DebugAIConfig {
    std::string logDirectory = "generated/debug_ai";

    bool detectNegativeHp = true;
    bool detectInvalidCounts = true;
    bool detectInvalidPosition = true;
    bool detectMapBounds = true;
    bool detectSameState = true;
    bool detectNoProgress = true;
    bool detectLowFps = true;

    float sameStateLimitSeconds = 10.0f;
    float noProgressLimitSeconds = 15.0f;
    float lowFpsLimitSeconds = 3.0f;
    float lowFpsThreshold = 30.0f;
    unsigned long long duplicateIssueCooldownFrames = 60;
};
