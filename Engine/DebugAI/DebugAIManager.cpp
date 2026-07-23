#include "DebugAIManager.h"

#include "Transport/NamedPipeDebugAITransport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

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

void NormalizeChosenAction(DebugAction& action) {
    if (action.name == "DodgeAway") {
        action.holdFrames = std::max(action.holdFrames, 14u);
    } else if (action.name == "Retreat") {
        action.holdFrames = std::max(action.holdFrames, 12u);
    } else if (
        action.name == "AttackWeak" ||
        action.name == "AttackTilt" ||
        action.name == "AttackSmash" ||
        action.name == "AttackNeutralSpecial" ||
        action.name == "AttackSideSpecial" ||
        action.name == "AttackUpSpecial" ||
        action.name == "AttackDownSpecial" ||
        action.name == "AttackSpecial") {
        action.holdFrames = 1;
    }
}

std::string BuildRestoreMessage(const DebugGameState& expected, const DebugGameState& actual, const std::vector<DebugSpawnOverride>& overrides) {
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

    int pendingCount = 0;
    std::ostringstream pendingDetails;
    for (const DebugEntityState& entity : actual.entities) {
        if (entity.category == "PendingSpawn" || entity.pending) {
            ++pendingCount;
            pendingDetails << " [" << entity.type << " delay=" << entity.delay << "]";
        }
    }
    message << " | pendingSpawns=" << pendingCount << pendingDetails.str();
    message << " | overrides=" << overrides.size();

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
        DistanceSq(expected.velocity, actual.velocity) > kEntityVelocityDriftSq ||
        expected.aiState1 != actual.aiState1 ||
        expected.aiState2 != actual.aiState2 ||
        std::fabs(expected.aiFloat1 - actual.aiFloat1) > kTimerDrift ||
        std::fabs(expected.aiFloat2 - actual.aiFloat2) > kTimerDrift ||
        expected.aiFloat3 != actual.aiFloat3 ||
        DistanceSq(expected.bossWanderVel, actual.bossWanderVel) > kEntityVelocityDriftSq ||
        std::fabs(expected.bossWanderChange - actual.bossWanderChange) > kTimerDrift;
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
        
        if (expectedEntity.type == "Boss") {
            message
                << " bossState " << expectedEntity.aiState1 << "->" << actualEntity->aiState1
                << " bossPhase " << expectedEntity.aiState2 << "->" << actualEntity->aiState2
                << " bossTime " << expectedEntity.aiFloat1 << "->" << actualEntity->aiFloat1
                << " bossStateTime " << expectedEntity.aiFloat2 << "->" << actualEntity->aiFloat2
                << " bossFlags " << expectedEntity.aiFloat3 << "->" << actualEntity->aiFloat3
                << " bossWanderVel ("
                << expectedEntity.bossWanderVel.x << "," << expectedEntity.bossWanderVel.y << "," << expectedEntity.bossWanderVel.z
                << ")->("
                << actualEntity->bossWanderVel.x << "," << actualEntity->bossWanderVel.y << "," << actualEntity->bossWanderVel.z
                << ")"
                << " bossWanderChange " << expectedEntity.bossWanderChange << "->" << actualEntity->bossWanderChange;
        }

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

std::string ReadGenericString(
    const DebugGenericAction& action,
    const char* key,
    const std::string& fallback = {}) {
    const auto found = action.parameters.find(key);
    if (found == action.parameters.end()) return fallback;
    if (const auto* value = std::get_if<std::string>(&found->second)) return *value;
    return fallback;
}

unsigned int ReadGenericDuration(const DebugGenericAction& action) {
    const auto read = [&](const char* key) -> unsigned int {
        const auto found = action.parameters.find(key);
        if (found == action.parameters.end()) return 0;
        if (const auto* value = std::get_if<std::int64_t>(&found->second)) {
            return static_cast<unsigned int>(std::clamp<std::int64_t>(*value, 1, 600));
        }
        return 0;
    };
    const unsigned int semantic = read(DebugActionParameter::DurationFrames);
    return semantic != 0 ? semantic : std::max(1u, read("holdFrames"));
}

std::string MatchingInputReplayPath(
    const std::string& actorReplayPath,
    const std::string& playerLogDirectory) {
    const std::string name = std::filesystem::path(actorReplayPath).filename().string();
    constexpr const char* prefix = "actors_";
    constexpr const char* suffix = ".dair2.jsonl";
    if (!name.starts_with(prefix) || !name.ends_with(suffix)) return {};
    const std::size_t tokenBegin = std::char_traits<char>::length(prefix);
    const std::size_t tokenLength = name.size() - tokenBegin - std::char_traits<char>::length(suffix);
    const std::string token = name.substr(tokenBegin, tokenLength);
    const auto inputPath = std::filesystem::path(playerLogDirectory) /
        "input" / ("input_" + token + ".dair");
    std::error_code error;
    return std::filesystem::is_regular_file(inputPath, error) ? inputPath.string() : std::string{};
}

}

void DebugAIManager::Initialize(const std::string& logDirectory) {
    DebugAIConfig config;
    config.logDirectory = logDirectory;
    Initialize(config);
}

void DebugAIManager::Initialize(const DebugAIConfig& config) {
    SetConfig(config);
    logger_.Open(config_.logDirectory);
    replayRecorder_.Open(config_.aiLogDirectory);
    playerReplayRecorder_.Open(config_.playerLogDirectory);
    inputReplay_.Open(config_.playerLogDirectory + "/input");
    genericActionReplay_.Open(config_.playerLogDirectory + "/actors");
    eventRecorder_.Open(config_.logDirectory + "/events");
    if (!controlTransport_) {
        controlTransport_ = std::make_unique<NamedPipeDebugAITransport>();
    }
    controlTransport_->Start(config_.controlEndpoint);
    logger_.SetSessionDirectory(replayRecorder_.SessionDirectoryPath());
}

void DebugAIManager::SetConfig(const DebugAIConfig& config) {
    config_ = config;
    if (config_.playerLogDirectory.empty()) {
        config_.playerLogDirectory = config_.logDirectory + "/player";
    }
    if (config_.aiLogDirectory.empty()) {
        config_.aiLogDirectory = config_.logDirectory + "/ai";
    }
}

void DebugAIManager::SetLoadingDetails(std::string status, std::vector<DebugAILoadingSourceFile> sourceFiles) {
    loadingStatus_ = std::move(status);
    loadingSourceFiles_ = std::move(sourceFiles);
}

void DebugAIManager::Shutdown() {
    if (controlTransport_) {
        controlTransport_->Stop();
    }
    logger_.WriteReport();
    inputReplay_.Close();
    genericActionReplay_.Close();
    eventRecorder_.Close();
    replayRecorder_.Close();
    playerReplayRecorder_.Close();
    logger_.Close();
}

void DebugAIManager::ProcessControlCommands() {
    if (!controlTransport_) {
        return;
    }
    if (genericAdapter_) {
        const DebugObservation observation = genericAdapter_->CaptureDebugObservation();
        if (eventRecorder_.IsRecording()) {
            eventRecorder_.Observe(observation);
        }
        if (genericActionReplay_.IsRecording()) {
            RecordActorStateChanges_(observation);
        }

        for (auto it = heldExternalActions_.begin(); it != heldExternalActions_.end();) {
            if (it->second.framesRemaining == 0) {
                it = heldExternalActions_.erase(it);
                continue;
            }
            genericAdapter_->ExecuteGenericDebugAction(it->second.action);
            --it->second.framesRemaining;
            ++it;
        }

        DebugGenericReplayEvent replayEvent;
        while (genericActionReplay_.PopDue(observation.frameNumber, replayEvent)) {
            const std::string actorId = ReadGenericString(
                replayEvent.action, DebugActionParameter::ActorId, "player");
            // Prefer the exact v1 player input stream if both tracks exist.
            if (!(inputReplay_.IsPlaying() && actorId == "player")) {
                ExecuteGenericAction_(replayEvent.action, "Replay", observation.frameNumber, false);
            }
        }

    }
    controlTransport_->Poll();
    std::string jsonText;
    while (controlTransport_->TryReceive(jsonText)) {
        DebugProtocolMessage request;
        DebugProtocolMessage response;
        std::string parseError;
        if (DebugProtocolJson::TryParse(jsonText, request, &parseError)) {
            response = ExecuteControlCommand_(request);
        } else {
            response.gameId = config_.gameId;
            response.gameVersion = config_.gameVersion;
            response.messageType = DebugProtocolMessageType::ControlResult;
            response.sequence = ++controlSequence_;
            response.properties["ok"] = false;
            response.properties["message"] = "Invalid protocol message: " + parseError;
        }
        controlTransport_->Send(DebugProtocolJson::Serialize(response));
    }
}

bool DebugAIManager::ExecuteGenericAction_(
    DebugGenericAction action,
    const std::string& source,
    std::uint64_t frame,
    bool record) {
    if (!genericAdapter_ || action.actionId.empty()) return false;

    if (action.parameters.find(DebugActionParameter::ActorId) == action.parameters.end()) {
        action.parameters[DebugActionParameter::ActorId] = std::string("player");
    }
    if (action.parameters.find(DebugActionParameter::Source) == action.parameters.end()) {
        action.parameters[DebugActionParameter::Source] = source;
    }
    if (!genericAdapter_->ExecuteGenericDebugAction(action)) return false;

    const std::string actorId = ReadGenericString(action, DebugActionParameter::ActorId, "player");
    const unsigned int duration = ReadGenericDuration(action);
    if (duration > 1) {
        heldExternalActions_[actorId] = HeldExternalAction{ action, duration - 1 };
    } else {
        heldExternalActions_.erase(actorId);
    }
    if (record && genericActionReplay_.IsRecording() && !genericActionReplay_.IsPlaying()) {
        genericActionReplay_.Record(frame, action, source);
    }
    if (eventRecorder_.IsRecording()) {
        eventRecorder_.RecordAction(frame, action, source);
    }
    return true;
}

void DebugAIManager::RecordActorStateChanges_(const DebugObservation& observation) {
    for (const DebugEntity& entity : observation.entities) {
        if (entity.id.empty() || entity.category != "Enemy" || entity.type != "Boss") continue;
        const auto stateIt = entity.properties.find("ai.state");
        if (stateIt == entity.properties.end()) continue;
        const auto* state = std::get_if<std::string>(&stateIt->second);
        if (!state || state->empty()) continue;

        auto& previous = lastRecordedActorStates_[entity.id];
        if (previous == *state) continue;
        previous = *state;

        DebugGenericAction event;
        event.actionId = "SetActorState";
        event.parameters[DebugActionParameter::ActorId] = entity.id;
        event.parameters[DebugActionParameter::State] = *state;
        event.parameters[DebugActionParameter::Source] = std::string("Native");
        event.parameters[DebugActionParameter::DurationFrames] = static_cast<std::int64_t>(1);
        genericActionReplay_.Record(observation.frameNumber, event, "Native");
    }
}

void DebugAIManager::SetControlTransport(std::unique_ptr<IDebugAITransport> transport) {
    if (controlTransport_) {
        controlTransport_->Stop();
    }
    controlTransport_ = std::move(transport);
}

DebugProtocolMessage DebugAIManager::ExecuteControlCommand_(const DebugProtocolMessage& request) {
    std::string command;
    if (const auto it = request.properties.find("command"); it != request.properties.end()) {
        if (const std::string* value = std::get_if<std::string>(&it->second)) {
            command = *value;
        }
    }
    bool ok = true;
    std::string message;
    if (request.messageType == DebugProtocolMessageType::StatusRequest) {
        message = "status";
    } else if (request.messageType == DebugProtocolMessageType::ExecuteAction) {
        std::uint64_t frame = 0;
        if (genericAdapter_) frame = genericAdapter_->CaptureDebugObservation().frameNumber;
        const std::string source = request.action
            ? ReadGenericString(*request.action, DebugActionParameter::Source, "External")
            : "External";
        ok = request.action.has_value() && ExecuteGenericAction_(*request.action, source, frame, true);
        if (ok) {
            const unsigned int holdFrames = ReadGenericDuration(*request.action);
            message = "action executed: " + request.action->actionId +
                " (durationFrames=" + std::to_string(holdFrames) + ")";
        } else {
            message = "action rejected for the current scene or phase";
        }
    } else if (request.messageType != DebugProtocolMessageType::ControlCommand) {
        ok = false;
        message = "unsupported messageType";
    } else if (command == "start_recording") {
        std::uint64_t startFrame = 0;
        DebugObservation startObservation;
        const bool hasStartObservation = genericAdapter_ != nullptr;
        if (hasStartObservation) {
            startObservation = genericAdapter_->CaptureDebugObservation();
            startFrame = startObservation.frameNumber;
        }
        // Always restart both tracks so their time origins match, even when
        // the game scene enabled automatic input recording earlier.
        const bool inputOk = inputReplay_.StartRecording();
        const bool actorOk = genericActionReplay_.StartRecording(startFrame);
        const bool eventOk = eventRecorder_.StartRecording(startFrame);
        if (eventOk && hasStartObservation) eventRecorder_.Observe(startObservation);
        lastRecordedActorStates_.clear();
        ok = inputOk && actorOk && eventOk;
        message = ok ? "player input + actor actions + event timeline recording started" :
            (!inputOk ? inputReplay_.LastError() :
                (!actorOk ? genericActionReplay_.LastError() : eventRecorder_.LastError()));
    } else if (command == "stop_recording") {
        std::uint64_t endFrame = 0;
        if (genericAdapter_) {
            const DebugObservation endObservation = genericAdapter_->CaptureDebugObservation();
            endFrame = endObservation.frameNumber;
            eventRecorder_.Observe(endObservation);
        }
        const std::string inputPath = inputReplay_.StopRecording();
        const std::string actorPath = genericActionReplay_.StopRecording();
        const std::string eventPath = eventRecorder_.StopRecording(endFrame);
        ok = !inputPath.empty() || !actorPath.empty() || !eventPath.empty();
        message = "input=" + inputPath + " actor=" + actorPath + " events=" + eventPath;
    } else if (command == "play_latest") {
        std::uint64_t frame = 0;
        bool skippedIntro = false;
        if (genericAdapter_) {
            DebugObservation playbackObservation = genericAdapter_->CaptureDebugObservation();
            const auto phaseIt = playbackObservation.properties.find("game.phase");
            const auto* phase = phaseIt == playbackObservation.properties.end()
                ? nullptr : std::get_if<std::string>(&phaseIt->second);
            if (phase && *phase == "IntroVideo") {
                const auto skipIt = std::find_if(
                    playbackObservation.availableActions.begin(),
                    playbackObservation.availableActions.end(),
                    [](const DebugGenericAction& action) { return action.actionId == "SkipIntro"; });
                if (skipIt != playbackObservation.availableActions.end()) {
                    genericAdapter_->ExecuteGenericDebugAction(*skipIt);
                    skippedIntro = true;
                    playbackObservation = genericAdapter_->CaptureDebugObservation();
                }
            }
            frame = playbackObservation.frameNumber;
        }
        const bool actorOk = genericActionReplay_.StartLatestReplay(frame);
        bool inputOk = false;
        if (actorOk) {
            const std::string pairedInput = MatchingInputReplayPath(
                genericActionReplay_.ReplayPath(), config_.playerLogDirectory);
            if (!pairedInput.empty()) {
                inputOk = inputReplay_.StartReplay(pairedInput);
            } else {
                inputReplay_.StopRecording();
                inputReplay_.StopReplay();
            }
        } else {
            inputOk = inputReplay_.StartLatestReplay();
        }
        heldExternalActions_.clear();
        ok = inputOk || actorOk;
        message = ok ? std::string(skippedIntro ? "intro skipped; " : "") +
            "input=" + inputReplay_.ReplayPath() +
            " actor=" + genericActionReplay_.ReplayPath() :
            "input: " + inputReplay_.LastError() + " actor: " + genericActionReplay_.LastError();
    } else if (command == "stop_replay") {
        inputReplay_.StopReplay();
        genericActionReplay_.StopReplay();
        heldExternalActions_.clear();
        message = "replay stopped";
    } else if (command == "pause_simulation" || command == "resume_simulation") {
        const bool paused = command == "pause_simulation";
        ok = genericAdapter_ != nullptr && genericAdapter_->SetDebugSimulationPaused(paused);
        message = ok ? (paused ? "simulation paused for AI" : "simulation resumed")
            : "simulation pause is not supported by the current adapter";
    } else {
        ok = false;
        message = "unknown command";
    }

    DebugProtocolMessage response;
    response.gameId = config_.gameId;
    response.gameVersion = config_.gameVersion;
    response.sessionId = request.sessionId;
    response.messageType = request.messageType == DebugProtocolMessageType::StatusRequest
        ? DebugProtocolMessageType::StatusResponse
        : DebugProtocolMessageType::ControlResult;
    response.sequence = request.sequence != 0 ? request.sequence : ++controlSequence_;
    response.properties["ok"] = ok;
    response.properties["recording"] = inputReplay_.IsRecording() ||
        genericActionReplay_.IsRecording() || eventRecorder_.IsRecording();
    response.properties["replaying"] = inputReplay_.IsPlaying() || genericActionReplay_.IsPlaying();
    response.properties["frame"] = static_cast<std::int64_t>(inputReplay_.CurrentFrame());
    response.properties["path"] = !genericActionReplay_.ReplayPath().empty()
        ? genericActionReplay_.ReplayPath() : inputReplay_.ReplayPath();
    response.properties["playerReplayPath"] = inputReplay_.ReplayPath();
    response.properties["actorReplayPath"] = genericActionReplay_.ReplayPath();
    response.properties["eventLogPath"] = eventRecorder_.TimelinePath();
    response.properties["eventSummaryPath"] = eventRecorder_.SummaryPath();
    response.properties["eventCount"] = static_cast<std::int64_t>(eventRecorder_.EventCount());
    response.properties["lastEvent"] = eventRecorder_.LastEventSummary();
    response.properties["message"] = message;
    response.properties["lastError"] = inputReplay_.LastError();
    if (genericAdapter_) {
        response.observation = genericAdapter_->CaptureDebugObservation();
    }
    return response;
}

void DebugAIManager::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        StopReplay();
        heldActionFramesRemaining_ = 0;
        hasPendingAction_ = false;
        waitingForAction_ = false;
        idleAfterUpdateFrames_ = 0;
        heldExternalActions_.clear();
    }
}

void DebugAIManager::SetBot(IDebugBot* bot) {
    bot_ = (bot != nullptr) ? bot : &randomBot_;
}

void DebugAIManager::ResetBotToRandom() {
    bot_ = &randomBot_;
}

bool DebugAIManager::StartLatestReplay() {
    if (adapter_ == nullptr) {
        return false;
    }
    if (!replayPlayer_.LoadLatestFromDirectory(config_.playerLogDirectory) &&
        !replayPlayer_.LoadLatestFromDirectory(config_.aiLogDirectory) &&
        !replayPlayer_.LoadLatestFromDirectory(logger_.DirectoryPath())) {
        return false;
    }
    logger_.SetSessionDirectory(std::filesystem::path(replayPlayer_.ReplayPath()).parent_path().string());

    RestoreReplayInitialState_();

    const DebugGameState state = adapter_->CaptureDebugState();
    replayPlayer_.Start(state.frameNumber);
    replayMode_ = true;
    enabled_ = true;
    isFirstReplayFrame_ = true;
    return true;
}

bool DebugAIManager::StartReplay(const std::string& replayPath) {
    if (adapter_ == nullptr || replayPath.empty()) {
        return false;
    }
    if (!replayPlayer_.Load(replayPath)) {
        return false;
    }
    logger_.SetSessionDirectory(std::filesystem::path(replayPlayer_.ReplayPath()).parent_path().string());

    RestoreReplayInitialState_();

    const DebugGameState state = adapter_->CaptureDebugState();
    replayPlayer_.Start(state.frameNumber);
    replayMode_ = true;
    enabled_ = true;
    isFirstReplayFrame_ = true;
    return true;
}

bool DebugAIManager::RestoreReplayInitialState() {
    return RestoreReplayInitialState_();
}

bool DebugAIManager::RestoreReplayInitialState_() {
    if (adapter_ == nullptr || !replayPlayer_.HasInitialState()) {
        return false;
    }

    DebugGameState restoreState = replayPlayer_.InitialState();
    if (restoreState.frameNumber > 0) {
        --restoreState.frameNumber;
    }

    adapter_->SetReplaySpawnOverrides(replayPlayer_.SpawnOverrides());
    const bool restored = adapter_->RestoreDebugState(restoreState);
    const DebugGameState restoredState = adapter_->CaptureDebugState();
    logger_.LogEvent(
        restoredState,
        "ReplayRestore",
        BuildRestoreMessage(replayPlayer_.InitialState(), restoredState, replayPlayer_.SpawnOverrides()));
    return restored;
}

void DebugAIManager::StopReplay() {
    replayPlayer_.Stop();
    inputReplay_.StopReplay();
    genericActionReplay_.StopReplay();
    heldExternalActions_.clear();
    replayMode_ = false;
    isFirstReplayFrame_ = false;
}

void DebugAIManager::InjectAction() {
    if (!enabled_ || adapter_ == nullptr) {
        waitingForAction_ = false;
        return;
    }
    hasPendingAction_ = false;

    if (!replayMode_ && heldActionFramesRemaining_ > 0) {
        waitingForAction_ = false;
        adapter_->ExecuteDebugAction(heldAction_);
        lastAction_ = heldAction_;
        --heldActionFramesRemaining_;
        return;
    }

    if (replayMode_) {
        waitingForAction_ = false;
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
    if (bot_ != nullptr && bot_->ChooseAction(beforeState, chosenAction)) {
        waitingForAction_ = false;
        NormalizeChosenAction(chosenAction);
        pendingBeforeState_ = beforeState;
        pendingAction_ = chosenAction;
        hasPendingAction_ = true;
        adapter_->ExecuteDebugAction(chosenAction);
        lastAction_ = chosenAction;
        heldAction_ = chosenAction;
        heldActionFramesRemaining_ = chosenAction.holdFrames > 1 ? chosenAction.holdFrames - 1 : 0;
        return;
    }

    waitingForAction_ = ShouldWaitForAction_();
}

bool DebugAIManager::ShouldWaitForAction_() const {
    for (const DebugAILoadingSourceFile& source : loadingSourceFiles_) {
        if (!source.loaded) {
            return true;
        }
    }
    return false;
}

void DebugAIManager::ProcessAfterUpdate(float dt) {
    if (!enabled_ || adapter_ == nullptr) {
        return;
    }

    const bool shouldDetectIssues =
        config_.detectNegativeHp ||
        config_.detectInvalidCounts ||
        config_.detectInvalidPosition ||
        config_.detectMapBounds ||
        config_.detectSameState ||
        config_.detectNoProgress ||
        config_.detectLowFps;
    const bool needsImmediateActionSample =
        replayMode_ ||
        (hasPendingAction_ && (config_.recordBotActions || config_.logActionResults));
    const bool needsSample =
        needsImmediateActionSample ||
        replayMode_ ||
        config_.logFrames ||
        shouldDetectIssues;

    if (!needsSample) {
        hasPendingAction_ = false;
        return;
    }

    if (!needsImmediateActionSample) {
        const unsigned int interval = std::max(1u, config_.idleSampleIntervalFrames);
        ++idleAfterUpdateFrames_;
        if (idleAfterUpdateFrames_ < interval) {
            if (hasPendingAction_) {
                hasPendingAction_ = false;
            }
            return;
        }
        idleAfterUpdateFrames_ = 0;
    } else {
        idleAfterUpdateFrames_ = 0;
    }

    DebugGameState afterState = adapter_->CaptureDebugState();
    DebugAction* executedAction = nullptr;

    if (hasPendingAction_) {
        executedAction = &pendingAction_;
        if (!replayMode_ && config_.recordBotActions) {
            replayRecorder_.RecordAction(pendingBeforeState_, pendingAction_, afterState);
            logger_.SetSessionDirectory(replayRecorder_.SessionDirectoryPath());
        }
        
        if (replayMode_) {
            logger_.LogEvent(afterState, "ReplayActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        } else if (config_.logActionResults) {
            logger_.LogEvent(afterState, "BotActionResult", BuildStateDiffMessage(pendingBeforeState_, afterState, pendingAction_));
        }
        hasPendingAction_ = false;
    }

    if (replayMode_) {
        CheckReplayDrift(afterState);
    }

    if (config_.logFrames) {
        const unsigned int frameLogInterval = std::max(1u, config_.frameLogIntervalFrames);
        ++frameLogSampleFrames_;
        if (executedAction != nullptr || frameLogSampleFrames_ >= frameLogInterval) {
            logger_.LogFrame(afterState, executedAction);
            frameLogSampleFrames_ = 0;
        }
    }
    if (shouldDetectIssues) {
        DetectIssues_(afterState, dt);
    }

    isFirstReplayFrame_ = false;
}

void DebugAIManager::RecordExternalAction(
    const DebugGameState& stateBefore,
    const DebugAction& action,
    const DebugGameState& stateAfter) {

    if (action.name.empty()) {
        return;
    }

    lastAction_ = action;
    playerReplayRecorder_.RecordAction(stateBefore, action, stateAfter);
    logger_.SetSessionDirectory(playerReplayRecorder_.SessionDirectoryPath());
    logger_.LogEvent(stateAfter, "ManualActionResult", BuildStateDiffMessage(stateBefore, stateAfter, action));
}

void DebugAIManager::LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message) {
    logger_.LogEvent(state, eventName, message);
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
    if (config_.detectNegativeHp) {
        if (state.playerHp < 0) {
            AddIssue_(DebugIssueSeverity::Error, state, "Player HP became negative.");
        }
        if (state.enemyHp < 0) {
            AddIssue_(DebugIssueSeverity::Error, state, "Enemy HP became negative.");
        }
    }
    if (config_.detectInvalidCounts && state.enemyCount < 0) {
        AddIssue_(DebugIssueSeverity::Error, state, "Enemy count became negative.");
    }
    if (config_.detectInvalidPosition && !IsFinite_(state.playerPosition)) {
        AddIssue_(DebugIssueSeverity::Error, state, "Player position became NaN or infinity.");
    }
    if (config_.detectMapBounds && state.mapBounds.enabled && IsOutsideBounds_(state.playerPosition, state.mapBounds)) {
        AddIssue_(DebugIssueSeverity::Warning, state, "Player moved outside the map bounds.");
    }

    if (config_.detectSameState && IsSameState_(state)) {
        sameStateSeconds_ += dt;
        if (sameStateSeconds_ >= config_.sameStateLimitSeconds) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Same state continued for too long.");
            sameStateSeconds_ = 0.0f;
        }
    } else {
        sameStateSeconds_ = 0.0f;
    }
    lastStableStateKey_ = state.stableStateKey;

    if (config_.detectNoProgress && !state.progressKey.empty() && state.progressKey == lastProgressKey_) {
        noProgressSeconds_ += dt;
        if (noProgressSeconds_ >= config_.noProgressLimitSeconds) {
            AddIssue_(DebugIssueSeverity::Warning, state, "Scene or game progress did not advance for too long.");
            noProgressSeconds_ = 0.0f;
        }
    } else {
        noProgressSeconds_ = 0.0f;
    }
    lastProgressKey_ = state.progressKey;

    if (config_.detectLowFps && state.fps > 0.0f && state.fps < config_.lowFpsThreshold) {
        lowFpsSeconds_ += dt;
        if (lowFpsSeconds_ >= config_.lowFpsLimitSeconds) {
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
        state.frameNumber < it->second + config_.duplicateIssueCooldownFrames) {
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
