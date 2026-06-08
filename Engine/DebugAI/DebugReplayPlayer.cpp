#include "DebugReplayPlayer.h"

#include <filesystem>
#include <fstream>

bool DebugReplayPlayer::Load(const std::string& replayPath) {
    std::ifstream in(replayPath);
    if (!in.is_open()) {
        return false;
    }

    std::vector<DebugReplayAction> loaded;
    DebugGameState loadedInitialState;
    bool loadedHasInitialState = false;
    std::string line;
    while (std::getline(in, line)) {
        DebugGameState initialState;
        if (ParseInitialStateLine_(line, initialState)) {
            loadedInitialState = initialState;
            loadedHasInitialState = true;
            continue;
        }

        DebugReplayAction action;
        if (ParseActionLine_(line, action)) {
            loaded.push_back(action);
        }
    }

    if (loaded.empty()) {
        return false;
    }

    actions_ = std::move(loaded);
    initialState_ = loadedInitialState;
    hasInitialState_ = loadedHasInitialState;
    replayPath_ = std::filesystem::absolute(replayPath).string();
    playing_ = false;
    nextIndex_ = 0;
    firstRecordedFrame_ = actions_.front().recordedFrame;
    replayStartFrame_ = 0;
    return true;
}

bool DebugReplayPlayer::LoadLatestFromDirectory(const std::string& directoryPath) {
    const std::string pointerPath = std::filesystem::absolute(directoryPath).string() + "/debug_ai_latest_action_log.txt";
    std::ifstream pointer(pointerPath);
    if (!pointer.is_open()) {
        return false;
    }

    std::string replayPath;
    std::getline(pointer, replayPath);
    if (replayPath.empty()) {
        return false;
    }

    return Load(replayPath);
}

void DebugReplayPlayer::Start(unsigned long long currentFrame) {
    if (actions_.empty()) {
        return;
    }

    playing_ = true;
    nextIndex_ = 0;
    replayStartFrame_ = currentFrame;
    firstRecordedFrame_ = actions_.front().recordedFrame;
}

void DebugReplayPlayer::Stop() {
    playing_ = false;
}

bool DebugReplayPlayer::PopDueAction(unsigned long long currentFrame, DebugReplayAction& outAction) {
    if (!playing_ || nextIndex_ >= actions_.size()) {
        return false;
    }

    const DebugReplayAction& next = actions_[nextIndex_];
    const unsigned long long offset = next.recordedFrame >= firstRecordedFrame_
        ? next.recordedFrame - firstRecordedFrame_
        : 0;
    const unsigned long long targetFrame = replayStartFrame_ + offset;

    if (currentFrame < targetFrame) {
        return false;
    }

    outAction = next;
    ++nextIndex_;
    if (nextIndex_ >= actions_.size()) {
        playing_ = false;
    }
    return true;
}

bool DebugReplayPlayer::ParseActionLine_(const std::string& line, DebugReplayAction& outAction) const {
    std::string type;
    if (!ExtractString_(line, "\"type\"", type) || type != "action") {
        return false;
    }

    if (!ExtractUnsignedLongLong_(line, "\"frame\"", outAction.recordedFrame)) {
        return false;
    }

    ExtractString_(line, "\"scene\"", outAction.sceneName);
    ExtractString_(line, "\"name\"", outAction.action.name);
    ExtractString_(line, "\"targetId\"", outAction.action.targetId);
    ExtractInt_(line, "\"intParam\"", outAction.action.intParam);
    ExtractFloat_(line, "\"floatParam\"", outAction.action.floatParam);

    return !outAction.action.name.empty();
}

bool DebugReplayPlayer::ParseInitialStateLine_(const std::string& line, DebugGameState& outState) const {
    std::string type;
    if (!ExtractString_(line, "\"type\"", type) || type != "initial_state") {
        return false;
    }

    ExtractUnsignedLongLong_(line, "\"frame\"", outState.frameNumber);
    ExtractString_(line, "\"scene\"", outState.sceneName);
    ExtractInt_(line, "\"playerHp\"", outState.playerHp);
    ExtractInt_(line, "\"enemyHp\"", outState.enemyHp);
    ExtractInt_(line, "\"enemyCount\"", outState.enemyCount);
    ExtractFloat_(line, "\"x\"", outState.playerPosition.x);
    ExtractFloat_(line, "\"y\"", outState.playerPosition.y);
    ExtractFloat_(line, "\"z\"", outState.playerPosition.z);
    ExtractFloat_(line, "\"fps\"", outState.fps);
    ExtractString_(line, "\"phase\"", outState.gamePhase);
    ParseEnemyStates_(line, outState.enemies);

    return !outState.sceneName.empty();
}

void DebugReplayPlayer::ParseEnemyStates_(const std::string& line, std::vector<DebugEnemyState>& outEnemies) const {
    outEnemies.clear();

    const size_t keyPos = line.find("\"enemies\"");
    if (keyPos == std::string::npos) {
        return;
    }
    const size_t arrayStart = line.find('[', keyPos);
    if (arrayStart == std::string::npos) {
        return;
    }

    int arrayDepth = 0;
    int objectDepth = 0;
    size_t objectStart = std::string::npos;
    for (size_t i = arrayStart; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '[') {
            ++arrayDepth;
            continue;
        }
        if (c == ']') {
            --arrayDepth;
            if (arrayDepth <= 0) {
                break;
            }
            continue;
        }
        if (arrayDepth <= 0) {
            continue;
        }

        if (c == '{') {
            if (objectDepth == 0) {
                objectStart = i;
            }
            ++objectDepth;
        } else if (c == '}') {
            --objectDepth;
            if (objectDepth == 0 && objectStart != std::string::npos) {
                const std::string objectText = line.substr(objectStart, i - objectStart + 1);
                DebugEnemyState enemy;
                ExtractString_(objectText, "\"type\"", enemy.type);
                ExtractInt_(objectText, "\"hp\"", enemy.hp);
                ExtractFloat_(objectText, "\"x\"", enemy.position.x);
                ExtractFloat_(objectText, "\"y\"", enemy.position.y);
                ExtractFloat_(objectText, "\"z\"", enemy.position.z);
                ExtractBool_(objectText, "\"pendingSpawn\"", enemy.pendingSpawn);
                ExtractFloat_(objectText, "\"spawnDelay\"", enemy.spawnDelay);
                if (!enemy.type.empty()) {
                    outEnemies.push_back(enemy);
                }
                objectStart = std::string::npos;
            }
        }
    }
}

bool DebugReplayPlayer::ExtractString_(const std::string& text, const std::string& key, std::string& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    const size_t firstQuote = text.find('"', colon + 1);
    if (firstQuote == std::string::npos) {
        return false;
    }

    std::string value;
    bool escaped = false;
    for (size_t i = firstQuote + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            outValue = UnescapeJson_(value);
            return true;
        }
        value.push_back(c);
    }

    return false;
}

bool DebugReplayPlayer::ExtractUnsignedLongLong_(const std::string& text, const std::string& key, unsigned long long& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stoull(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractInt_(const std::string& text, const std::string& key, int& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    if (end < text.size() && (text[end] == '-' || text[end] == '+')) {
        ++end;
    }
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stoi(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractFloat_(const std::string& text, const std::string& key, float& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t end = colon + 1;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    const size_t start = end;
    while (end < text.size() &&
        ((text[end] >= '0' && text[end] <= '9') || text[end] == '-' || text[end] == '+' || text[end] == '.')) {
        ++end;
    }
    if (start == end) {
        return false;
    }

    outValue = std::stof(text.substr(start, end - start));
    return true;
}

bool DebugReplayPlayer::ExtractBool_(const std::string& text, const std::string& key, bool& outValue) const {
    const size_t keyPos = text.find(key);
    if (keyPos == std::string::npos) {
        return false;
    }

    const size_t colon = text.find(':', keyPos + key.size());
    if (colon == std::string::npos) {
        return false;
    }

    size_t start = colon + 1;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
        ++start;
    }

    if (text.compare(start, 4, "true") == 0) {
        outValue = true;
        return true;
    }
    if (text.compare(start, 5, "false") == 0) {
        outValue = false;
        return true;
    }
    return false;
}

std::string DebugReplayPlayer::UnescapeJson_(const std::string& text) const {
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (char c : text) {
        if (!escaped) {
            if (c == '\\') {
                escaped = true;
            } else {
                result.push_back(c);
            }
            continue;
        }

        switch (c) {
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        default: result.push_back(c); break;
        }
        escaped = false;
    }
    return result;
}
