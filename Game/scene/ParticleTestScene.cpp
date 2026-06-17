#include "ParticleTestScene.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "Input.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"

#ifdef USE_IMGUI
#include <imgui.h>
#endif

#include <algorithm>
#include <string>

namespace {
constexpr const char* kParticleJson = "test_particles.json";
}

void ParticleTestScene::OnEnter(GameApp& app)
{
    if (auto* input = app.GetInput()) {
        input->SetCameraControlEnabled(false);
    }

    app.Render()->SetMode(PostEffectMode::FullScreen);

    TextureManager::GetInstance()->LoadTexture("resources/circle.png");
    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate({ 0.0f, 3.0f, -20.0f });
    camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
    camera_->Update();
    app.ObjCom()->SetDefaultCamera(camera_.get());

    ground_ = std::make_unique<Object3d>();
    ground_->Initialize(app.ObjCom(), app.Dx());
    ground_->SetCamera(camera_.get());
    ground_->SetModel("ground/ground.obj");
    ground_->SetTranslate({ 0.0f, -5.0f, 0.0f });
    ground_->SetScale({ 1.0f, 1.0f, 1.0f });
    ground_->SetEnableLighting(0);

    editorParticle_ = std::make_unique<Particle>();
    editorParticle_->Initialize(app.ParticleCom(), app.Dx(), app.Srv());
    editorParticle_->SetModel("plane.obj");
    editorParticle_->SetCamera(camera_.get());
    editorParticle_->SetBlendMode(ParticleCommon::BlendMode::kBlendModeAdd);
    editorParticle_->SetMaterialColor({ 1, 1, 1, 1 });

    ReloadParticleJson_();
}

void ParticleTestScene::OnExit(GameApp&)
{
    ParticleManager::GetInstance()->ClearGroups();
    editorParticle_.reset();
    ground_.reset();
    camera_.reset();
}

void ParticleTestScene::Update(GameApp& app, float dt)
{
    const Input* input = app.GetInput();
    if (!input) {
        return;
    }

    if (input->IsKeyTrigger(DIK_ESCAPE)) {
        RequestChangeScene_("Title");
        return;
    }

    reloadCooldown_ = std::max(0.0f, reloadCooldown_ - dt);
    if (input->IsKeyTrigger(DIK_R) && reloadCooldown_ <= 0.0f) {
        ReloadParticleJson_();
        reloadCooldown_ = 0.2f;
    }

    if (input->IsKeyTrigger(DIK_SPACE)) {
        SpawnHitEffectPreview_();
    }

    if (camera_) {
        camera_->Update();
        ParticleManager::GetInstance()->Update(dt, *camera_);
    }

    if (ground_) {
        ground_->Update(dt);
    }

    if (editorParticle_) {
        editorParticle_->Update();
    }
}

void ParticleTestScene::DrawRender(GameApp& app)
{
    if (ground_) {
  //      ground_->Draw();
    }

    if (editorParticle_) {
        app.ParticleCom()->SetGraphicsPipelineState();
        editorParticle_->Draw();
    }

    ParticleManager::GetInstance()->Draw(app.Dx()->GetCommandList());
}

void ParticleTestScene::Draw3D(GameApp&)
{
}

void ParticleTestScene::Draw2D(GameApp&)
{
}

void ParticleTestScene::Draw(GameApp&)
{
}

void ParticleTestScene::EnsureHitEffectGroup_()
{
    auto* particleManager = ParticleManager::GetInstance();
    const std::string groupName = hitEffectGroupName_;
    if (groupName.empty()) {
        return;
    }

    if (!particleManager->HasGroup(groupName)) {
        particleManager->CreateParticleGroup(groupName, "resources/circle.png");
        particleManager->ConfigureHitEffectPreset(groupName);
    }
}

void ParticleTestScene::ReloadParticleJson_()
{
    auto* particleManager = ParticleManager::GetInstance();
    particleManager->ClearGroups();
    particleManager->Load(kParticleJson);
    EnsureHitEffectGroup_();
}

void ParticleTestScene::SpawnHitEffectPreview_()
{
    const std::string groupName = hitEffectGroupName_;
    if (groupName.empty()) {
        return;
    }

    EnsureHitEffectGroup_();
    ParticleManager::GetInstance()->Emit(
        groupName,
        hitEffectSpawnPosition_,
        static_cast<uint32_t>(std::max(1, hitEffectSpawnCount_)));
}

void ParticleTestScene::DrawImGui(GameApp&)
{
#ifdef USE_IMGUI
    ImGui::Begin("Particle Test Scene");
    ImGui::Text("JSON: %s", kParticleJson);
    ImGui::Text("R: reload / Space: spawn / ESC: title");
    if (ImGui::Button("Reload JSON")) {
        ReloadParticleJson_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save HitEffect JSON")) {
        ParticleManager::GetInstance()->Save("hit_effect.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load HitEffect JSON")) {
        ParticleManager::GetInstance()->ClearGroups();
        ParticleManager::GetInstance()->Load("hit_effect.json");
        EnsureHitEffectGroup_();
    }

    ImGui::Separator();
    ImGui::InputText("HitEffect Group", hitEffectGroupName_, sizeof(hitEffectGroupName_));
    ImGui::DragInt("Spawn Count", &hitEffectSpawnCount_, 1, 1, 1024);
    ImGui::DragFloat3("Spawn Position", &hitEffectSpawnPosition_.x, 0.1f);
    if (ImGui::Button("Create / Reset HitEffect Preset")) {
        EnsureHitEffectGroup_();
        ParticleManager::GetInstance()->ConfigureHitEffectPreset(hitEffectGroupName_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Spawn HitEffect")) {
        SpawnHitEffectPreview_();
    }
    ImGui::End();

    if (editorParticle_) {
        editorParticle_->DebugImGui();
    }

    ParticleManager::GetInstance()->DrawImGui();
#endif
}
