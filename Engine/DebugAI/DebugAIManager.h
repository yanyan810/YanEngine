#pragma once

#include "DebugAIConfig.h"
#include "DebugLogger.h"
#include "DebugReplayPlayer.h"
#include "DebugReplayRecorder.h"
#include "IDebugBot.h"
#include "IGameDebugAdapter.h"
#include "RandomDebugBot.h"

#include <memory>
#include <string>
#include <unordered_map>

class DebugAIManager {
public:
    void Initialize(const std::string& logDirectory = "generated/debug_ai");
    void Initialize(const DebugAIConfig& config);
    void Shutdown();

    void SetConfig(const DebugAIConfig& config);
    const DebugAIConfig& Config() const { return config_; }

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }

    void SetAdapter(IGameDebugAdapter* adapter) { adapter_ = adapter; }
    void SetBot(IDebugBot* bot);
    void ResetBotToRandom();
    const char* CurrentBotName() const { return bot_ ? bot_->Name() : "None"; }
    void InjectAction();
    void ProcessAfterUpdate(float dt);
    void RecordExternalAction(
        const DebugGameState& stateBefore,
        const DebugAction& action,
        const DebugGameState& stateAfter);
    void CheckReplayDrift(const DebugGameState& actualState);

    bool StartLatestReplay();
    bool StartReplay(const std::string& replayPath);
    bool RestoreReplayInitialState();
    void StopReplay();
    bool IsReplayPlaying() const { return replayMode_ && replayPlayer_.IsPlaying(); }
    bool IsFirstReplayFrame() const { return isFirstReplayFrame_; }

    const DebugLogger& Logger() const { return logger_; }
    const DebugReplayRecorder& ReplayRecorder() const { return replayRecorder_; }
    const DebugReplayRecorder& PlayerReplayRecorder() const { return playerReplayRecorder_; }
    const DebugReplayPlayer& ReplayPlayer() const { return replayPlayer_; }
    const DebugAction& LastAction() const { return lastAction_; }
    void LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message);

private:
    bool RestoreReplayInitialState_();
    void DetectIssues_(const DebugGameState& state, float dt);
    void AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message);
    bool IsFinite_(const Vector3& value) const;
    bool IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const;
    bool IsSameState_(const DebugGameState& state) const;

private:
    bool enabled_ = false;
    DebugAIConfig config_;
    IGameDebugAdapter* adapter_ = nullptr;
    RandomDebugBot randomBot_;
    IDebugBot* bot_ = &randomBot_;
    DebugLogger logger_;
    DebugReplayRecorder replayRecorder_;
    DebugReplayRecorder playerReplayRecorder_;
    DebugReplayPlayer replayPlayer_;
    DebugAction lastAction_;
    bool replayMode_ = false;
    bool isFirstReplayFrame_ = false;

    DebugGameState pendingBeforeState_;
    DebugAction pendingAction_;
    bool hasPendingAction_ = false;

    std::string lastStableStateKey_;
    std::string lastProgressKey_;
    float sameStateSeconds_ = 0.0f;
    float noProgressSeconds_ = 0.0f;
    float lowFpsSeconds_ = 0.0f;

    std::unordered_map<std::string, unsigned long long> lastIssueFrameByMessage_;
};
