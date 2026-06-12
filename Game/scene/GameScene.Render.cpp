#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
#include "DebugAI/DebugAIManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <d3d12.h>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const char* DebugIssueSeverityLabel(DebugIssueSeverity severity) {
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

void DrawDebugActionLine(const char* label, const DebugAction& action) {
    if (action.name.empty()) {
        ImGui::Text("%s: none", label);
        return;
    }

    ImGui::Text("%s: %s", label, action.name.c_str());
    if (!action.targetId.empty() ||
        action.intParam != 0 ||
        action.floatParam != 0.0f ||
        !action.stringParam.empty() ||
        action.holdFrames != 1) {
        ImGui::Text("  target=%s int=%d float=%.2f string=%s hold=%u",
            action.targetId.empty() ? "-" : action.targetId.c_str(),
            action.intParam,
            action.floatParam,
            action.stringParam.empty() ? "-" : action.stringParam.c_str(),
            action.holdFrames);
    }
}

struct DebugReplayLogEntry {
    std::string name;
    std::string path;
    std::filesystem::file_time_type lastWriteTime{};
};

std::vector<DebugReplayLogEntry> CollectReplayActionLogs(const std::string& directoryPath) {
    std::vector<DebugReplayLogEntry> entries;
    if (directoryPath.empty()) {
        return entries;
    }

    std::error_code error;
    const std::filesystem::path directory(directoryPath);
    if (!std::filesystem::exists(directory, error)) {
        return entries;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error)) {
            continue;
        }

        const std::filesystem::path path = entry.path();
        const std::string fileName = path.filename().string();
        const bool legacyRunLog =
            fileName.rfind("debug_ai_actions_run_", 0) == 0 && path.extension() == ".jsonl";
        const bool sessionRunLog =
            fileName == "debug_ai_actions.jsonl";
        if (!legacyRunLog && !sessionRunLog) {
            continue;
        }

        std::string displayName = fileName;
        std::error_code relativeError;
        const std::filesystem::path relativePath = std::filesystem::relative(path, directory, relativeError);
        if (!relativeError) {
            displayName = relativePath.string();
        }

        entries.push_back({
            displayName,
            path.string(),
            entry.last_write_time(error),
        });
    }

    std::sort(entries.begin(), entries.end(), [](const DebugReplayLogEntry& lhs, const DebugReplayLogEntry& rhs) {
        return lhs.lastWriteTime > rhs.lastWriteTime;
    });
    return entries;
}

}

void GameScene::DrawRender(GameApp& app) {
    auto* cmd = app.Dx()->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    skyDome_->Draw();

    if (phase_ == Phase::IntroVideo || phase_ == Phase::OutroVideo) {
        if (enableVideo_ && video_ && videoPlane_) {
            ID3D12DescriptorHeap* heaps[] = { app.Srv()->GetDescriptorHeap() };
            cmd->SetDescriptorHeaps(_countof(heaps), heaps);

            video_->UploadToGpu(cmd);
            D3D12_GPU_DESCRIPTOR_HANDLE vh = video_->SrvGpu();
            videoPlane_->DrawWithOverrideSrv(vh);
            video_->EndFrame(cmd);
        }
    } else if (phase_ == Phase::Battle) {
        if (ground_) ground_->Draw();
        if (player_) {
            player_->Draw();
        }
        enemyMgr_.Draw();
        if (player_) player_->DrawDebugHitBoxes(enemyMgr_);
    }

    // GPU Particle
    app.ParticleCom()->SetGraphicsPipelineState();
}

void GameScene::Draw3D(GameApp& app) {
}

// 2D / Sprite
void GameScene::Draw2D(GameApp& app) {
    app.SpriteCom()->SetGraphicsPipelineState();

    Matrix4x4 view = Matrix4x4::MakeIdentity4x4();
    Matrix4x4 proj = Matrix4x4::MakeOrthographicMatrix(
        0, 0, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0, 100);

    if (phase_ == Phase::Battle) {
        if (hpBack_) { hpBack_->Update(view, proj); hpBack_->Draw(); }
        if (hpFill_) { hpFill_->Update(view, proj); hpFill_->Draw(); }

        for (int i = 0; i < 3; ++i) {
            if (!hpDigits_[i]) continue;
            hpDigits_[i]->Update(view, proj);
            hpDigits_[i]->Draw();
        }

        if (bossHpBack_) { bossHpBack_->Update(view, proj); bossHpBack_->Draw(); }
        if (bossHpFill_) { bossHpFill_->Update(view, proj); bossHpFill_->Draw(); }
        for (int i = 0; i < 3; ++i) {
            if (!bossHpDigits_[i]) continue;
            bossHpDigits_[i]->Update(view, proj);
            bossHpDigits_[i]->Draw();
        }

        if (isPaused_) {
            if (pauseClose_) { pauseClose_->Update(view, proj); pauseClose_->Draw(); }
            if (pauseToTitle_) { pauseToTitle_->Update(view, proj); pauseToTitle_->Draw(); }
        }
    }
}

void GameScene::Draw(GameApp& app) {
}

void GameScene::DrawImGui(GameApp& app) {
#ifdef USE_IMGUI
    if (phase_ == Phase::IntroVideo && videoPlane_) {
        videoPlane_->SetTranslate(srtVideo_.pos);
        videoPlane_->SetRotate(srtVideo_.rot);
        videoPlane_->SetScale(srtVideo_.scale);
    }

    ImGui::Begin("VideoPlane SRT");
    ImGui::DragFloat3("T", &srtVideo_.pos.x, 0.1f);
    ImGui::DragFloat3("R", &srtVideo_.rot.x, 0.01f);
    ImGui::DragFloat3("S", &srtVideo_.scale.x, 0.1f);
    ImGui::End();

    ImGui::Begin("Object Specific Effects");
    
    // Player
    if (ImGui::CollapsingHeader("Player")) {
        auto* playerModel = player_->GetModelObject();
        if (playerModel) {
            bool pOutline = playerModel->GetEnableOutline();
            if (ImGui::Checkbox("Outline##Player", &pOutline)) playerModel->SetEnableOutline(pOutline);
            
            bool pDissolve = playerModel->GetEnableDissolve();
            if (ImGui::Checkbox("Dissolve##Player", &pDissolve)) playerModel->SetEnableDissolve(pDissolve);
            
            bool pRandom = playerModel->GetEnableRandom();
            if (ImGui::Checkbox("Random##Player", &pRandom)) playerModel->SetEnableRandom(pRandom);
            
            if (pRandom) {
                playerModel->SetRandomTime((float)ImGui::GetTime());
            }
        }
    }
    
    // Ground
    if (ImGui::CollapsingHeader("Ground")) {
        if (ground_) {
            bool gOutline = ground_->GetEnableOutline();
            if (ImGui::Checkbox("Outline##Ground", &gOutline)) ground_->SetEnableOutline(gOutline);
            
            bool gDissolve = ground_->GetEnableDissolve();
            if (ImGui::Checkbox("Dissolve##Ground", &gDissolve)) ground_->SetEnableDissolve(gDissolve);
            
            bool gRandom = ground_->GetEnableRandom();
            if (ImGui::Checkbox("Random##Ground", &gRandom)) ground_->SetEnableRandom(gRandom);
            
            if (gRandom) {
                ground_->SetRandomTime((float)ImGui::GetTime());
            }
        }
    }
    
    ImGui::End();

    DebugAIManager* debugAI = app.DebugAI();
    const DebugGameState debugState = CaptureDebugState();
    ImGui::Begin("Debug AI");
    if (!debugAI) {
        ImGui::TextUnformatted("DebugAI manager is not available.");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Status: %s", debugAI->IsEnabled() ? "Running" : "Stopped");
        ImGui::Text("Replay: %s", debugAI->IsReplayPlaying() ? "Playing" : "Stopped");
        ImGui::Text("Bot: %s", debugAI->CurrentBotName());

        if (debugAI->IsReplayPlaying()) {
            if (ImGui::Button("Stop Replay")) {
                debugRequestStopReplay_ = true;
            }
        } else {
            if (ImGui::Button("Replay Latest (F7)")) {
                debugSelectedReplayPath_.clear();
                debugRequestStartReplay_ = true;
            }
        }

        const std::vector<DebugReplayLogEntry> replayLogs =
            CollectReplayActionLogs(debugAI->Logger().DirectoryPath());
        if (!replayLogs.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Replay File");
            const std::string selectedReplayName = debugSelectedReplayPath_.empty()
                ? "(latest)"
                : std::filesystem::path(debugSelectedReplayPath_).filename().string();
            ImGui::Text("Selected: %s", selectedReplayName.c_str());

            if (!debugAI->IsReplayPlaying()) {
                if (ImGui::Button("Replay Selected")) {
                    debugRequestStartReplay_ = true;
                }
            }

            ImGui::BeginChild("ReplayLogList", ImVec2(0.0f, 150.0f), true);
            if (ImGui::Selectable("(latest)", debugSelectedReplayPath_.empty())) {
                debugSelectedReplayPath_.clear();
            }
            const size_t maxRows = std::min<size_t>(replayLogs.size(), 20);
            for (size_t i = 0; i < maxRows; ++i) {
                const DebugReplayLogEntry& entry = replayLogs[i];
                const bool selected = debugSelectedReplayPath_ == entry.path;
                if (ImGui::Selectable(entry.name.c_str(), selected)) {
                    debugSelectedReplayPath_ = entry.path;
                }
            }
            ImGui::EndChild();
        } else {
            ImGui::TextDisabled("Replay logs: none");
        }

        ImGui::SameLine();
        if (debugAI->IsReplayPlaying()) {
            ImGui::TextDisabled("Random Bot locked during replay");
        } else if (debugAIEnabled_) {
            if (ImGui::Button("Stop Random Bot (F8)")) {
                debugRequestStopBot_ = true;
            }
        } else {
            if (ImGui::Button("Start Random Bot (F8)")) {
                debugRequestStartBot_ = true;
            }
        }

        if (debugAI->ReplayPlayer().HasInitialState()) {
            if (ImGui::Button("Restore Initial State")) {
                debugRequestRestoreInitialState_ = true;
            }
        } else {
            ImGui::TextDisabled("Restore Initial State: no replay initial state loaded");
        }
    }

    if (ImGui::CollapsingHeader("State", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Scene: %s", debugState.sceneName.c_str());
        ImGui::Text("Phase: %s", debugState.gamePhase.c_str());
        ImGui::Text("Frame: %llu", debugState.frameNumber);
        ImGui::Text("Player HP: %d", debugState.playerHp);
        ImGui::Text("Enemy HP: %d", debugState.enemyHp);
        ImGui::Text("Enemy Count: %d", debugState.enemyCount);
        ImGui::Text("Entities: %zu", debugState.entities.size());
        ImGui::Text("FPS: %.2f", debugState.fps);
        ImGui::Text("Random Seed: %u", debugState.randomSeed);
        ImGui::Text("Player Pos: %.2f, %.2f, %.2f",
            debugState.playerPosition.x,
            debugState.playerPosition.y,
            debugState.playerPosition.z);
    }

    if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDebugActionLine("Last Action", debugAI->LastAction());
        ImGui::Separator();
        ImGui::Text("Available Actions: %zu", debugState.availableActions.size());
        for (size_t i = 0; i < debugState.availableActions.size(); ++i) {
            const DebugAction& action = debugState.availableActions[i];
            char label[128];
            std::snprintf(label, sizeof(label), "%02zu: %s", i + 1, action.name.c_str());
            ImGui::BulletText("%s", label);
        }
        ImGui::Separator();
        ImGui::Text("Replay Step: %zu / %zu",
            debugAI->ReplayPlayer().NextIndex(),
            debugAI->ReplayPlayer().ActionCount());
        ImGui::Text("Checkpoint Step: %zu / %zu",
            debugAI->ReplayPlayer().NextCheckpointIndex(),
            debugAI->ReplayPlayer().CheckpointCount());
    }

    if (ImGui::CollapsingHeader("Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::vector<DebugIssue>& issues = debugAI->Logger().Issues();
        ImGui::Text("Issue Count: %zu", issues.size());
        const size_t first = issues.size() > 8 ? issues.size() - 8 : 0;
        for (size_t i = first; i < issues.size(); ++i) {
            const DebugIssue& issue = issues[i];
            ImGui::Separator();
            ImGui::Text("[%s] Frame %llu", DebugIssueSeverityLabel(issue.severity), issue.frameNumber);
            ImGui::TextWrapped("%s", issue.message.c_str());
            DrawDebugActionLine("Last Action", issue.lastAction);
            if (!issue.replayPath.empty()) {
                ImGui::TextWrapped("Replay: %s", issue.replayPath.c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Paths")) {
        ImGui::TextWrapped("Logs: %s", debugAI->Logger().DirectoryPath().c_str());
        ImGui::TextWrapped("Session: %s", debugAI->Logger().SessionDirectoryPath().c_str());
        ImGui::TextWrapped("Actions: %s", debugAI->ReplayRecorder().ActionLogPath().c_str());
        ImGui::TextWrapped("Initial: %s", debugAI->ReplayRecorder().InitialStatePath().c_str());
        ImGui::TextWrapped("Replay: %s", debugAI->ReplayPlayer().ReplayPath().c_str());
    }
    ImGui::End();
#endif // USE_IMGUI
}
