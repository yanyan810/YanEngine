#include "GameScene.h"
#include "GameApp.h"

#include "Camera.h"
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
}

// 繝舌ャ繧ｯ繝舌ャ繝輔ぃ縺ｸ逶ｴ謗･謠上￥3D・医・繧ｹ繝医お繝輔ぉ繧ｯ繝井ｸ崎ｦ√↑繧ゅ・・・
void GameScene::Draw3D(GameApp& app) {
    // 莉翫・迚ｹ縺ｫ縺ｪ縺・
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

// 縺昴・莉厄ｼ育ｩｺ縺ｧOK・・
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
#endif // USE_IMGUI
}
