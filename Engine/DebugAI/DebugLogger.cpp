#include "DebugLogger.h"

#include <filesystem>
#include <sstream>

bool DebugLogger::Open(const std::string& directoryPath) {
    directoryPath_ = std::filesystem::absolute(directoryPath).string();
    std::filesystem::create_directories(directoryPath_);

    frameLog_.open(directoryPath_ + "/debug_ai_frames.jsonl", std::ios::out | std::ios::trunc);
    issueLog_.open(directoryPath_ + "/debug_ai_issues.jsonl", std::ios::out | std::ios::trunc);
    eventLog_.open(directoryPath_ + "/debug_ai_events.jsonl", std::ios::out | std::ios::trunc);
    return frameLog_.is_open() && issueLog_.is_open();
}

void DebugLogger::Close() {
    if (frameLog_.is_open()) {
        frameLog_.close();
    }
    if (issueLog_.is_open()) {
        issueLog_.close();
    }
    if (eventLog_.is_open()) {
        eventLog_.close();
    }
}

void DebugLogger::LogFrame(const DebugGameState& state, const DebugAction* action) {
    if (!frameLog_.is_open()) {
        return;
    }

    frameLog_
        << "{"
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
        << "\"entityCount\":" << state.entities.size() << ","
        << "\"randomSeed\":" << state.randomSeed << ","
        << "\"action\":\"" << EscapeJson_(action ? ActionToString_(*action) : "") << "\""
        << "}\n";
    frameLog_.flush();
}

void DebugLogger::LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message) {
    if (!eventLog_.is_open()) {
        return;
    }

    eventLog_
        << "{"
        << "\"frame\":" << state.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(state.sceneName) << "\","
        << "\"event\":\"" << EscapeJson_(eventName) << "\","
        << "\"message\":\"" << EscapeJson_(message) << "\","
        << "\"playerHp\":" << state.playerHp << ","
        << "\"enemyHp\":" << state.enemyHp << ","
        << "\"enemyCount\":" << state.enemyCount << ","
        << "\"entityCount\":" << state.entities.size() << ","
        << "\"randomSeed\":" << state.randomSeed
        << "}\n";
    eventLog_.flush();
}

void DebugLogger::LogIssue(const DebugIssue& issue) {
    issues_.push_back(issue);

    if (!issueLog_.is_open()) {
        return;
    }

    issueLog_
        << "{"
        << "\"severity\":\"" << SeverityToString_(issue.severity) << "\","
        << "\"message\":\"" << EscapeJson_(issue.message) << "\","
        << "\"frame\":" << issue.frameNumber << ","
        << "\"scene\":\"" << EscapeJson_(issue.sceneName) << "\","
        << "\"lastAction\":\"" << EscapeJson_(ActionToString_(issue.lastAction)) << "\","
        << "\"replayPath\":\"" << EscapeJson_(issue.replayPath) << "\""
        << "}\n";
    issueLog_.flush();
    WriteReport();
}

void DebugLogger::WriteReport() {
    if (directoryPath_.empty()) {
        return;
    }

    std::ofstream report(directoryPath_ + "/debug_ai_report.txt", std::ios::out | std::ios::trunc);
    if (!report.is_open()) {
        return;
    }

    report << "Debug AI Report\n";
    report << "Issue Count: " << issues_.size() << "\n\n";

    for (const DebugIssue& issue : issues_) {
        report << "Severity: " << SeverityToString_(issue.severity) << "\n";
        report << "Message: " << issue.message << "\n";
        report << "Frame: " << issue.frameNumber << "\n";
        report << "Scene: " << issue.sceneName << "\n";
        report << "LastAction: " << ActionToString_(issue.lastAction) << "\n\n";
        report << "ReplayPath: " << issue.replayPath << "\n\n";
    }
}

std::string DebugLogger::EscapeJson_(const std::string& text) const {
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

std::string DebugLogger::SeverityToString_(DebugIssueSeverity severity) const {
    switch (severity) {
    case DebugIssueSeverity::Info:
        return "Info";
    case DebugIssueSeverity::Warning:
        return "Warning";
    case DebugIssueSeverity::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

std::string DebugLogger::ActionToString_(const DebugAction& action) const {
    if (action.name.empty()) {
        return "";
    }
    if (action.targetId.empty()) {
        return action.name;
    }
    return action.name + "(" + action.targetId + ")";
}
