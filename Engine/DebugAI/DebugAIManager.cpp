#include "DebugAIManager.h"

#include <cmath>
#include <sstream>

namespace {

float AbsDiff(float a, float b) {
    return std::fabs(a - b);
}

std::string BuildStateDiffMessage(const DebugGameState& before, const DebugGameState& after, const DebugAction& action) {
    std::ostringstream message;
    message
        << action.name
        << " playerHp " << before.playerHp << "->" << after.playerHp
        << " enemyHp " << before.enemyHp << "->" << after.enemyHp
        << " enemyCount " << before.enemyCount << "->" << after.enemyCount
        << " entities " << before.entities.size() << "->" << after.entities.size()
        << " playerPos("
        << before.playerPosition.x << "," << before.playerPosition.y << "," << before.playerPosition.z
        << ")->("
        << after.playerPosition.x << "," << after.playerPosition.y << "," << after.playerPosition.z
        << ")";
    return message.str();
}

std::string BuildRestoreMessage(const DebugGameState& expected, const DebugGameState& actual) {
    std::ostringstream message;
    message
        << "restore"
        << " expectedPlayerHp=" << expected.playerHp
        << " actualPlayerHp=" << actual.playerHp
        << " expectedEnemyCount=" << expected.enemyCount
        << " actualEnemyCount=" << actual.enemyCount
        << " expectedEntities=" << expected.entities.size()
        << " actualEntities=" << actual.entities.size()
        << " expectedSeed=" << expected.randomSeed
        << " actualSeed=" << actual.randomSeed
        << " posDiff=("
        << AbsDiff(expected.playerPosition.x, actual.playerPosition.x) << ","
        << AbsDiff(expected.playerPosition.y, actual.playerPosition.y) << ","
        << AbsDiff(expected.playerPosition.z, actual.playerPosition.z) << ")";
    return message.str();
}

float DistanceSq(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

std::string EntityLabel(const DebugEntityState& entity) {
    return entity.id + ":" + entity.category + ":" + entity.type;
}

const DebugEntityState* FindEntityById(const std::vector<DebugEntityState>& entities, const std::string& id) {
    for (const DebugEntityState& entity : entities) {
        if (entity.id == id) {
            return &entity;
        }
    }
    return nullptr;
}

bool HasEntityDrift(const DebugEntityState& expected, const DebugEntityState& actual) {
    constexpr float kEntityPositionDriftSq = 0.10f * 0.10f;
    constexpr float kEntityVelocityDriftSq = 0.10f * 0.10f;
    constexpr float kTimerDrift = 0.05f;
    return expected.category != actual.category ||
        expected.type != actual.type ||
        expected.hp != actual.hp ||
        expected.damage != actual.damage ||
        expected.alive != actual.alive ||
        expected.pending != actual.pending ||
        std::fabs(expected.delay - actual.delay) > kTimerDrift ||
        std::fabs(expected.life - actual.life) > kTimerDrift ||
        DistanceSq(expected.position, actual.position) > kEntityPositionDriftSq ||
        DistanceSq(expected.velocity, actual.velocity) > kEntityVelocityDriftSq;
}

void AppendEntityDriftSummary(std::ostringstream& message, const DebugGameState& expected, const DebugGameState& actual) {
    int diffCount = 0;
    constexpr int kMaxEntityDiffs = 6;

    for (const DebugEntityState& expectedEntity : expected.entities) {
        if (diffCount >= kMaxEntityDiffs) {
            break;
        }

        const DebugEntityState* actualEntity = FindEntityById(actual.entities, expectedEntity.id);
        if (!actualEntity) {
            message << " | missingActualEntity " << EntityLabel(expectedEntity);
            ++diffCount;
            continue;
        }

        if (!HasEntityDrift(expectedEntity, *actualEntity)) {
            continue;
        }

        message
            << " | entityDrift " << EntityLabel(expectedEntity)
            << " hp " << expectedEntity.hp << "->" << actualEntity->hp
            << " pos expected=("
            << expectedEntity.position.x << "," << expectedEntity.position.y << "," << expectedEntity.position.z
            << ") actual=("
            << actualEntity->position.x << "," << actualEntity->position.y << "," << actualEntity->position.z
            << ")"
            << " vel expected=("
            << expectedEntity.velocity.x << "," << expectedEntity.velocity.y << "," << expectedEntity.velocity.z
            << ") actual=("
            << actualEntity->velocity.x << "," << actualEntity->velocity.y << "," << actualEntity->velocity.z
            << ")"
            << " life " << expectedEntity.life << "->" << actualEntity->life
            << " delay " << expectedEntity.delay << "->" << actualEntity->delay;
        ++diffCount;
    }

    for (const DebugEntityState& actualEntity : actual.entities) {
        if (diffCount >= kMaxEntityDiffs) {
            break;
        }
        if (FindEntityById(expected.entities, actualEntity.id) == nullptr) {
            message << " | extraActualEntity " << EntityLabel(actualEntity);
            ++diffCount;
        }
    }
}

bool HasReplayDrift(const DebugGameState& expected, const DebugGameState& actual) {
    constexpr float kPositionDriftSq = 0.25f * 0.25f;
    if (expected.sceneName != actual.sceneName ||
        expected.gamePhase != actual.gamePhase ||
        expected.playerHp != actual.playerHp ||
        expected.enemyHp != actual.enemyHp ||
        expected.enemyCount != actual.enemyCount ||
        expected.entities.size() != actual.entities.size() ||
        DistanceSq(expected.playerPosition, actual.playerPosition) > kPositionDriftSq) {
        return true;
    }

    for (const DebugEntityState& expectedEntity : expected.entities) {
        const DebugEntityState* actualEntity = FindEntityById(actual.entities, expectedEntity.id);
        if (!actualEntity || HasEntityDrift(expectedEntity, *actualEntity)) {
            return true;
        }
    }
    return false;
}

std::string BuildDriftMessage(const DebugGameState& expected, const DebugGameState& actual) {
    std::ostringstream message;
    message
        << "checkpoint frame=" << expected.frameNumber
        << " actualFrame=" << actual.frameNumber
        << " phase " << expected.gamePhase << "->" << actual.gamePhase
        << " playerHp " << expected.playerHp << "->" << actual.playerHp
        << " enemyHp " << expected.enemyHp << "->" << actual.enemyHp
        << " enemyCount " << expected.enemyCount << "->" << actual.enemyCount
        << " entities " << expected.entities.size() << "->" << actual.entities.size()
        << " playerPos expected=("
        << expected.playerPosition.x << "," << expected.playerPosition.y << "," << expected.playerPosition.z
        << ") actual=("
        << actual.playerPosition.x << "," << actual.playerPosition.y << "," << actual.playerPosition.z
        << ")";
    AppendEntityDriftSummary(message, expected, actual);
    return message.str();
}

}

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
        DebugGameState restoreState = replayPlayer_.InitialState();
        if (restoreState.frameNumber > 0) {
            --restoreState.frameNumber;
        }
        adapter_->RestoreDebugState(restoreState);
        adapter_->SetReplaySpawnOverrides(replayPlayer_.SpawnOverrides());
        const DebugGameState restoredState = adapter_->CaptureDebugState();
        logger_.LogEvent(restoredState, "ReplayRestore", BuildRestoreMessage(replayPlayer_.InitialState(), restoredState));
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

void DebugAIManager::InjectAction() {
    if (!enabled_ || adapter_ == nullptr) {
        return;
    }
    hasPendingAction_ = false;

    if (replayMode_) {
        DebugGameState currentState = adapter_->CaptureDebugState();
        DebugReplayAction replayAction;
        if (replayPlayer_.PopDueAction(currentState.frameNumber, replayAction)) {
            pendingBeforeState_ = currentState;
            pendingAction_ = replayAction.action;
            hasPendingAction_ = true;
            adapter_->ExecuteDebugAction(replayAction.action);
            lastAction_ = replayAction.action;
        }

        if (!replayPlayer_.IsPlaying() && replayPlayer_.IsFinished()) {
            replayMode_ = false;
            enabled_ = false;
        }
        return;
    }

    DebugGameState beforeState = adapter_->CaptureDebugState();
    DebugAction chosenAction;
    if (bot_.ChooseAction(beforeState, chosenAction)) {
        pendingBeforeState_ = beforeState;
        pendingAction_ = chosenAction;
        hasPendingAction_ = true;
        adapter_->ExecuteDebugAction(chosenAction);
        lastAction_ = chosenAction;
    }
}

void DebugAIManager::ProcessAfterUpdate(float dt) {
    if (!enabled_ || adapter_ == nullptr) {
        return;
    }

    DebugGameState afterState = adapter_->CaptureDebugState();
    DebugAction* executedAction = nullptr;

    if (hasPendingAction_) {
        executedAction = &pendingAction_;
        replayRecorder_.RecordAction(pendingBeforeState_, pendingAction_, afterState);
        
        if (replayMode_) {
            logger_.LogEvent(afterState, "ReplayActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        } else {
            logger_.LogEvent(afterState, "BotActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        }
        hasPendingAction_ = false;
    }

    if (replayMode_) {
        CheckReplayDrift(afterState);
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
    logger_.LogEvent(stateAfter, "ManualActionResult", BuildStateDiffMessage(stateBefore, stateAfter, action));
}

void DebugAIManager::CheckReplayDrift(const DebugGameState& actualState) {
    if (!replayMode_) {
        return;
    }

    DebugReplayCheckpoint checkpoint;
    while (replayPlayer_.PopDueCheckpoint(actualState.frameNumber, checkpoint)) {
        if (HasReplayDrift(checkpoint.state, actualState)) {
            logger_.LogEvent(actualState, "ReplayDrift", BuildDriftMessage(checkpoint.state, actualState));
        }
    }
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
