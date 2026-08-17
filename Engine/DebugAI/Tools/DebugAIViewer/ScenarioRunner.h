#pragma once

#include "DebugProtocol.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class ScenarioRunner {
public:
    enum class Status { Idle, Ready, Running, Passed, Failed, Stopped };

    bool Load(
        const std::filesystem::path& scenarioPath,
        const std::filesystem::path& actionProfilePath,
        const std::filesystem::path& resultDirectory,
        std::string& error);
    bool Start(std::string& error);
    void Observe(const DebugObservation& observation);
    void RecordExecutedAction(
        const DebugGenericAction& action,
        std::uint64_t frameNumber);
    void RecordAnomalyStatus(
        std::size_t anomalyCount,
        std::size_t errorCount,
        std::string lastAnomaly);
    void RequestStop();
    void Fail(std::string reason);
    bool IsRunning() const;
    Status CurrentStatus() const;
    std::string ActorMode() const;
    std::string TargetSceneId() const;
    std::string FailureReason() const;
    std::size_t AnomalyCount() const;
    std::size_t AnomalyErrorCount() const;
    bool AutoRecord() const;
    std::string FormatProgress() const;
    std::string Finalize(const std::string& replaySummary);
    std::filesystem::path ResultPath() const;

private:
    struct Goal {
        std::string id;
        std::string type;
        std::string description;
        std::string tag;
        std::string value;
        std::string property;
        std::vector<std::string> actionIds;
        std::set<std::string> candidateActionIds;
        std::set<std::string> requiredActionIds;
        std::set<std::string> usedActionIds;
        double amount = 0.0;
        bool complete = false;
    };

    void UpdateStatusLocked_();
    void SaveResultLocked_(const std::string& replaySummary);
    static std::string StatusName_(Status status);

    mutable std::mutex mutex_;
    Status status_ = Status::Idle;
    std::filesystem::path scenarioPath_;
    std::filesystem::path resultDirectory_;
    std::filesystem::path resultPath_;
    std::string id_;
    std::string name_;
    std::string description_;
    std::string actorMode_ = "Player";
    std::string targetSceneId_;
    bool autoRecord_ = true;
    bool failOnAnomaly_ = true;
    double timeoutSeconds_ = 120.0;
    std::vector<Goal> goals_;
    std::set<std::string> executedActions_;
    std::uint64_t startFrame_ = 0;
    std::uint64_t lastFrame_ = 0;
    double initialEnemyHp_ = -1.0;
    double currentEnemyHp_ = -1.0;
    std::string currentPhase_;
    std::string failureReason_;
    std::size_t anomalyCount_ = 0;
    std::size_t anomalyErrorCount_ = 0;
    std::string lastAnomaly_;
    std::chrono::steady_clock::time_point startedAt_{};
};
