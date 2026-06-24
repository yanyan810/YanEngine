#include "ParticleTestScene.h"
#include "ParticleTestSceneSupport.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"

#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gParticleTestEditorModeSwitcherVisible;
extern int gParticleTestEditorMode;
extern std::vector<std::string> gParticleTestBlenderHierarchyNames;
extern int gParticleTestBlenderHierarchySelected;
extern bool gParticleTestBlenderHierarchySelectionChanged;
extern bool gParticleTestAnimationCameraPreviewVisible;
extern bool gParticleTestAnimationCameraPreviewSwapped;
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <Windows.h>
#include <commdlg.h>

using json = nlohmann::json;
using namespace ParticleTestSceneSupport;
void ParticleTestScene::OnEnter(GameApp& app)
{
    if (auto* input = app.GetInput()) {
        input->SetCameraControlEnabled(false);
    }

    app.Render()->SetMode(PostEffectMode::FullScreen);

    TextureManager::GetInstance()->LoadTexture("resources/circle.png");
    TextureManager::GetInstance()->LoadTexture("resources/white1x1.png");

    editorCameraPosition_ = { 0.0f, 3.0f, -20.0f };
    editorCameraRotation_ = { 0.0f, 0.0f, 0.0f };

    camera_ = std::make_unique<Camera>();
    camera_->SetTranslate(editorCameraPosition_);
    camera_->SetRotate(editorCameraRotation_);
    camera_->Update();

    animationCamera_ = std::make_unique<Camera>();
    ApplyAnimationCamera_();
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

    playerAttackHitboxCube_ = std::make_unique<Object3d>();
    playerAttackHitboxCube_->Initialize(app.ObjCom(), app.Dx());
    playerAttackHitboxCube_->SetCamera(GetSceneCamera_());
    playerAttackHitboxCube_->SetModel("cube/cube.obj");
    playerAttackHitboxCube_->SetEnableLighting(0);
    playerAttackHitboxCube_->SetMaterialColor({ 0.1f, 1.0f, 0.25f, 0.35f });
    playerAttackHitboxCube_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    AddEditorObject_(app, editorModelPath_);

#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = true;
    gParticleTestEditorMode = static_cast<int>(editorMode_);
#endif
    lastTimelineTime_ = (timelineTime_ == 0.0f) ? -0.001f : timelineTime_;
    previousTimelineTime_ = timelineTime_;

    auto* pm = ParticleManager::GetInstance();
    if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
        pm->ClearGroups();
        for (auto& node : particleNodes_) {
            pm->LoadAdditional(node.particleFileName, "");
            node.hasEmitted = false;
        }
    } else {
        ReloadParticleJson_();
    }
    lastEditorMode_ = editorMode_;
}

void ParticleTestScene::OnExit(GameApp&)
{
    editorObjects_.clear();
    playerAttackHitboxCube_.reset();
    editorParticle_.reset();
    ground_.reset();
    animationCamera_.reset();
    camera_.reset();
#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = false;
    gParticleTestBlenderHierarchyNames.clear();
    gParticleTestBlenderHierarchySelected = -1;
    gParticleTestBlenderHierarchySelectionChanged = false;
    gParticleTestAnimationCameraPreviewVisible = false;
    gParticleTestAnimationCameraPreviewSwapped = false;
#endif
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

    if (pendingDeleteSelectedObject_) {
        pendingDeleteSelectedObject_ = false;
        DeleteSelectedObject_();
    }

    reloadCooldown_ = std::max(0.0f, reloadCooldown_ - dt);
    if (editorMode_ == EditorMode::Particle && input->IsKeyTrigger(DIK_R) && reloadCooldown_ <= 0.0f) {
        ReloadParticleJson_();
        reloadCooldown_ = 0.2f;
    }

    if (editorMode_ == EditorMode::Particle && input->IsKeyTrigger(DIK_SPACE)) {
        SpawnHitEffectPreview_();
    }

    // モード切り替え時のクリアと再ロード
    if (editorMode_ != lastEditorMode_) {
        auto* pm = ParticleManager::GetInstance();
        pm->ClearAllParticles();
        if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
            pm->ClearGroups();
            for (auto& node : particleNodes_) {
                pm->LoadAdditional(node.particleFileName, "");
                node.hasEmitted = false;
            }
        } else {
            ReloadParticleJson_();
        }
        lastEditorMode_ = editorMode_;
    }

    float particleDt = 0.0f;
    bool timeJumped = false;

    if (pendingTimelineRebuild_) {
        pendingTimelineRebuild_ = false;
        RebuildParticleTimeline_(pendingTimelineRebuildTime_);
        particleDt = 0.0f;
        timeJumped = true;
    }

    if (!timeJumped && timelinePlaying_) {
        float nextTime = timelineTime_ + dt;
        if (nextTime > timelineDuration_) {
            if (timelineLoop_ && timelineDuration_ > 0.0f) {
                timelineTime_ = std::fmod(nextTime, timelineDuration_);
                RebuildParticleTimeline_(timelineTime_);
                timeJumped = true;
            } else {
                timelineTime_ = timelineDuration_;
                timelinePlaying_ = false;
            }
        } else {
            timelineTime_ = nextTime;
        }
        if (timeJumped) {
            particleDt = 0.0f;
        } else {
            EvaluateTimeline_(true);
            particleDt = dt;
        }
    } else if (!timeJumped) {
        if (timelineTime_ != previousTimelineTime_) {
            RebuildParticleTimeline_(timelineTime_);
            particleDt = 0.0f;
            timeJumped = true;
        }
    }

    if (!timeJumped) {
        lastTimelineTime_ = (timelineTime_ == 0.0f) ? -0.001f : timelineTime_;
    }
    previousTimelineTime_ = timelineTime_;

    if (camera_) {
        camera_->Update();
    }
    ApplyAnimationCamera_();
    ApplyCameraToEditorObjects_();
    if (editorMode_ == EditorMode::Particle) {
        particleDt = dt;
    }
    if (GetSceneCamera_()) {
        ParticleManager::GetInstance()->Update(particleDt, *GetSceneCamera_());
    }

    if (ground_) {
        ground_->Update(dt);
    }

    for (auto& item : editorObjects_) {
        if (item.object) {
            if (!item.attachToBone) {
                ApplyEditorObjectTransform_(item);
            }
            item.object->Update(dt);
        }
    }

    for (auto& item : editorObjects_) {
        if (!item.object || !item.attachToBone || item.attachParentId == item.id || item.attachJointName.empty()) {
            continue;
        }

        Object3d* parentObject = nullptr;
        for (auto& parent : editorObjects_) {
            if (parent.id == item.attachParentId && parent.object) {
                parentObject = parent.object.get();
                break;
            }
        }

        if (parentObject && parentObject->AttachObjectToJoint(*item.object, item.attachJointName, item.attachOffset, item.attachRotation, item.attachScale)) {
            item.object->SetCamera(GetSceneCamera_());
            item.object->SetMaterialColor(item.color);
            item.object->SetBlendMode(item.blendMode);
            if (!item.texturePath.empty()) {
                item.object->SetTexture(item.texturePath);
            } else {
                item.object->ClearTextureOverride();
            }
            item.object->Update(dt);
        } else {
            ApplyEditorObjectTransform_(item);
            item.object->Update(dt);
        }
    }

    if (playerAttackHitboxCube_) {
        Vector3 playerBase{};
        if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
            playerBase = editorObjects_[playerAttackObjectIndex_].position;
        }
        playerAttackHitboxCube_->SetTranslate(playerBase + currentPlayerAttackHitbox_.offset);
        playerAttackHitboxCube_->SetScale(currentPlayerAttackHitbox_.halfSize);
        playerAttackHitboxCube_->Update(dt);
    }

    if (editorMode_ == EditorMode::Particle && editorParticle_) {
        editorParticle_->Update();
    }
}

void ParticleTestScene::DrawRender(GameApp& app)
{
    DrawSceneContent_(app);
}

void ParticleTestScene::DrawPreview(GameApp& app)
{
    if (!useAnimationCameraPreview_) {
        return;
    }

    Camera* previewCamera = GetPreviewCamera_();
    Camera* sceneCamera = GetSceneCamera_();
    if (!previewCamera) {
        return;
    }

    if (ground_) {
        ground_->SetCamera(previewCamera);
    }
    if (editorParticle_) {
        editorParticle_->SetCamera(previewCamera);
    }
    for (auto& item : editorObjects_) {
        if (item.object) {
            item.object->SetCamera(previewCamera);
        }
    }
    if (playerAttackHitboxCube_) {
        playerAttackHitboxCube_->SetCamera(previewCamera);
    }

    DrawSceneContent_(app);

    if (ground_) {
        ground_->SetCamera(sceneCamera);
    }
    if (editorParticle_) {
        editorParticle_->SetCamera(sceneCamera);
    }
    for (auto& item : editorObjects_) {
        if (item.object) {
            item.object->SetCamera(sceneCamera);
        }
    }
    if (playerAttackHitboxCube_) {
        playerAttackHitboxCube_->SetCamera(sceneCamera);
    }
}

void ParticleTestScene::DrawPostEffectTargets(GameApp& app)
{
    Vector4 layerBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 layerOutlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        const auto& selected = editorObjects_[selectedEditorObject_];
        if (selected.bloomPostEffect || selected.outlineBloomPostEffect) {
            layerBloomColor = selected.bloomColor;
            layerOutlineBloomColor = selected.outlineBloomColor;
        }
    } else {
        for (const auto& item : editorObjects_) {
            if (item.bloomPostEffect || item.outlineBloomPostEffect) {
                layerBloomColor = item.bloomColor;
                layerOutlineBloomColor = item.outlineBloomColor;
                break;
            }
        }
    }
    app.Render()->SetObjectLayerBloomColor(layerBloomColor);
    app.Render()->SetObjectLayerOutlineBloomColor(layerOutlineBloomColor);
    for (auto& item : editorObjects_) {
        if ((item.bloomPostEffect || item.outlineBloomPostEffect) && item.object) {
            item.object->Draw();
        }
    }
}

bool ParticleTestScene::HasObjectBloomTargets() const
{
    return std::any_of(editorObjects_.begin(), editorObjects_.end(), [](const EditorObject& item) {
        return item.bloomPostEffect && item.object;
    });
}

bool ParticleTestScene::HasObjectOutlineBloomTargets() const
{
    return std::any_of(editorObjects_.begin(), editorObjects_.end(), [](const EditorObject& item) {
        return item.outlineBloomPostEffect && item.object;
    });
}

void ParticleTestScene::DrawSceneContent_(GameApp& app)
{
   /* if (ground_) {
        ground_->Draw();
    }*/

    for (auto& item : editorObjects_) {
        if (item.object) {
            item.object->Draw();
        }
    }

    if (playerAttackEditorEnabled_ && drawPlayerAttackHitbox_ && currentPlayerAttackHitbox_.active && playerAttackHitboxCube_) {
        playerAttackHitboxCube_->Draw();
    }

    if (editorMode_ == EditorMode::Particle && editorParticle_) {
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
