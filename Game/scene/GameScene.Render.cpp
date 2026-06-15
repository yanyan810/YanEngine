#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include "WinApp.h"
#include "Matrix4x4.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <d3d12.h>

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
    ParticleManager::GetInstance()->Draw(cmd);
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

    ImGui::Begin("Bone Attach");
    static char weaponBoneName[128] = "ボーン.017";
    static Vector3 weaponOffset{ 0.0f, 0.0f, 0.0f };
    static Vector3 weaponRotate{ 0.0f, 0.0f, 0.0f };
    static Vector3 weaponScale{ 0.15f, 0.15f, 0.15f };

    ImGui::InputText("Weapon Bone", weaponBoneName, sizeof(weaponBoneName));
    ImGui::DragFloat3("Weapon Offset", &weaponOffset.x, 0.01f);
    ImGui::DragFloat3("Weapon Rotate", &weaponRotate.x, 0.01f);
    ImGui::DragFloat3("Weapon Scale", &weaponScale.x, 0.01f, 0.001f, 10.0f);
    if (player_ && ImGui::Button("Apply Weapon Attach")) {
        player_->SetWeaponAttachment({ weaponBoneName }, weaponOffset, weaponRotate, weaponScale);
    }

    ImGui::Separator();
    static char particleGroupName[128] = "BoneSpark";
    static char particleBoneName[128] = "ボーン.017";
    static Vector3 particleOffset{ 0.0f, 0.0f, 0.0f };
    static int particleCount = 16;

    ImGui::InputText("Particle Group", particleGroupName, sizeof(particleGroupName));
    ImGui::InputText("Particle Bone", particleBoneName, sizeof(particleBoneName));
    ImGui::DragFloat3("Particle Offset", &particleOffset.x, 0.01f);
    ImGui::DragInt("Particle Count", &particleCount, 1, 1, 1024);

    if (ImGui::Button("Create Default Group")) {
        ParticleManager::GetInstance()->CreateParticleGroup(particleGroupName, "resources/white1x1.png");
    }
    ImGui::SameLine();
    if (player_ && ImGui::Button("Emit From Bone")) {
        player_->EmitParticleFromBone(
            particleGroupName,
            particleBoneName,
            static_cast<uint32_t>(std::max(1, particleCount)),
            particleOffset);
    }

    if (player_) {
        Vector3 bonePos{};
        if (player_->TryGetBoneWorldPosition(particleBoneName, bonePos, particleOffset)) {
            ImGui::Text("Bone Pos: %.2f, %.2f, %.2f", bonePos.x, bonePos.y, bonePos.z);
        } else {
            ImGui::TextDisabled("Bone not found or pose is not ready.");
        }
    }
    ImGui::End();

    ParticleManager::GetInstance()->DrawImGui();

    debugAIImGuiPanelState_.botRunning = debugAIEnabled_;
    const DebugAIImGuiPanelRequests debugAIRequests =
        DrawDebugAIImGuiPanel(app.DebugAI(), CaptureDebugState(), debugAIImGuiPanelState_);
    if (debugAIRequests.stopReplay) {
        debugRequestStopReplay_ = true;
    }
    if (debugAIRequests.startReplay) {
        debugReplayStartPath_ = debugAIRequests.replayPath;
        debugRequestStartReplay_ = true;
    }
    if (debugAIRequests.startBot) {
        debugRequestStartBot_ = true;
    }
    if (debugAIRequests.stopBot) {
        debugRequestStopBot_ = true;
    }
    if (debugAIRequests.restoreInitialState) {
        debugRequestRestoreInitialState_ = true;
    }
#endif // USE_IMGUI
}
