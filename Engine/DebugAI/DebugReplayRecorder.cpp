#include "DebugReplayRecorder.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

bool DebugReplayRecorder::Open(const std::string& directoryPath) {
    directoryPath_ = std::filesystem::absolute(directoryPath).string();
    std::filesystem::create_directories(directoryPath_);

    unsigned int runIndex = 1;
    do {
        std::ostringstream path;
        path << directoryPath_ << "/debug_ai_actions_run_"
            << std::setw(4) << std::setfill('0') << runIndex
            << ".jsonl";
        actionLogPath_ = path.str();
        ++runIndex;
    } while (std::filesystem::exists(actionLogPath_));

    actionLog_.open(actionLogPath_, std::ios::out | std::ios::trunc);
    initialStatePath_ = actionLogPath_;
    const std::string suffix = ".jsonl";
    if (initialStatePath_.size() >= suffix.size() &&
        initialStatePath_.compare(initialStatePath_.size() - suffix.size(), suffix.size(), suffix) == 0) {
        initialStatePath_.erase(initialStatePath_.size() - suffix.size());
    }
    initialStatePath_ += "_initial_state.json";

    recentRecords_.clear();
    lastIssueReplayPath_.clear();
    hasInitialState_ = false;
    issueReplayIndex_ = 0;
    return actionLog_.is_open();
}

void DebugReplayRecorder::Close() {
    if (actionLog_.is_open()) {
        actionLog_.close();
    }
}

void DebugReplayRecorder::RecordAction(
    const DebugGameState& stateBefore,
    const DebugAction& action,
    const DebugGameState& stateAfter) {

    EnsureInitialState_(stateBefore);

    DebugActionRecord record;
    record.frameNumber = stateAfter.frameNumber;
    record.sceneName = stateAfter.sceneName.empty() ? stateBefore.sceneName : stateAfter.sceneName;
    record.action = action;
    record.stateBefore = stateBefore;
    record.stateAfter = stateAfter;

    recentRecords_.push_back(record);
    while (recentRecords_.size() > maxRecentRecords_) {
        recentRecords_.pop_front();
    }

    if (actionLog_.is_open()) {
        WriteRecord_(actionLog_, record);
        actionLog_ << "\n";
        actionLog_.flush();

        std::ofstream latestPath(directoryPath_ + "/debug_ai_latest_action_log.txt", std::ios::out | std::ios::trunc);
        if (latestPath.is_open()) {
            latestPath << actionLogPath_;
        }
    }
}

std::string DebugReplayRecorder::SaveRecentReplayForIssue(const DebugIssue& issue) {
    if (directoryPath_.empty()) {
        return "";
    }

    ++issueReplayIndex_;

    std::ostringstream fileName;
    fileName << directoryPath_ << "/issue_replay_"
        << std::setw(4) << std::setfill('0') << issueReplayIndex_
        << "_frame_" << issue.frameNumber
        << ".jsonl";

    std::ofstream out(fileName.str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return "";
    }

    if (hasInitialState_) {
        WriteInitialState_(out, initialState_);
        out << "\n";
    }

    out << "{"
        << "\"type\":\"issue\","
        << "\"message\":\"" << EscapeJson_(issue.message) << "\","
        << "\"frame\":" << issue.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(issue.sceneName) << "\","
        << "\"lastAction\":";
    WriteActionJson_(out, issue.lastAction);
    out << "}\n";

    for (const DebugActionRecord& record : recentRecords_) {
        WriteRecord_(out, record);
        out << "\n";
    }

    lastIssueReplayPath_ = fileName.str();
    return lastIssueReplayPath_;
}

void DebugReplayRecorder::EnsureInitialState_(const DebugGameState& state) {
    if (hasInitialState_) {
        return;
    }

    initialState_ = state;
    hasInitialState_ = true;

    if (actionLog_.is_open()) {
        WriteInitialState_(actionLog_, initialState_);
        actionLog_ << "\n";
        actionLog_.flush();
    }

    std::ofstream initialStateFile(initialStatePath_, std::ios::out | std::ios::trunc);
    if (initialStateFile.is_open()) {
        WriteInitialState_(initialStateFile, initialState_);
        initialStateFile << "\n";
    }
}

void DebugReplayRecorder::WriteInitialState_(std::ostream& out, const DebugGameState& state) const {
    out << "{"
        << "\"type\":\"initial_state\","
        << "\"state\":";
    WriteStateSummaryJson_(out, state);
    out << ",\"availableActions\":[";
    for (size_t i = 0; i < state.availableActions.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        WriteActionJson_(out, state.availableActions[i]);
    }
    out << "]}";
}

void DebugReplayRecorder::WriteRecord_(std::ostream& out, const DebugActionRecord& record) const {
    out << "{"
        << "\"type\":\"action\","
        << "\"frame\":" << record.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(record.sceneName) << "\","
        << "\"action\":";
    WriteActionJson_(out, record.action);
    out << ",\"before\":";
    WriteStateSummaryJson_(out, record.stateBefore);
    out << ",\"after\":";
    WriteStateSummaryJson_(out, record.stateAfter);
    out << "}";
}

void DebugReplayRecorder::WriteActionJson_(std::ostream& out, const DebugAction& action) const {
    out << "{"
        << "\"name\":\"" << EscapeJson_(action.name) << "\","
        << "\"targetId\":\"" << EscapeJson_(action.targetId) << "\","
        << "\"intParam\":" << action.intParam << ","
        << "\"floatParam\":" << action.floatParam
        << "}";
}

void DebugReplayRecorder::WriteStateSummaryJson_(std::ostream& out, const DebugGameState& state) const {
    out << "{"
        << "\"frame\":" << state.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(state.sceneName) << "\","
        << "\"playerHp\":" << state.playerHp << ","
        << "\"enemyHp\":" << state.enemyHp << ","
        << "\"enemyCount\":" << state.enemyCount << ","
        << "\"playerPosition\":{"
        << "\"x\":" << state.playerPosition.x << ","
        << "\"y\":" << state.playerPosition.y << ","
        << "\"z\":" << state.playerPosition.z
        << "},"
        << "\"fps\":" << state.fps << ","
        << "\"phase\":\"" << EscapeJson_(state.gamePhase) << "\","
        << "\"enemies\":[";
    for (size_t i = 0; i < state.enemies.size(); ++i) {
        const DebugEnemyState& enemy = state.enemies[i];
        if (i > 0) {
            out << ",";
        }
        out << "{"
            << "\"type\":\"" << EscapeJson_(enemy.type) << "\","
            << "\"hp\":" << enemy.hp << ","
            << "\"pendingSpawn\":" << (enemy.pendingSpawn ? "true" : "false") << ","
            << "\"spawnDelay\":" << enemy.spawnDelay << ","
            << "\"position\":{"
            << "\"x\":" << enemy.position.x << ","
            << "\"y\":" << enemy.position.y << ","
            << "\"z\":" << enemy.position.z
            << "}"
            << "}";
    }
    out << "]"
        << "}";
}

std::string DebugReplayRecorder::EscapeJson_(const std::string& text) const {
    std::ostringstream escaped;
    for (char c : text) {
        switch (c) {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default: escaped << c; break;
        }
    }
    return escaped.str();
}
