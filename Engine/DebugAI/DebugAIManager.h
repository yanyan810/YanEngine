#pragma once

#include "DebugAIConfig.h"
#include "Protocol/DebugProtocol.h"
#include "Protocol/IGenericGameDebugAdapter.h"
#include "Transport/IDebugAITransport.h"
#include "DebugLogger.h"
#include "DebugGenericActionReplay.h"
#include "DebugInputReplay.h"
#include "DebugObservationEventRecorder.h"
#include "DebugReplayPlayer.h"
#include "DebugReplayRecorder.h"
#include "IDebugBot.h"
#include "IGameDebugAdapter.h"
#include "RandomDebugBot.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DebugAIManager {
public:
    void Initialize(const std::string& logDirectory = "generated/debug_ai");
    void Initialize(const DebugAIConfig& config);
    void Shutdown();

    void SetConfig(const DebugAIConfig& config);
    const DebugAIConfig& Config() const { return config_; }

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }
    bool IsWaitingForAction() const { return waitingForAction_; }
    void SetLoadingDetails(std::string status, std::vector<DebugAILoadingSourceFile> sourceFiles);
    const std::string& LoadingStatus() const { return loadingStatus_; }
    const std::vector<DebugAILoadingSourceFile>& LoadingSourceFiles() const { return loadingSourceFiles_; }

    void SetAdapter(IGameDebugAdapter* adapter) { adapter_ = adapter; }
    void SetGenericAdapter(IGenericGameDebugAdapter* adapter) { genericAdapter_ = adapter; }
    void SetBot(IDebugBot* bot);
    void ResetBotToRandom();
    const char* CurrentBotName() const { return bot_ ? bot_->Name() : "None"; }
    void InjectAction();
    void ProcessAfterUpdate(float dt);
    void ProcessControlCommands();
    void SetControlTransport(std::unique_ptr<IDebugAITransport> transport);
    void RecordExternalAction(
        const DebugGameState& stateBefore,
        const DebugAction& action,
        const DebugGameState& stateAfter);
    void CheckReplayDrift(const DebugGameState& actualState);

    bool StartLatestReplay();
    bool StartReplay(const std::string& replayPath);
    bool RestoreReplayInitialState();
    void StopReplay();
    bool IsReplayPlaying() const {
        return (replayMode_ && replayPlayer_.IsPlaying()) ||
            inputReplay_.IsPlaying() || genericActionReplay_.IsPlaying();
    }
    bool IsFirstReplayFrame() const { return isFirstReplayFrame_; }

    const DebugLogger& Logger() const { return logger_; }
    const DebugReplayRecorder& ReplayRecorder() const { return replayRecorder_; }
    const DebugReplayRecorder& PlayerReplayRecorder() const { return playerReplayRecorder_; }
    const DebugReplayPlayer& ReplayPlayer() const { return replayPlayer_; }
    DebugInputReplay& InputReplay() { return inputReplay_; }
    const DebugInputReplay& InputReplay() const { return inputReplay_; }
    const DebugGenericActionReplay& GenericActionReplay() const { return genericActionReplay_; }
    const DebugAction& LastAction() const { return lastAction_; }
    bool ShouldLogEvents() const { return config_.logActionResults || replayMode_; }
    void LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message);

private:
    DebugProtocolMessage ExecuteControlCommand_(const DebugProtocolMessage& request);
    bool ShouldWaitForAction_() const;
    bool RestoreReplayInitialState_();
    void DetectIssues_(const DebugGameState& state, float dt);
    void AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message);
    bool IsFinite_(const Vector3& value) const;
    bool IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const;
    bool IsSameState_(const DebugGameState& state) const;
    bool ExecuteGenericAction_(
        DebugGenericAction action,
        const std::string& source,
        std::uint64_t frame,
        bool record);
    void RecordActorStateChanges_(const DebugObservation& observation);

private:
    bool enabled_ = false;
    DebugAIConfig config_;
    IGameDebugAdapter* adapter_ = nullptr;
    IGenericGameDebugAdapter* genericAdapter_ = nullptr;
    RandomDebugBot randomBot_;
    IDebugBot* bot_ = &randomBot_;
    DebugLogger logger_;
    DebugReplayRecorder replayRecorder_;
    DebugReplayRecorder playerReplayRecorder_;
    DebugReplayPlayer replayPlayer_;
    DebugInputReplay inputReplay_;
    DebugGenericActionReplay genericActionReplay_;
    DebugObservationEventRecorder eventRecorder_;
    std::unique_ptr<IDebugAITransport> controlTransport_;
    std::uint64_t controlSequence_ = 0;
    DebugAction lastAction_;
    bool replayMode_ = false;
    bool isFirstReplayFrame_ = false;

    DebugGameState pendingBeforeState_;
    DebugAction pendingAction_;
    bool hasPendingAction_ = false;
    bool waitingForAction_ = false;
    DebugAction heldAction_;
    unsigned int heldActionFramesRemaining_ = 0;
    struct HeldExternalAction {
        DebugGenericAction action;
        unsigned int framesRemaining = 0;
    };
    std::unordered_map<std::string, HeldExternalAction> heldExternalActions_;
    std::unordered_map<std::string, std::string> lastRecordedActorStates_;
    unsigned int idleAfterUpdateFrames_ = 0;
    unsigned int frameLogSampleFrames_ = 0;

    std::string lastStableStateKey_;
    std::string lastProgressKey_;
    float sameStateSeconds_ = 0.0f;
    float noProgressSeconds_ = 0.0f;
    float lowFpsSeconds_ = 0.0f;
    std::string loadingStatus_ = "Waiting for AI response.";
    std::vector<DebugAILoadingSourceFile> loadingSourceFiles_;

    std::unordered_map<std::string, unsigned long long> lastIssueFrameByMessage_;
};
