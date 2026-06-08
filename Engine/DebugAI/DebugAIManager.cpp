#include "DebugAIManager.h"

#include <cmath>

void DebugAIManager::Initialize(const std::string& logDirectory) {
    logger_.Open(logDirectory);
    replayRecorder_.Open(logger_.DirectoryPath());
}

void DebugAIManager::Shutdown() {
    logger_.WriteReport();
    replayRecorder_.Close();
    logger_.Close();
}

void DebugAIManager::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        StopReplay();
    }
}

bool DebugAIManager::StartLatestReplay() {
    if (adapter_ == nullptr) {
        return false;
    }
    if (!replayPlayer_.LoadLatestFromDirectory(logger_.DirectoryPath())) {
        return false;
    }

    if (replayPlayer_.HasInitialState()) {
        adapter_->RestoreDebugState(replayPlayer_.InitialState());
    }

    const DebugGameState state = adapter_->CaptureDebugState();
    replayPlayer_.Start(state.frameNumber);
    replayMode_ = true;
    enabled_ = true;
    return true;
}

void DebugAIManager::StopReplay() {
    replayPlayer_.Stop();
    replayMode_ = false;
}

void DebugAIManager::Tick(float dt) {
    if (!enabled_ || adapter_ == nullptr) {
        return;
    }

    DebugAction* executedAction = nullptr;

    if (replayMode_) {
        DebugGameState currentState = adapter_->CaptureDebugState();
        DebugReplayAction replayAction;
        while (replayPlayer_.PopDueAction(currentState.frameNumber, replayAction)) {
            const DebugGameState beforeState = currentState;
            adapter_->ExecuteDebugAction(replayAction.action);
            lastAction_ = replayAction.action;
            executedAction = &lastAction_;
            currentState = adapter_->CaptureDebugState();
            replayRecorder_.RecordAction(beforeState, replayAction.action, currentState);
        }

        if (!replayPlayer_.IsPlaying() && replayPlayer_.IsFinished()) {
            replayMode_ = false;
            enabled_ = false;
        }

        logger_.LogFrame(currentState, executedAction);
        DetectIssues_(currentState, dt);
        return;
    }

    DebugGameState beforeState = adapter_->CaptureDebugState();

    DebugAction chosenAction;
    if (bot_.ChooseAction(beforeState, chosenAction)) {
        adapter_->ExecuteDebugAction(chosenAction);
        lastAction_ = chosenAction;
        executedAction = &lastAction_;
    }

    DebugGameState afterState = adapter_->CaptureDebugState();
    if (executedAction != nullptr) {
        replayRecorder_.RecordAction(beforeState, *executedAction, afterState);
    }
    logger_.LogFrame(afterState, executedAction);
    DetectIssues_(afterState, dt);
}

void DebugAIManager::RecordExternalAction(
    const DebugGameState& stateBefore,
    const DebugAction& action,
    const DebugGameState& stateAfter) {

    if (action.name.empty()) {
        return;
    }

    lastAction_ = action;
    replayRecorder_.RecordAction(stateBefore, action, stateAfter);
}

void DebugAIManager::DetectIssues_(const DebugGameState& state, float dt) {
    if (state.playerHp < 0) {
        AddIssue_(DebugIssueSeverity::Error, state, "Player HP became negative.");
    }
    if (state.enemyHp < 0) {
        AddIssue_(DebugIssueSeverity::Error, state, "Enemy HP became negative.");
    }
    if (state.enemyCount < 0) {
        AddIssue_(DebugIssueSeverity::Error, state, "Enemy count became negative.");
    }
    if (!IsFinite_(state.playerPosition)) {
        AddIssue_(DebugIssueSeverity::Error, state, "Player position became NaN or infinity.");
    }
    if (state.mapBounds.enabled && IsOutsideBounds_(state.playerPosition, state.mapBounds)) {
        AddIssue_(DebugIssueSeverity::Warning, state, "Player moved outside the map bounds.");
    }

    if (IsSameState_(state)) {
        sameStateSeconds_ += dt;
        if (sameStateSeconds_ >= sameStateLimitSeconds_) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Same state continued for too long.");
            sameStateSeconds_ = 0.0f;
        }
    } else {
        sameStateSeconds_ = 0.0f;
    }
    lastStableStateKey_ = state.stableStateKey;

    if (!state.progressKey.empty() && state.progressKey == lastProgressKey_) {
        noProgressSeconds_ += dt;
        if (noProgressSeconds_ >= noProgressLimitSeconds_) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Scene or game progress did not advance for too long.");
            noProgressSeconds_ = 0.0f;
        }
    } else {
        noProgressSeconds_ = 0.0f;
    }
    lastProgressKey_ = state.progressKey;

    if (state.fps > 0.0f && state.fps < lowFpsThreshold_) {
        lowFpsSeconds_ += dt;
        if (lowFpsSeconds_ >= lowFpsLimitSeconds_) {
            AddIssue_(DebugIssueSeverity::Warning, state, "FPS stayed below the debug threshold.");
            lowFpsSeconds_ = 0.0f;
        }
    } else {
        lowFpsSeconds_ = 0.0f;
    }
}

void DebugAIManager::AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message) {
    const auto it = lastIssueFrameByMessage_.find(message);
    if (it != lastIssueFrameByMessage_.end() &&
        state.frameNumber < it->second + duplicateIssueCooldownFrames_) {
        return;
    }
    lastIssueFrameByMessage_[message] = state.frameNumber;

    DebugIssue issue;
    issue.severity = severity;
    issue.message = message;
    issue.frameNumber = state.frameNumber;
    issue.sceneName = state.sceneName;
    issue.lastAction = lastAction_;
    issue.replayPath = replayRecorder_.SaveRecentReplayForIssue(issue);
    logger_.LogIssue(issue);
}

bool DebugAIManager::IsFinite_(const Vector3& value) const {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool DebugAIManager::IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const {
    return value.x < bounds.min.x || value.y < bounds.min.y || value.z < bounds.min.z ||
        value.x > bounds.max.x || value.y > bounds.max.y || value.z > bounds.max.z;
}

bool DebugAIManager::IsSameState_(const DebugGameState& state) const {
    if (state.stableStateKey.empty()) {
        return false;
    }
    return state.stableStateKey == lastStableStateKey_;
}
