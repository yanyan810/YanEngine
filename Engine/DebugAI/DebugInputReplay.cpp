#include "DebugInputReplay.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

constexpr std::uint32_t kMagic = 0x52494144;

}

bool DebugInputReplay::Open(const std::string& directoryPath) {
    Close();
    directoryPath_ = directoryPath;
    std::error_code error;
    std::filesystem::create_directories(directoryPath_, error);
    if (error) {
        SetError_("Failed to create input replay directory: " + error.message());
        return false;
    }
    return true;
}

void DebugInputReplay::Close() {
    StopRecording();
    StopReplay();
    directoryPath_.clear();
}

bool DebugInputReplay::StartRecording() {
    StopReplay();
    StopRecording();
    if (directoryPath_.empty()) {
        SetError_("Input replay directory is not open.");
        return false;
    }

    replayPath_ = CreateSessionPath_();
    output_.open(replayPath_, std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) {
        SetError_("Failed to open input replay for writing: " + replayPath_);
        return false;
    }

    header_ = {};
    output_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
    currentFrame_ = 0;
    inputProcessedThisFrame_ = false;
    recording_ = true;
    lastError_.clear();
    return true;
}

std::string DebugInputReplay::StopRecording() {
    if (!recording_) {
        return replayPath_;
    }

    recording_ = false;
    if (output_.is_open()) {
        header_.frameCount = currentFrame_;
        output_.seekp(0, std::ios::beg);
        output_.write(reinterpret_cast<const char*>(&header_), sizeof(header_));
        output_.close();
    }
    return replayPath_;
}

bool DebugInputReplay::StartReplay(const std::string& replayPath) {
    StopRecording();
    StopReplay();
    if (!Load_(replayPath)) {
        return false;
    }
    currentFrame_ = 0;
    inputProcessedThisFrame_ = false;
    playing_ = true;
    return true;
}

bool DebugInputReplay::StartLatestReplay() {
    if (directoryPath_.empty()) {
        return false;
    }

    std::filesystem::path latest;
    std::filesystem::file_time_type latestTime{};
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directoryPath_, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".dair") {
            continue;
        }
        const auto writeTime = entry.last_write_time(error);
        if (!error && (latest.empty() || writeTime > latestTime)) {
            latest = entry.path();
            latestTime = writeTime;
        }
    }
    return !latest.empty() && StartReplay(latest.string());
}

void DebugInputReplay::StopReplay() {
    playing_ = false;
    replayData_.clear();
    currentFrame_ = 0;
    inputProcessedThisFrame_ = false;
}

bool DebugInputReplay::ProcessBytes_(void* input, std::size_t size) {
    if (!recording_ && !playing_) {
        return true;
    }
    if (inputProcessedThisFrame_) {
        SetError_("ProcessInput was called more than once in one replay frame.");
        return false;
    }
    inputProcessedThisFrame_ = true;

    if (header_.commandSize == 0) {
        header_.commandSize = static_cast<std::uint32_t>(size);
    }
    if (header_.commandSize != size) {
        SetError_("Input command size does not match the replay file.");
        StopReplay();
        return false;
    }

    if (recording_) {
        output_.write(reinterpret_cast<const char*>(input), static_cast<std::streamsize>(size));
        if (!output_) {
            SetError_("Failed to write input replay frame.");
            StopRecording();
            return false;
        }
        return true;
    }

    const std::size_t offset = static_cast<std::size_t>(currentFrame_) * size;
    if (offset + size > replayData_.size()) {
        StopReplay();
        return false;
    }
    std::memcpy(input, replayData_.data() + offset, size);
    return true;
}

void DebugInputReplay::EndFrame() {
    if (!recording_ && !playing_) {
        return;
    }
    if (!inputProcessedThisFrame_) {
        SetError_("EndFrame was called without ProcessInput.");
    }
    inputProcessedThisFrame_ = false;
    ++currentFrame_;
    if (playing_ && currentFrame_ >= header_.frameCount) {
        StopReplay();
    }
}

bool DebugInputReplay::Load_(const std::string& replayPath) {
    std::ifstream input(replayPath, std::ios::binary);
    if (!input.is_open()) {
        SetError_("Failed to open input replay: " + replayPath);
        return false;
    }

    Header loadedHeader{};
    input.read(reinterpret_cast<char*>(&loadedHeader), sizeof(loadedHeader));
    if (!input || loadedHeader.magic != kMagic || loadedHeader.version != 1 || loadedHeader.commandSize == 0) {
        SetError_("Invalid or unsupported input replay header.");
        return false;
    }

    const std::size_t dataSize = static_cast<std::size_t>(loadedHeader.frameCount) * loadedHeader.commandSize;
    std::vector<std::byte> data(dataSize);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::size_t>(input.gcount()) != data.size()) {
        SetError_("Input replay data is truncated.");
        return false;
    }

    header_ = loadedHeader;
    replayData_ = std::move(data);
    replayPath_ = replayPath;
    lastError_.clear();
    return true;
}

std::string DebugInputReplay::CreateSessionPath_() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream name;
    name << "input_" << std::put_time(&local, "%Y%m%d_%H%M%S") << ".dair";
    return (std::filesystem::path(directoryPath_) / name.str()).string();
}

void DebugInputReplay::SetError_(std::string message) {
    lastError_ = std::move(message);
}
