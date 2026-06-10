#pragma once

#include "DebugTypes.h"

#include <fstream>
#include <string>
#include <vector>

class DebugLogger {
public:
    bool Open(const std::string& directoryPath);
    void Close();

    void LogFrame(const DebugGameState& state, const DebugAction* action);
    void LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message);
    void LogIssue(const DebugIssue& issue);
    void WriteReport();

    const std::vector<DebugIssue>& Issues() const { return issues_; }
    const std::string& DirectoryPath() const { return directoryPath_; }

private:
    std::string EscapeJson_(const std::string& text) const;
    std::string SeverityToString_(DebugIssueSeverity severity) const;
    std::string ActionToString_(const DebugAction& action) const;

private:
    std::ofstream frameLog_;
    std::ofstream issueLog_;
    std::ofstream eventLog_;
    std::string directoryPath_;
    std::vector<DebugIssue> issues_;
};
