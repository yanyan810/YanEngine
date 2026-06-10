#pragma once

#include "DebugLogger.h"
#include "DebugReplayPlayer.h"
#include "DebugReplayRecorder.h"
#include "IGameDebugAdapter.h"
#include "RandomDebugBot.h"

#include <memory>
#include <string>
#include <unordered_map>

class DebugAIManager {
public:
    void Initialize(const std::string& logDirectory = "generated/debug_ai");
    void Shutdown();

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }

    void SetAdapter(IGameDebugAdapter* adapter) { adapter_ = adapter; }
    void InjectAction();
    void ProcessAfterUpdate(float dt);
    void RecordExternalAction(
        const DebugGameState& stateBefore,
        const DebugAction& action,
        const DebugGameState& stateAfter);
    void CheckReplayDrift(const DebugGameState& actualState);

    bool StartLatestReplay();
    void StopReplay();
    bool IsReplayPlaying() const { return replayMode_ && replayPlayer_.IsPlaying(); }

    const DebugLogger& Logger() const { return logger_; }
    const DebugReplayRecorder& ReplayRecorder() const { return replayRecorder_; }
    const DebugReplayPlayer& ReplayPlayer() const { return replayPlayer_; }
    const DebugAction& LastAction() const { return lastAction_; }

private:
    void DetectIssues_(const DebugGameState& state, float dt);
    void AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message);
    bool IsFinite_(const Vector3& value) const;
    bool IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const;
    bool IsSameState_(const DebugGameState& state) const;

private:
    bool enabled_ = false;
    IGameDebugAdapter* adapter_ = nullptr;
    RandomDebugBot bot_;
    DebugLogger logger_;
    DebugReplayRecorder replayRecorder_;
    DebugReplayPlayer replayPlayer_;
    DebugAction lastAction_;
    bool replayMode_ = false;

    DebugGameState pendingBeforeState_;
    DebugAction pendingAction_;
    bool hasPendingAction_ = false;

    std::string lastStableStateKey_;
    std::string lastProgressKey_;
    float sameStateSeconds_ = 0.0f;
    float noProgressSeconds_ = 0.0f;
    float lowFpsSeconds_ = 0.0f;

    float sameStateLimitSeconds_ = 10.0f;
    float noProgressLimitSeconds_ = 15.0f;
    float lowFpsLimitSeconds_ = 3.0f;
    float lowFpsThreshold_ = 30.0f;
    unsigned long long duplicateIssueCooldownFrames_ = 60;
    std::unordered_map<std::string, unsigned long long> lastIssueFrameByMessage_;
};
