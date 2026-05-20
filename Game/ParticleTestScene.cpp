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

    ParticleManager::GetInstance()->ClearGroups();
    ParticleManager::GetInstance()->Load(kParticleJson);
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
        ParticleManager::GetInstance()->ClearGroups();
        ParticleManager::GetInstance()->Load(kParticleJson);
        reloadCooldown_ = 0.2f;
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
        ground_->Draw();
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

void ParticleTestScene::DrawImGui(GameApp&)
{
#ifdef USE_IMGUI
    ImGui::Begin("Particle Test Scene");
    ImGui::Text("JSON: %s", kParticleJson);
    ImGui::Text("R: reload / ESC: title");
    if (ImGui::Button("Reload JSON")) {
        ParticleManager::GetInstance()->ClearGroups();
        ParticleManager::GetInstance()->Load(kParticleJson);
    }
    ImGui::End();

    if (editorParticle_) {
        editorParticle_->DebugImGui();
    }

    ParticleManager::GetInstance()->DrawImGui();
#endif
}
