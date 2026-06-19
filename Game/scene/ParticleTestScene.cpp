#include "ParticleTestScene.h"

#include "Camera.h"
#include "DirectXCommon.h"
#include "GameApp.h"
#include "GeometryGenerator.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "Particle.h"
#include "ParticleCommon.h"
#include "ParticleManager.h"
#include "RenderManager.h"
#include "TextureManager.h"

#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
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

namespace {
constexpr const char* kParticleJson = "test_particles.json";
constexpr size_t kMaxUndoCount = 64;
constexpr float kPi = 3.14159265358979323846f;
constexpr const char* kGeometryNames[] = {
    "Ring",
    "Sphere",
    "Box",
    "Plane",
    "Torus",
    "Cylinder",
    "Cone",
    "Triangle",
    "Capsule",
    "Star",
    "Diamond",
};
constexpr int kGeometryCount = static_cast<int>(sizeof(kGeometryNames) / sizeof(kGeometryNames[0]));
constexpr const char* kObjectBlendModeNames[] = {
    "None",
    "Normal",
    "Add",
    "Subtract",
    "Multiply",
    "Screen",
};

}

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

    AddEditorObject_(app, editorModelPath_);

#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = true;
    gParticleTestEditorMode = static_cast<int>(editorMode_);
#endif
    lastTimelineTime_ = (timelineTime_ == 0.0f) ? -0.001f : timelineTime_;
    previousTimelineTime_ = timelineTime_;

    auto* pm = ParticleManager::GetInstance();
    if (editorMode_ == EditorMode::Blender) {
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
        if (editorMode_ == EditorMode::Blender) {
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
            ApplyEditorObjectTransform_(item);
            item.object->Update(dt);
        }
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

namespace {
Vector3 LerpVector3(const Vector3& a, const Vector3& b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

Vector4 LerpVector4(const Vector4& a, const Vector4& b, float t)
{
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t,
    };
}

float LengthVector3(const Vector3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 NormalizeVector3(const Vector3& v)
{
    const float length = LengthVector3(v);
    if (length <= 0.0001f) {
        return { 0.0f, 0.0f, 0.0f };
    }
    return { v.x / length, v.y / length, v.z / length };
}

Vector3 CameraRight(const Matrix4x4& cameraWorld)
{
    return NormalizeVector3({ cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2] });
}

Vector3 CameraUp(const Matrix4x4& cameraWorld)
{
    return NormalizeVector3({ cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2] });
}

Vector3 CameraForward(const Matrix4x4& cameraWorld)
{
    return NormalizeVector3({ cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2] });
}

std::vector<Model::VertexData> MakeEditorGeometryVertices(int typeIndex)
{
    switch (typeIndex) {
    case 0: return GeometryGenerator::GenerateRingTriListXY(64, 1.0f, 0.5f);
    case 1: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    case 2: return GeometryGenerator::GenerateBoxTriList(2.0f, 2.0f, 2.0f);
    case 3: return GeometryGenerator::GeneratePlaneTriListXY(2.0f, 2.0f);
    case 4: return GeometryGenerator::GenerateTorusTriList(32, 16, 1.0f, 0.3f);
    case 5: return GeometryGenerator::GenerateCylinderTriList(32, 1.0f, 2.0f);
    case 6: return GeometryGenerator::GenerateConeTriList(32, 1.0f, 2.0f);
    case 7: return GeometryGenerator::GenerateTriangleTriListXY(2.0f, 2.0f);
    case 8: return GeometryGenerator::GenerateCapsuleTriList(32, 8, 0.65f, 1.4f);
    case 9: return GeometryGenerator::GenerateStarTriListXY(1.1f, 0.48f, 5);
    case 10: return GeometryGenerator::GenerateDiamondTriListXY(1.6f, 2.2f);
    default: return GeometryGenerator::GenerateSphereTriList(32, 16, 1.0f);
    }
}

Model::ModelData MakeEditorGeometryModelData(const std::vector<Model::VertexData>& vertices)
{
    Model::ModelData modelData{};
    modelData.materials.push_back({ "" });

    Model::MeshData mesh{};
    mesh.materialIndex = 0;
    mesh.vertices = vertices;
    mesh.skinned = false;
    mesh.startVertex = 0;
    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.startIndex = 0;
    mesh.indexCount = static_cast<uint32_t>(vertices.size());
    modelData.meshes.push_back(std::move(mesh));

    modelData.indices.resize(vertices.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
        modelData.indices[i] = i;
    }

    modelData.rootNode.name = "EditorGeometryRoot";
    modelData.rootNode.localMatrix = Matrix4x4::MakeIdentity4x4();
    modelData.rootNode.meshIndices.push_back(0);
    return modelData;
}

Model* GetOrCreateEditorGeometryModel(int typeIndex)
{
    typeIndex = std::clamp(typeIndex, 0, kGeometryCount - 1);
    const std::string key = "EffectEditorGeometry_" + std::to_string(typeIndex);
    if (Model* model = ModelManager::GetInstance()->FindModel(key)) {
        return model;
    }
    auto vertices = MakeEditorGeometryVertices(typeIndex);
    auto modelData = MakeEditorGeometryModelData(vertices);
    return ModelManager::GetInstance()->CreatePrimitiveModel(key, modelData);
}

std::string ToResourceRelativeModelPath(const std::filesystem::path& sourcePath)
{
    std::filesystem::path normalized = sourcePath.lexically_normal();
    std::filesystem::path relative = normalized;
    bool foundResources = false;
    for (auto it = normalized.begin(); it != normalized.end(); ++it) {
        if (it->string() == "resources") {
            relative.clear();
            ++it;
            for (; it != normalized.end(); ++it) {
                relative /= *it;
            }
            foundResources = true;
            break;
        }
    }

    if (!foundResources) {
        relative = normalized.filename();
    }

    return relative.generic_string();
}
}

#ifdef USE_IMGUI
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
extern bool gParticleTestEditorModeSwitcherVisible;
extern int gParticleTestEditorMode;
#endif

void ParticleTestScene::AddEditorObject_(GameApp& app, const std::string& modelPath)
{
    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "EffectObject_%02d", item.id);
    item.name = name;
    item.modelPath = modelPath.empty() ? "cube/cube.obj" : modelPath;
    item.geometryType = -1;
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetModel(item.modelPath);
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::AddGeometryObject_(GameApp& app, int geometryType)
{
    geometryType = std::clamp(geometryType, 0, kGeometryCount - 1);

    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "%s_%02d", kGeometryNames[geometryType], item.id);
    item.name = name;
    item.modelPath = std::string("geometry:") + kGeometryNames[geometryType];
    item.geometryType = geometryType;
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetModel(GetOrCreateEditorGeometryModel(geometryType));
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::PasteEditorObject_(GameApp& app)
{
    if (!hasCopiedObject_) {
        return;
    }

    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "%s_Copy_%02d", copiedObject_.name.c_str(), item.id);
    item.name = name;
    item.modelPath = copiedObject_.modelPath;
    item.geometryType = copiedObject_.geometryType;
    item.position = copiedObject_.position + Vector3{ 0.5f, 0.0f, 0.0f };
    item.rotation = copiedObject_.rotation;
    item.scale = copiedObject_.scale;
    item.color = copiedObject_.color;
    item.texturePath = copiedObject_.texturePath;
    item.blendMode = copiedObject_.blendMode;
    item.billboard = copiedObject_.billboard;
    item.bloomPostEffect = copiedObject_.bloomPostEffect;
    item.outlineBloomPostEffect = copiedObject_.outlineBloomPostEffect;
    item.bloomColor = copiedObject_.bloomColor;
    item.outlineBloomColor = copiedObject_.outlineBloomColor;
    item.showBones = copiedObject_.showBones;
    item.selectedBone = copiedObject_.selectedBone;
    item.bonePoses = copiedObject_.bonePoses;
    item.keyframes = copiedObject_.keyframes;
    for (auto& key : item.keyframes) {
        key.position += Vector3{ 0.5f, 0.0f, 0.0f };
    }
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(GetSceneCamera_());
    if (item.geometryType >= 0) {
        item.object->SetModel(GetOrCreateEditorGeometryModel(item.geometryType));
    } else {
        item.object->SetModel(item.modelPath);
    }
    item.object->SetEnableLighting(0);
    ApplyEditorObjectTransform_(item);
    editorObjects_.push_back(std::move(item));
    selectedEditorObject_ = static_cast<int>(editorObjects_.size()) - 1;
}

void ParticleTestScene::DuplicateSelectedObject_(GameApp& app)
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    const EditorObjectSnapshot src = CaptureSelectedObject_();
    if (src.geometryType >= 0) {
        AddGeometryObject_(app, src.geometryType);
    } else {
        AddEditorObject_(app, src.modelPath);
    }
    EditorObject& dst = editorObjects_.back();
    dst.position = src.position + Vector3{ 0.5f, 0.0f, 0.0f };
    dst.geometryType = src.geometryType;
    dst.modelPath = src.modelPath;
    if (dst.object) {
        if (dst.geometryType >= 0) {
            dst.object->SetModel(GetOrCreateEditorGeometryModel(dst.geometryType));
        } else {
            dst.object->SetModel(dst.modelPath);
        }
    }
    dst.rotation = src.rotation;
    dst.scale = src.scale;
    dst.color = src.color;
    dst.texturePath = src.texturePath;
    dst.blendMode = src.blendMode;
    dst.billboard = src.billboard;
    dst.bloomPostEffect = src.bloomPostEffect;
    dst.outlineBloomPostEffect = src.outlineBloomPostEffect;
    dst.bloomColor = src.bloomColor;
    dst.outlineBloomColor = src.outlineBloomColor;
    dst.showBones = src.showBones;
    dst.selectedBone = src.selectedBone;
    dst.bonePoses = src.bonePoses;
    dst.keyframes = src.keyframes;
    ApplyEditorObjectTransform_(dst);
}

ParticleTestScene::EditorObjectSnapshot ParticleTestScene::CaptureSelectedObject_() const
{
    EditorObjectSnapshot snapshot;
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return snapshot;
    }

    const EditorObject& item = editorObjects_[selectedEditorObject_];
    snapshot.id = item.id;
    snapshot.name = item.name;
    snapshot.modelPath = item.modelPath;
    snapshot.geometryType = item.geometryType;
    snapshot.position = item.position;
    snapshot.rotation = item.rotation;
    snapshot.scale = item.scale;
    snapshot.color = item.color;
    snapshot.texturePath = item.texturePath;
    snapshot.blendMode = item.blendMode;
    snapshot.billboard = item.billboard;
    snapshot.bloomPostEffect = item.bloomPostEffect;
    snapshot.outlineBloomPostEffect = item.outlineBloomPostEffect;
    snapshot.bloomColor = item.bloomColor;
    snapshot.outlineBloomColor = item.outlineBloomColor;
    snapshot.showBones = item.showBones;
    snapshot.selectedBone = item.selectedBone;
    snapshot.bonePoses = item.bonePoses;
    snapshot.keyframes = item.keyframes;
    return snapshot;
}

void ParticleTestScene::CopySelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }
    copiedObject_ = CaptureSelectedObject_();
    hasCopiedObject_ = true;
}

void ParticleTestScene::RequestDeleteSelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    if (!pendingDeleteSelectedObject_) {
        PushUndoSnapshot_();
    }

    pendingDeleteSelectedObject_ = true;
    activeViewportGizmoAxis_ = -1;
    transformDragActive_ = false;
    transformDragChanged_ = false;
}

void ParticleTestScene::DeleteSelectedObject_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    editorObjects_.erase(editorObjects_.begin() + selectedEditorObject_);
    if (editorObjects_.empty()) {
        selectedEditorObject_ = -1;
    } else {
        selectedEditorObject_ = std::min(selectedEditorObject_, static_cast<int>(editorObjects_.size()) - 1);
    }
}

void ParticleTestScene::ApplyEditorObjectTransform_(EditorObject& item)
{
    if (!item.object) {
        return;
    }
    item.object->SetCamera(GetSceneCamera_());
    item.object->SetTranslate(item.position);
    if (item.billboard && GetSceneCamera_()) {
        item.object->SetRotate(GetSceneCamera_()->GetRotate());
    } else {
        item.object->SetRotate(item.rotation);
    }
    item.object->SetScale(item.scale);
    item.object->SetMaterialColor(item.color);
    item.object->SetBlendMode(item.blendMode);
    if (!item.texturePath.empty()) {
        item.object->SetTexture(item.texturePath);
    } else {
        item.object->ClearTextureOverride();
    }
    ApplyEditorObjectBonePose_(item);
}

Camera* ParticleTestScene::GetSceneCamera_() const
{
    if (useAnimationCameraPreview_ && animationCameraPreviewSwapped_ && animationCamera_) {
        return animationCamera_.get();
    }
    return camera_.get();
}

Camera* ParticleTestScene::GetPreviewCamera_() const
{
    if (animationCameraPreviewSwapped_ && camera_) {
        return camera_.get();
    }
    if (animationCamera_) {
        return animationCamera_.get();
    }
    return camera_.get();
}

void ParticleTestScene::ApplyCameraToEditorObjects_()
{
    Camera* sceneCamera = GetSceneCamera_();
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
}

void ParticleTestScene::ApplyAnimationCamera_()
{
    if (!animationCamera_) {
        return;
    }
    animationCamera_->SetTranslate(animationCameraPosition_);
    animationCamera_->SetRotate(animationCameraRotation_);
    animationCamera_->SetFovY(animationCameraFovY_);
    animationCamera_->Update();
}

void ParticleTestScene::SyncEditorObjectBones_(EditorObject& item)
{
    if (!item.object || !item.object->HasSkinningModel()) {
        item.selectedBone = 0;
        item.bonePoses.clear();
        return;
    }

    const Model::Skeleton* skeleton = item.object->GetSkeleton();
    if (!skeleton) {
        item.selectedBone = 0;
        item.bonePoses.clear();
        return;
    }

    std::vector<EditorBonePose> next;
    next.reserve(skeleton->joints.size());
    for (const auto& joint : skeleton->joints) {
        EditorBonePose pose;
        pose.name = joint.name;
        auto it = std::find_if(item.bonePoses.begin(), item.bonePoses.end(), [&](const EditorBonePose& current) {
            return current.name == joint.name;
        });
        if (it != item.bonePoses.end()) {
            pose.translate = it->translate;
            pose.rotate = it->rotate;
            pose.scale = it->scale;
        }
        next.push_back(std::move(pose));
    }

    item.bonePoses = std::move(next);
    if (item.selectedBone >= static_cast<int>(item.bonePoses.size())) {
        item.selectedBone = item.bonePoses.empty() ? 0 : static_cast<int>(item.bonePoses.size()) - 1;
    }
}

void ParticleTestScene::ApplyEditorObjectBonePose_(EditorObject& item)
{
    if (!item.object || !item.object->HasSkinningModel()) {
        return;
    }

    SyncEditorObjectBones_(item);
    item.object->ResetManualJointTransforms();
    for (int i = 0; i < static_cast<int>(item.bonePoses.size()); ++i) {
        const EditorBonePose& pose = item.bonePoses[i];
        item.object->SetManualJointTransform(i, pose.translate, pose.rotate, pose.scale);
    }
}

void ParticleTestScene::SortKeyframes_(EditorObject& item)
{
    std::sort(item.keyframes.begin(), item.keyframes.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) {
        return a.time < b.time;
    });
}

void ParticleTestScene::SortCameraKeyframes_()
{
    std::sort(cameraKeyframes_.begin(), cameraKeyframes_.end(), [](const CameraKeyframe& a, const CameraKeyframe& b) {
        return a.time < b.time;
    });
}

void ParticleTestScene::AddCameraKeyframe_()
{
    for (auto& key : cameraKeyframes_) {
        if (std::abs(key.time - timelineTime_) < 0.001f) {
            key.position = animationCameraPosition_;
            key.rotation = animationCameraRotation_;
            key.fovY = animationCameraFovY_;
            return;
        }
    }

    cameraKeyframes_.push_back({ timelineTime_, animationCameraPosition_, animationCameraRotation_, animationCameraFovY_ });
    SortCameraKeyframes_();
}

void ParticleTestScene::DeleteNearestCameraKeyframe_()
{
    if (cameraKeyframes_.empty()) {
        return;
    }

    auto it = std::min_element(cameraKeyframes_.begin(), cameraKeyframes_.end(), [this](const CameraKeyframe& a, const CameraKeyframe& b) {
        return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
    });
    if (it != cameraKeyframes_.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
        cameraKeyframes_.erase(it);
    }
}

void ParticleTestScene::AddKeyframeToSelected_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    EditorObject& item = editorObjects_[selectedEditorObject_];
    for (auto& key : item.keyframes) {
        if (std::abs(key.time - timelineTime_) < 0.001f) {
            key.position = item.position;
            key.rotation = item.rotation;
            key.scale = item.scale;
            key.color = item.color;
            key.bloomPostEffect = item.bloomPostEffect;
            key.outlineBloomPostEffect = item.outlineBloomPostEffect;
            key.bloomColor = item.bloomColor;
            key.outlineBloomColor = item.outlineBloomColor;
            return;
        }
    }

    item.keyframes.push_back({
        timelineTime_,
        item.position,
        item.rotation,
        item.scale,
        item.color,
        item.bloomPostEffect,
        item.outlineBloomPostEffect,
        item.bloomColor,
        item.outlineBloomColor
    });
    SortKeyframes_(item);
}

void ParticleTestScene::DeleteNearestKeyframeFromSelected_()
{
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    auto& keys = editorObjects_[selectedEditorObject_].keyframes;
    if (keys.empty()) {
        return;
    }

    auto it = std::min_element(keys.begin(), keys.end(), [this](const EffectKeyframe& a, const EffectKeyframe& b) {
        return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
    });
    if (it != keys.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
        keys.erase(it);
    }
}

void ParticleTestScene::EvaluateTimeline_(bool emitParticles)
{
    if (!cameraKeyframes_.empty()) {
        SortCameraKeyframes_();
        if (timelineTime_ <= cameraKeyframes_.front().time) {
            animationCameraPosition_ = cameraKeyframes_.front().position;
            animationCameraRotation_ = cameraKeyframes_.front().rotation;
            animationCameraFovY_ = cameraKeyframes_.front().fovY;
        } else if (timelineTime_ >= cameraKeyframes_.back().time) {
            animationCameraPosition_ = cameraKeyframes_.back().position;
            animationCameraRotation_ = cameraKeyframes_.back().rotation;
            animationCameraFovY_ = cameraKeyframes_.back().fovY;
        } else {
            for (size_t i = 0; i + 1 < cameraKeyframes_.size(); ++i) {
                const auto& a = cameraKeyframes_[i];
                const auto& b = cameraKeyframes_[i + 1];
                if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
                    const float range = std::max(0.0001f, b.time - a.time);
                    const float t = (timelineTime_ - a.time) / range;
                    animationCameraPosition_ = LerpVector3(a.position, b.position, t);
                    animationCameraRotation_ = LerpVector3(a.rotation, b.rotation, t);
                    animationCameraFovY_ = a.fovY + (b.fovY - a.fovY) * t;
                    break;
                }
            }
        }
        ApplyAnimationCamera_();
    }

    for (auto& item : editorObjects_) {
        if (item.keyframes.empty()) {
            ApplyEditorObjectTransform_(item);
            continue;
        }

        SortKeyframes_(item);
        if (timelineTime_ <= item.keyframes.front().time) {
            item.position = item.keyframes.front().position;
            item.rotation = item.keyframes.front().rotation;
            item.scale = item.keyframes.front().scale;
            item.color = item.keyframes.front().color;
            item.bloomPostEffect = item.keyframes.front().bloomPostEffect;
            item.outlineBloomPostEffect = item.keyframes.front().outlineBloomPostEffect;
            item.bloomColor = item.keyframes.front().bloomColor;
            item.outlineBloomColor = item.keyframes.front().outlineBloomColor;
            ApplyEditorObjectTransform_(item);
            continue;
        }
        if (timelineTime_ >= item.keyframes.back().time) {
            item.position = item.keyframes.back().position;
            item.rotation = item.keyframes.back().rotation;
            item.scale = item.keyframes.back().scale;
            item.color = item.keyframes.back().color;
            item.bloomPostEffect = item.keyframes.back().bloomPostEffect;
            item.outlineBloomPostEffect = item.keyframes.back().outlineBloomPostEffect;
            item.bloomColor = item.keyframes.back().bloomColor;
            item.outlineBloomColor = item.keyframes.back().outlineBloomColor;
            ApplyEditorObjectTransform_(item);
            continue;
        }

        for (size_t i = 0; i + 1 < item.keyframes.size(); ++i) {
            const auto& a = item.keyframes[i];
            const auto& b = item.keyframes[i + 1];
            if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
                const float range = std::max(0.001f, b.time - a.time);
                const float t = (timelineTime_ - a.time) / range;
                item.position = LerpVector3(a.position, b.position, t);
                item.rotation = LerpVector3(a.rotation, b.rotation, t);
                item.scale = LerpVector3(a.scale, b.scale, t);
                item.color = LerpVector4(a.color, b.color, t);
                item.bloomPostEffect = a.bloomPostEffect;
                item.outlineBloomPostEffect = a.outlineBloomPostEffect;
                item.bloomColor = LerpVector4(a.bloomColor, b.bloomColor, t);
                item.outlineBloomColor = LerpVector4(a.outlineBloomColor, b.outlineBloomColor, t);
                ApplyEditorObjectTransform_(item);
                break;
            }
        }
    }

    if (!emitParticles) {
        return;
    }

    for (auto& node : particleNodes_) {
        const float nodeEndTime = node.startTime + GetParticleNodeDuration_(node);
        node.endTime = nodeEndTime;
        if (timelineTime_ < node.startTime) {
            node.hasEmitted = false;
            continue;
        }
        if (timelineTime_ > nodeEndTime) {
            node.hasEmitted = true;
            continue;
        }

        bool crossed = (lastTimelineTime_ < node.startTime && timelineTime_ >= node.startTime);

        if (crossed && !node.hasEmitted) {
            EmitParticleNode_(node, 0.0f);
            node.hasEmitted = true;
        }
    }
}

float ParticleTestScene::GetParticleNodeDuration_(const ParticleNode& node) const
{
    return std::max(0.01f, node.presetDuration);
}

void ParticleTestScene::EmitParticleNode_(const ParticleNode& node, float initialAge)
{
    auto* pm = ParticleManager::GetInstance();
    std::vector<std::string> groupNames = pm->GetGroupNamesInFile(node.particleFileName);
    for (const auto& groupName : groupNames) {
        if (pm->HasGroup(groupName)) {
            pm->EmitConfigured(groupName, node.position, 1.0f, initialAge);
        }
    }
}

void ParticleTestScene::RequestTimelineRebuild_(float targetTime)
{
    targetTime = std::clamp(targetTime, 0.0f, timelineDuration_);
    timelineTime_ = targetTime;
    pendingTimelineRebuildTime_ = targetTime;
    pendingTimelineRebuild_ = true;
    EvaluateTimeline_(false);
}

void ParticleTestScene::RebuildParticleTimeline_(float targetTime)
{
    targetTime = std::clamp(targetTime, 0.0f, timelineDuration_);
    timelineTime_ = targetTime;

    EvaluateTimeline_(false);

    auto* pm = ParticleManager::GetInstance();
    pm->ClearAllParticles();
    for (auto& node : particleNodes_) {
        const float duration = GetParticleNodeDuration_(node);
        node.endTime = node.startTime + duration;
        node.hasEmitted = false;

        if (targetTime < node.startTime || targetTime > node.endTime) {
            node.hasEmitted = targetTime > node.endTime;
            continue;
        }

        EmitParticleNode_(node, targetTime - node.startTime);
        node.hasEmitted = true;
    }

    lastTimelineTime_ = (targetTime == 0.0f) ? -0.001f : targetTime;
    previousTimelineTime_ = targetTime;
}

ParticleTestScene::EditorSnapshot ParticleTestScene::CaptureEditorSnapshot_() const
{
    EditorSnapshot snapshot;
    snapshot.selectedObject = selectedEditorObject_;
    snapshot.nextObjectId = nextEditorObjectId_;
    snapshot.timelineTime = timelineTime_;
    snapshot.timelineDuration = timelineDuration_;
    snapshot.timelineLoop = timelineLoop_;
    snapshot.animationCameraPosition = animationCameraPosition_;
    snapshot.animationCameraRotation = animationCameraRotation_;
    snapshot.animationCameraFovY = animationCameraFovY_;
    snapshot.useAnimationCameraPreview = useAnimationCameraPreview_;
    snapshot.animationCameraPreviewSwapped = animationCameraPreviewSwapped_;
    snapshot.cameraKeyframes = cameraKeyframes_;
    snapshot.particleNodes = particleNodes_;
    snapshot.selectedParticleNode = selectedParticleNode_;
    snapshot.objects.reserve(editorObjects_.size());
    for (const auto& item : editorObjects_) {
        EditorObjectSnapshot object;
        object.id = item.id;
        object.name = item.name;
        object.modelPath = item.modelPath;
        object.geometryType = item.geometryType;
        object.position = item.position;
        object.rotation = item.rotation;
        object.scale = item.scale;
        object.color = item.color;
        object.texturePath = item.texturePath;
        object.blendMode = item.blendMode;
        object.billboard = item.billboard;
        object.bloomPostEffect = item.bloomPostEffect;
        object.outlineBloomPostEffect = item.outlineBloomPostEffect;
        object.bloomColor = item.bloomColor;
        object.outlineBloomColor = item.outlineBloomColor;
        object.showBones = item.showBones;
        object.selectedBone = item.selectedBone;
        object.bonePoses = item.bonePoses;
        object.keyframes = item.keyframes;
        snapshot.objects.push_back(std::move(object));
    }
    return snapshot;
}

void ParticleTestScene::RestoreEditorSnapshot_(GameApp& app, const EditorSnapshot& snapshot)
{
    editorObjects_.clear();
    editorObjects_.reserve(snapshot.objects.size());
    for (const auto& src : snapshot.objects) {
        EditorObject item;
        item.id = src.id;
        item.name = src.name;
        item.modelPath = src.modelPath;
        item.geometryType = src.geometryType;
        item.position = src.position;
        item.rotation = src.rotation;
        item.scale = src.scale;
        item.color = src.color;
        item.texturePath = src.texturePath;
        item.blendMode = src.blendMode;
        item.billboard = src.billboard;
        item.bloomPostEffect = src.bloomPostEffect;
        item.outlineBloomPostEffect = src.outlineBloomPostEffect;
        item.bloomColor = src.bloomColor;
        item.outlineBloomColor = src.outlineBloomColor;
        item.showBones = src.showBones;
        item.selectedBone = src.selectedBone;
        item.bonePoses = src.bonePoses;
        item.keyframes = src.keyframes;
        item.object = std::make_unique<Object3d>();
        item.object->Initialize(app.ObjCom(), app.Dx());
        item.object->SetCamera(GetSceneCamera_());
        if (item.geometryType >= 0) {
            item.object->SetModel(GetOrCreateEditorGeometryModel(item.geometryType));
        } else {
            item.object->SetModel(item.modelPath);
        }
        item.object->SetEnableLighting(0);
        ApplyEditorObjectTransform_(item);
        editorObjects_.push_back(std::move(item));
    }
    selectedEditorObject_ = snapshot.selectedObject;
    if (selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        selectedEditorObject_ = editorObjects_.empty() ? -1 : static_cast<int>(editorObjects_.size()) - 1;
    }
    nextEditorObjectId_ = snapshot.nextObjectId;
    timelineTime_ = snapshot.timelineTime;
    timelineDuration_ = std::max(0.05f, snapshot.timelineDuration);
    timelineLoop_ = snapshot.timelineLoop;
    animationCameraPosition_ = snapshot.animationCameraPosition;
    animationCameraRotation_ = snapshot.animationCameraRotation;
    animationCameraFovY_ = snapshot.animationCameraFovY;
    useAnimationCameraPreview_ = snapshot.useAnimationCameraPreview;
    animationCameraPreviewSwapped_ = snapshot.animationCameraPreviewSwapped;
    cameraKeyframes_ = snapshot.cameraKeyframes;
    particleNodes_ = snapshot.particleNodes;
    selectedParticleNode_ = snapshot.selectedParticleNode;

    // Blenderモード中のスナップショット復元の場合は、パーティクルグループも同期する
    if (editorMode_ == EditorMode::Blender) {
        auto* pm = ParticleManager::GetInstance();
        pm->ClearGroups();
        for (auto& node : particleNodes_) {
            pm->LoadAdditional(node.particleFileName, "");
            node.hasEmitted = false;
        }
    }

    ApplyAnimationCamera_();
    ApplyCameraToEditorObjects_();
    EvaluateTimeline_();
}

void ParticleTestScene::PushUndoSnapshot_(const EditorSnapshot& snapshot)
{
    undoStack_.push_back(snapshot);
    while (undoStack_.size() > kMaxUndoCount) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
}

void ParticleTestScene::PushUndoSnapshot_()
{
    PushUndoSnapshot_(CaptureEditorSnapshot_());
}

void ParticleTestScene::Undo_(GameApp& app)
{
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(CaptureEditorSnapshot_());
    EditorSnapshot snapshot = undoStack_.back();
    undoStack_.pop_back();
    RestoreEditorSnapshot_(app, snapshot);
}

void ParticleTestScene::Redo_(GameApp& app)
{
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(CaptureEditorSnapshot_());
    EditorSnapshot snapshot = redoStack_.back();
    redoStack_.pop_back();
    RestoreEditorSnapshot_(app, snapshot);
}

void ParticleTestScene::SaveEffectJson_(const std::string& path) const
{
    json root;
    root["timeline"] = {
        { "duration", timelineDuration_ },
        { "loop", timelineLoop_ }
    };
    root["animationCamera"] = {
        { "position", { animationCameraPosition_.x, animationCameraPosition_.y, animationCameraPosition_.z } },
        { "rotation", { animationCameraRotation_.x, animationCameraRotation_.y, animationCameraRotation_.z } },
        { "fovY", animationCameraFovY_ },
        { "preview", useAnimationCameraPreview_ },
        { "previewSwapped", animationCameraPreviewSwapped_ },
        { "keyframes", json::array() }
    };
    for (const auto& key : cameraKeyframes_) {
        root["animationCamera"]["keyframes"].push_back({
            { "time", key.time },
            { "position", { key.position.x, key.position.y, key.position.z } },
            { "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
            { "fovY", key.fovY }
        });
    }
    root["objects"] = json::array();
    for (const auto& item : editorObjects_) {
        json object;
        object["id"] = item.id;
        object["name"] = item.name;
        object["modelPath"] = item.modelPath;
        object["texturePath"] = item.texturePath;
        object["geometryType"] = item.geometryType;
        object["position"] = { item.position.x, item.position.y, item.position.z };
        object["rotation"] = { item.rotation.x, item.rotation.y, item.rotation.z };
        object["scale"] = { item.scale.x, item.scale.y, item.scale.z };
        object["color"] = { item.color.x, item.color.y, item.color.z, item.color.w };
        object["blendMode"] = static_cast<int>(item.blendMode);
        object["billboard"] = item.billboard;
        object["bloomPostEffect"] = item.bloomPostEffect;
        object["outlineBloomPostEffect"] = item.outlineBloomPostEffect;
        object["bloomColor"] = { item.bloomColor.x, item.bloomColor.y, item.bloomColor.z, item.bloomColor.w };
        object["outlineBloomColor"] = { item.outlineBloomColor.x, item.outlineBloomColor.y, item.outlineBloomColor.z, item.outlineBloomColor.w };
        object["showBones"] = item.showBones;
        object["selectedBone"] = item.selectedBone;
        object["bonePoses"] = json::array();
        for (const auto& pose : item.bonePoses) {
            object["bonePoses"].push_back({
                { "name", pose.name },
                { "translate", { pose.translate.x, pose.translate.y, pose.translate.z } },
                { "rotate", { pose.rotate.x, pose.rotate.y, pose.rotate.z } },
                { "scale", { pose.scale.x, pose.scale.y, pose.scale.z } }
            });
        }
        object["keyframes"] = json::array();
        for (const auto& key : item.keyframes) {
            object["keyframes"].push_back({
                { "time", key.time },
                { "position", { key.position.x, key.position.y, key.position.z } },
                { "rotation", { key.rotation.x, key.rotation.y, key.rotation.z } },
                { "scale", { key.scale.x, key.scale.y, key.scale.z } },
                { "color", { key.color.x, key.color.y, key.color.z, key.color.w } },
                { "bloomPostEffect", key.bloomPostEffect },
                { "outlineBloomPostEffect", key.outlineBloomPostEffect },
                { "bloomColor", { key.bloomColor.x, key.bloomColor.y, key.bloomColor.z, key.bloomColor.w } },
                { "outlineBloomColor", { key.outlineBloomColor.x, key.outlineBloomColor.y, key.outlineBloomColor.z, key.outlineBloomColor.w } }
            });
        }
        root["objects"].push_back(std::move(object));
    }

    root["particleNodes"] = json::array();
    for (const auto& node : particleNodes_) {
        json jNode;
        jNode["name"] = node.name;
        jNode["particleFileName"] = node.particleFileName;
        jNode["startTime"] = node.startTime;
        jNode["endTime"] = node.endTime;
        jNode["position"] = { node.position.x, node.position.y, node.position.z };
        jNode["rotation"] = { node.rotation.x, node.rotation.y, node.rotation.z };
        jNode["scale"] = { node.scale.x, node.scale.y, node.scale.z };
        jNode["emitCount"] = node.emitCount;
        jNode["presetDuration"] = node.presetDuration;
        root["particleNodes"].push_back(std::move(jNode));
    }

    std::filesystem::path outputPath(path);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream file(path);
    if (file.is_open()) {
        file << root.dump(4);
    }
}

void ParticleTestScene::LoadEffectJson_(GameApp& app, const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    json root;
    file >> root;

    EditorSnapshot snapshot;
    snapshot.timelineDuration = root.value("timeline", json::object()).value("duration", 1.0f);
    snapshot.timelineLoop = root.value("timeline", json::object()).value("loop", true);
    snapshot.timelineTime = 0.0f;
    snapshot.selectedObject = -1;
    snapshot.nextObjectId = 1;
    const json cameraJson = root.value("animationCamera", json::object());
    auto cp = cameraJson.value("position", json::array({ 0.0f, 3.0f, -12.0f }));
    auto cr = cameraJson.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
    snapshot.animationCameraPosition = { cp[0], cp[1], cp[2] };
    snapshot.animationCameraRotation = { cr[0], cr[1], cr[2] };
    snapshot.animationCameraFovY = cameraJson.value("fovY", 0.45f);
    snapshot.useAnimationCameraPreview = cameraJson.value("preview", false);
    snapshot.animationCameraPreviewSwapped = cameraJson.value("previewSwapped", false);
    for (const auto& keySource : cameraJson.value("keyframes", json::array())) {
        CameraKeyframe key;
        key.time = keySource.value("time", 0.0f);
        auto kp = keySource.value("position", json::array({ snapshot.animationCameraPosition.x, snapshot.animationCameraPosition.y, snapshot.animationCameraPosition.z }));
        auto kr = keySource.value("rotation", json::array({ snapshot.animationCameraRotation.x, snapshot.animationCameraRotation.y, snapshot.animationCameraRotation.z }));
        key.position = { kp[0], kp[1], kp[2] };
        key.rotation = { kr[0], kr[1], kr[2] };
        key.fovY = keySource.value("fovY", snapshot.animationCameraFovY);
        snapshot.cameraKeyframes.push_back(key);
    }

    for (const auto& source : root.value("objects", json::array())) {
        EditorObjectSnapshot object;
        object.id = source.value("id", snapshot.nextObjectId);
        object.name = source.value("name", std::string("EffectObject"));
        object.modelPath = source.value("modelPath", std::string("cube/cube.obj"));
        object.texturePath = source.value("texturePath", std::string{});
        object.geometryType = source.value("geometryType", -1);
        auto p = source.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = source.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = source.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        auto c = source.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }));
        object.position = { p[0], p[1], p[2] };
        object.rotation = { r[0], r[1], r[2] };
        object.scale = { s[0], s[1], s[2] };
        object.color = { c[0], c[1], c[2], c[3] };
        object.blendMode = static_cast<Object3dCommon::BlendMode>(std::clamp(
            source.value("blendMode", static_cast<int>(Object3dCommon::BlendMode::kBlendModeNormal)),
            0,
            static_cast<int>(Object3dCommon::BlendMode::kCountOfBlendMode) - 1));
        object.billboard = source.value("billboard", false);
        object.bloomPostEffect = source.value("bloomPostEffect", false);
        object.outlineBloomPostEffect = source.value("outlineBloomPostEffect", false);
        auto bc = source.value("bloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
        object.bloomColor = { bc[0], bc[1], bc[2], bc[3] };
        auto obc = source.value("outlineBloomColor", json::array({ 1.0f, 0.72f, 0.22f, 1.0f }));
        object.outlineBloomColor = { obc[0], obc[1], obc[2], obc[3] };
        object.showBones = source.value("showBones", false);
        object.selectedBone = source.value("selectedBone", 0);

        for (const auto& poseSource : source.value("bonePoses", json::array())) {
            EditorBonePose pose;
            pose.name = poseSource.value("name", std::string{});
            auto bt = poseSource.value("translate", json::array({ 0.0f, 0.0f, 0.0f }));
            auto br = poseSource.value("rotate", json::array({ 0.0f, 0.0f, 0.0f }));
            auto bs = poseSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
            pose.translate = { bt[0], bt[1], bt[2] };
            pose.rotate = { br[0], br[1], br[2] };
            pose.scale = { bs[0], bs[1], bs[2] };
            object.bonePoses.push_back(std::move(pose));
        }

        for (const auto& keySource : source.value("keyframes", json::array())) {
            EffectKeyframe key;
            key.time = keySource.value("time", 0.0f);
            auto kp = keySource.value("position", json::array({ object.position.x, object.position.y, object.position.z }));
            auto kr = keySource.value("rotation", json::array({ object.rotation.x, object.rotation.y, object.rotation.z }));
            auto ks = keySource.value("scale", json::array({ object.scale.x, object.scale.y, object.scale.z }));
            auto kc = keySource.value("color", json::array({ object.color.x, object.color.y, object.color.z, object.color.w }));
            key.position = { kp[0], kp[1], kp[2] };
            key.rotation = { kr[0], kr[1], kr[2] };
            key.scale = { ks[0], ks[1], ks[2] };
            key.color = { kc[0], kc[1], kc[2], kc[3] };
            key.bloomPostEffect = keySource.value("bloomPostEffect", object.bloomPostEffect);
            key.outlineBloomPostEffect = keySource.value("outlineBloomPostEffect", object.outlineBloomPostEffect);
            auto kbc = keySource.value("bloomColor", json::array({ object.bloomColor.x, object.bloomColor.y, object.bloomColor.z, object.bloomColor.w }));
            key.bloomColor = { kbc[0], kbc[1], kbc[2], kbc[3] };
            auto kobc = keySource.value("outlineBloomColor", json::array({ object.outlineBloomColor.x, object.outlineBloomColor.y, object.outlineBloomColor.z, object.outlineBloomColor.w }));
            key.outlineBloomColor = { kobc[0], kobc[1], kobc[2], kobc[3] };
            object.keyframes.push_back(key);
        }

        snapshot.nextObjectId = std::max(snapshot.nextObjectId, object.id + 1);
        snapshot.objects.push_back(std::move(object));
    }

    for (const auto& nodeSource : root.value("particleNodes", json::array())) {
        ParticleNode node;
        node.name = nodeSource.value("name", "ParticleNode");
        if (nodeSource.contains("particleFileName")) {
            node.particleFileName = nodeSource.value("particleFileName", "");
        } else {
            node.particleFileName = nodeSource.value("particleGroup", "");
        }
        node.startTime = nodeSource.value("startTime", 0.0f);
        node.endTime = nodeSource.value("endTime", 1.0f);
        auto p = nodeSource.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = nodeSource.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = nodeSource.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        node.position = { p[0], p[1], p[2] };
        node.rotation = { r[0], r[1], r[2] };
        node.scale = { s[0], s[1], s[2] };
        node.emitCount = nodeSource.value("emitCount", 10);
        node.presetDuration = nodeSource.value("presetDuration", 1.0f);
        node.hasEmitted = false;
        snapshot.particleNodes.push_back(std::move(node));
    }

    if (!snapshot.objects.empty()) {
        snapshot.selectedObject = 0;
    }
    RestoreEditorSnapshot_(app, snapshot);
    undoStack_.clear();
    redoStack_.clear();
}

bool ParticleTestScene::OpenModelFileDialog_(std::string& outModelPath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Model Files (*.obj;*.gltf;*.glb;*.fbx)\0*.obj;*.gltf;*.glb;*.fbx\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outModelPath = ToResourceRelativeModelPath(std::filesystem::path(filePath));
    return true;
}

bool ParticleTestScene::OpenModelFileDialog_()
{
    std::string path;
    if (OpenModelFileDialog_(path)) {
        strncpy_s(editorModelPath_, sizeof(editorModelPath_), path.c_str(), _TRUNCATE);
        return true;
    }
    return false;
}

bool ParticleTestScene::OpenTextureFileDialog_(std::string& outTexturePath)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Texture Files (*.png;*.jpg;*.jpeg;*.dds;*.tga)\0*.png;*.jpg;*.jpeg;*.dds;*.tga\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    openFileName.lpstrInitialDir = "resources";
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    outTexturePath = ToResourceRelativeModelPath(std::filesystem::path(filePath));
    return true;
}

void ParticleTestScene::DrawGizmoControls_(EditorObject& item)
{
#ifdef USE_IMGUI
    ImGui::Separator();
    ImGui::TextUnformatted("Gizmo");
    int mode = static_cast<int>(gizmoMode_);
    if (ImGui::RadioButton("Translate", mode == 0)) {
        gizmoMode_ = GizmoMode::Translate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", mode == 1)) {
        gizmoMode_ = GizmoMode::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", mode == 2)) {
        gizmoMode_ = GizmoMode::Scale;
    }

    auto dragAxis = [&](const char* label, float* value, float speed, float minValue, float maxValue, ImVec4 color) -> bool {
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        bool changed = ImGui::DragFloat(label, value, speed, minValue, maxValue, "%.3f");
        ImGui::PopStyleColor();
        return changed;
    };

    bool changed = false;
    auto trackGizmoDrag = [&](bool itemChanged) {
        if (ImGui::IsItemActivated() && !transformDragActive_) {
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
        if (itemChanged) {
            transformDragChanged_ = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
            if (transformDragChanged_) {
                PushUndoSnapshot_(transformDragBefore_);
            }
            transformDragActive_ = false;
            transformDragChanged_ = false;
        }
    };

    if (gizmoMode_ == GizmoMode::Translate) {
        bool x = dragAxis("X Position", &item.position.x, 0.05f, -100.0f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
        bool y = dragAxis("Y Position", &item.position.y, 0.05f, -100.0f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
        bool z = dragAxis("Z Position", &item.position.z, 0.05f, -100.0f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
    } else if (gizmoMode_ == GizmoMode::Rotate) {
        bool x = dragAxis("X Rotation", &item.rotation.x, 0.01f, -100.0f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
        bool y = dragAxis("Y Rotation", &item.rotation.y, 0.01f, -100.0f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
        bool z = dragAxis("Z Rotation", &item.rotation.z, 0.01f, -100.0f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
    } else {
        bool x = dragAxis("X Scale", &item.scale.x, 0.05f, 0.01f, 100.0f, { 1.0f, 0.25f, 0.25f, 1.0f }); changed |= x; trackGizmoDrag(x);
        bool y = dragAxis("Y Scale", &item.scale.y, 0.05f, 0.01f, 100.0f, { 0.35f, 1.0f, 0.35f, 1.0f }); changed |= y; trackGizmoDrag(y);
        bool z = dragAxis("Z Scale", &item.scale.z, 0.05f, 0.01f, 100.0f, { 0.35f, 0.55f, 1.0f, 1.0f }); changed |= z; trackGizmoDrag(z);
    }
    if (changed) {
        ApplyEditorObjectTransform_(item);
    }
#endif
}

void ParticleTestScene::DrawBoneControls_(EditorObject& item)
{
#ifdef USE_IMGUI
    if (!item.object || !item.object->HasSkinningModel()) {
        return;
    }

    SyncEditorObjectBones_(item);
    if (item.bonePoses.empty()) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Bone Controls");
    ImGui::Checkbox("Show Bones", &item.showBones);
    item.object->SetDebugDrawBones(false);
    ImGui::Text("Bones: %d", static_cast<int>(item.bonePoses.size()));

    item.selectedBone = std::clamp(item.selectedBone, 0, static_cast<int>(item.bonePoses.size()) - 1);
    const char* previewName = item.bonePoses[item.selectedBone].name.c_str();
    if (ImGui::BeginCombo("Bone", previewName)) {
        for (int i = 0; i < static_cast<int>(item.bonePoses.size()); ++i) {
            const bool selected = i == item.selectedBone;
            if (ImGui::Selectable(item.bonePoses[i].name.c_str(), selected)) {
                item.selectedBone = i;
                item.showBones = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    EditorBonePose& pose = item.bonePoses[item.selectedBone];
    bool changed = false;
    auto trackBoneDrag = [&](bool itemChanged) {
        if (ImGui::IsItemActivated() && !transformDragActive_) {
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
        if (itemChanged) {
            transformDragChanged_ = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
            if (transformDragChanged_) {
                PushUndoSnapshot_(transformDragBefore_);
            }
            transformDragActive_ = false;
            transformDragChanged_ = false;
        }
    };

    bool t = ImGui::DragFloat3("Bone Translate", &pose.translate.x, 0.01f, -10.0f, 10.0f);
    changed |= t;
    trackBoneDrag(t);
    bool r = ImGui::DragFloat3("Bone Rotate", &pose.rotate.x, 0.01f, -100.0f, 100.0f);
    changed |= r;
    trackBoneDrag(r);
    bool s = ImGui::DragFloat3("Bone Scale", &pose.scale.x, 0.01f, 0.01f, 10.0f);
    changed |= s;
    trackBoneDrag(s);

    if (ImGui::Button("Reset Bone")) {
        transformDragBefore_ = CaptureEditorSnapshot_();
        pose.translate = { 0.0f, 0.0f, 0.0f };
        pose.rotate = { 0.0f, 0.0f, 0.0f };
        pose.scale = { 1.0f, 1.0f, 1.0f };
        PushUndoSnapshot_(transformDragBefore_);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All Bones")) {
        transformDragBefore_ = CaptureEditorSnapshot_();
        for (auto& bonePose : item.bonePoses) {
            bonePose.translate = { 0.0f, 0.0f, 0.0f };
            bonePose.rotate = { 0.0f, 0.0f, 0.0f };
            bonePose.scale = { 1.0f, 1.0f, 1.0f };
        }
        PushUndoSnapshot_(transformDragBefore_);
        changed = true;
    }

    if (changed) {
        ApplyEditorObjectBonePose_(item);
    }
#endif
}

void ParticleTestScene::DrawViewportBones_()
{
#ifdef USE_IMGUI
    Camera* sceneCamera = GetSceneCamera_();
    if (!gHasSceneImageRect || !sceneCamera) {
        return;
    }
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }
    EditorObject& item = editorObjects_[selectedEditorObject_];
    if (!item.showBones || !item.object || !item.object->HasSkinningModel()) {
        return;
    }

    SyncEditorObjectBones_(item);
    const Model::Skeleton* skeleton = item.object->GetSkeleton();
    if (!skeleton || skeleton->joints.empty()) {
        return;
    }

    const ImVec2 sceneMin = gSceneImageMin;
    const ImVec2 sceneMax = gSceneImageMax;
    const float sceneW = std::max(1.0f, sceneMax.x - sceneMin.x);
    const float sceneH = std::max(1.0f, sceneMax.y - sceneMin.y);

    auto project = [&](const Vector3& world, ImVec2& out) -> bool {
        const Matrix4x4& vp = sceneCamera->GetViewProjectionMatrix();
        const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
        const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
        const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
        if (w <= 0.001f) {
            return false;
        }
        const float ndcX = x / w;
        const float ndcY = y / w;
        out.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneW;
        out.y = sceneMin.y + (0.5f - ndcY * 0.5f) * sceneH;
        return out.x >= sceneMin.x - 80.0f && out.x <= sceneMax.x + 80.0f &&
            out.y >= sceneMin.y - 80.0f && out.y <= sceneMax.y + 80.0f;
    };

    std::vector<Vector3> worldPositions(skeleton->joints.size());
    std::vector<ImVec2> screenPositions(skeleton->joints.size());
    std::vector<bool> visible(skeleton->joints.size(), false);
    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        Matrix4x4 jointWorld{};
        if (!item.object->TryGetJointWorldMatrix(skeleton->joints[i].name, jointWorld)) {
            continue;
        }
        worldPositions[i] = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
        visible[i] = project(worldPositions[i], screenPositions[i]);
    }

    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInsideScene =
        mouse.x >= sceneMin.x && mouse.x <= sceneMax.x &&
        mouse.y >= sceneMin.y && mouse.y <= sceneMax.y;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0 && !viewportBoneDragActive_) {
        int nearestBone = -1;
        float nearestDistance = 9999.0f;
        for (int i = 0; i < static_cast<int>(screenPositions.size()); ++i) {
            if (!visible[i]) {
                continue;
            }
            const float dx = mouse.x - screenPositions[i].x;
            const float dy = mouse.y - screenPositions[i].y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestBone = i;
            }
        }

        if (nearestBone >= 0 && nearestDistance <= 10.0f) {
            item.selectedBone = nearestBone;
            activeViewportBone_ = nearestBone;
            viewportBoneLastMouseX_ = mouse.x;
            viewportBoneLastMouseY_ = mouse.y;
            transformDragBefore_ = CaptureEditorSnapshot_();
            viewportBoneDragActive_ = true;
            viewportBoneDragChanged_ = false;
        }
    }

    if (viewportBoneDragActive_ && activeViewportBone_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta{ mouse.x - viewportBoneLastMouseX_, mouse.y - viewportBoneLastMouseY_ };
        viewportBoneLastMouseX_ = mouse.x;
        viewportBoneLastMouseY_ = mouse.y;

        if (activeViewportBone_ < static_cast<int>(item.bonePoses.size())) {
            const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
            const float amountX = delta.x / 55.0f;
            const float amountY = -delta.y / 55.0f;
            item.bonePoses[activeViewportBone_].translate += CameraRight(cameraWorld) * amountX;
            item.bonePoses[activeViewportBone_].translate += CameraUp(cameraWorld) * amountY;
            ApplyEditorObjectBonePose_(item);
            viewportBoneDragChanged_ = true;
        }
    }

    if (viewportBoneDragActive_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (viewportBoneDragChanged_) {
            PushUndoSnapshot_(transformDragBefore_);
        }
        viewportBoneDragActive_ = false;
        viewportBoneDragChanged_ = false;
        activeViewportBone_ = -1;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImU32 lineColor = IM_COL32(255, 220, 80, 230);
    const ImU32 jointColor = IM_COL32(255, 255, 255, 245);
    const ImU32 selectedColor = IM_COL32(80, 170, 255, 255);

    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        if (!visible[i] || !skeleton->joints[i].parent.has_value()) {
            continue;
        }
        const int32_t parentIndex = *skeleton->joints[i].parent;
        if (parentIndex < 0 || parentIndex >= static_cast<int32_t>(screenPositions.size()) || !visible[parentIndex]) {
            continue;
        }
        drawList->AddLine(screenPositions[parentIndex], screenPositions[i], lineColor, 2.0f);
    }

    for (size_t i = 0; i < screenPositions.size(); ++i) {
        if (!visible[i]) {
            continue;
        }
        const bool selected = static_cast<int>(i) == item.selectedBone;
        drawList->AddCircleFilled(screenPositions[i], selected ? 5.5f : 3.5f, selected ? selectedColor : jointColor, 16);
        drawList->AddCircle(screenPositions[i], selected ? 7.0f : 5.0f, IM_COL32(30, 30, 30, 220), 16, 1.0f);
    }
#endif
}

void ParticleTestScene::DrawViewportGizmo_()
{
#ifdef USE_IMGUI
    Camera* sceneCamera = GetSceneCamera_();
    if (!gHasSceneImageRect || !sceneCamera) {
        return;
    }
    if (selectedEditorObject_ < 0 || selectedEditorObject_ >= static_cast<int>(editorObjects_.size())) {
        return;
    }

    EditorObject& item = editorObjects_[selectedEditorObject_];
    const ImVec2 sceneMin = gSceneImageMin;
    const ImVec2 sceneMax = gSceneImageMax;
    const float sceneW = std::max(1.0f, sceneMax.x - sceneMin.x);
    const float sceneH = std::max(1.0f, sceneMax.y - sceneMin.y);

    auto project = [&](const Vector3& world, ImVec2& out) -> bool {
        const Matrix4x4& vp = sceneCamera->GetViewProjectionMatrix();
        const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
        const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
        const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
        if (w <= 0.001f) {
            return false;
        }
        const float ndcX = x / w;
        const float ndcY = y / w;
        out.x = sceneMin.x + (ndcX * 0.5f + 0.5f) * sceneW;
        out.y = sceneMin.y + (0.5f - ndcY * 0.5f) * sceneH;
        return out.x >= sceneMin.x - 80.0f && out.x <= sceneMax.x + 80.0f &&
            out.y >= sceneMin.y - 80.0f && out.y <= sceneMax.y + 80.0f;
    };

    Vector3 gizmoWorldPosition = item.position;
    bool editingBone = false;
    EditorBonePose* selectedBonePose = nullptr;
    if (item.showBones && item.object && item.object->HasSkinningModel()) {
        SyncEditorObjectBones_(item);
        if (!item.bonePoses.empty()) {
            item.selectedBone = std::clamp(item.selectedBone, 0, static_cast<int>(item.bonePoses.size()) - 1);
            const Model::Skeleton* skeleton = item.object->GetSkeleton();
            if (skeleton && item.selectedBone < static_cast<int>(skeleton->joints.size())) {
                Matrix4x4 jointWorld{};
                if (item.object->TryGetJointWorldMatrix(skeleton->joints[item.selectedBone].name, jointWorld)) {
                    gizmoWorldPosition = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };
                    selectedBonePose = &item.bonePoses[item.selectedBone];
                    editingBone = selectedBonePose != nullptr;
                }
            }
        }
    }

    ImVec2 center{};
    if (!project(gizmoWorldPosition, center)) {
        return;
    }

    const Vector3 axisWorld[3] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
    };
    const ImU32 axisColor[3] = {
        IM_COL32(255, 70, 70, 255),
        IM_COL32(80, 255, 80, 255),
        IM_COL32(90, 130, 255, 255),
    };
    ImVec2 axisEnd[3]{};
    ImVec2 axisDir[3]{};
    float axisLen[3]{};
    const float worldHandleLength = 1.5f;

    for (int axis = 0; axis < 3; ++axis) {
        ImVec2 projectedEnd{};
        if (!project(gizmoWorldPosition + axisWorld[axis] * worldHandleLength, projectedEnd)) {
            projectedEnd = center;
        }
        ImVec2 rawDir{ projectedEnd.x - center.x, projectedEnd.y - center.y };
        float len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y);
        if (len < 0.001f) {
            rawDir = axis == 0 ? ImVec2(1.0f, 0.0f) : axis == 1 ? ImVec2(0.0f, -1.0f) : ImVec2(0.7f, 0.7f);
            len = 1.0f;
        }
        axisDir[axis] = ImVec2(rawDir.x / len, rawDir.y / len);
        axisLen[axis] = 72.0f;
        axisEnd[axis] = ImVec2(center.x + axisDir[axis].x * axisLen[axis], center.y + axisDir[axis].y * axisLen[axis]);
    }

    auto distanceToSegment = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        const ImVec2 ab{ b.x - a.x, b.y - a.y };
        const ImVec2 ap{ p.x - a.x, p.y - a.y };
        const float abLen2 = ab.x * ab.x + ab.y * ab.y;
        float t = abLen2 > 0.001f ? (ap.x * ab.x + ap.y * ab.y) / abLen2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const ImVec2 nearest{ a.x + ab.x * t, a.y + ab.y * t };
        const float dx = p.x - nearest.x;
        const float dy = p.y - nearest.y;
        return std::sqrt(dx * dx + dy * dy);
    };

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const bool mouseInsideScene =
        ImGui::GetMousePos().x >= sceneMin.x && ImGui::GetMousePos().x <= sceneMax.x &&
        ImGui::GetMousePos().y >= sceneMin.y && ImGui::GetMousePos().y <= sceneMax.y;
    const ImVec2 mouse = ImGui::GetMousePos();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0 && !viewportBoneDragActive_) {
        int nearestAxis = -1;
        float nearestDistance = 9999.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float distance = distanceToSegment(mouse, center, axisEnd[axis]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestAxis = axis;
            }
        }
        const float centerDistance = std::sqrt((mouse.x - center.x) * (mouse.x - center.x) + (mouse.y - center.y) * (mouse.y - center.y));
        if (centerDistance <= 14.0f || nearestDistance <= 10.0f) {
            activeViewportGizmoAxis_ = centerDistance <= 14.0f ? 3 : nearestAxis;
            viewportGizmoLastMouseX_ = mouse.x;
            viewportGizmoLastMouseY_ = mouse.y;
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
    }

    if (activeViewportGizmoAxis_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta{ mouse.x - viewportGizmoLastMouseX_, mouse.y - viewportGizmoLastMouseY_ };
        viewportGizmoLastMouseX_ = mouse.x;
        viewportGizmoLastMouseY_ = mouse.y;
        const int axis = activeViewportGizmoAxis_;
        if (axis >= 0 && axis < 3) {
            const float signedPixels = delta.x * axisDir[axis].x + delta.y * axisDir[axis].y;
            const float amount = signedPixels / 55.0f;
            if (std::abs(amount) > 0.00001f) {
                if (gizmoMode_ == GizmoMode::Translate) {
                    Vector3& translate = editingBone ? selectedBonePose->translate : item.position;
                    if (axis == 0) translate.x += amount;
                    if (axis == 1) translate.y += amount;
                    if (axis == 2) translate.z += amount;
                } else if (gizmoMode_ == GizmoMode::Rotate) {
                    Vector3& rotate = editingBone ? selectedBonePose->rotate : item.rotation;
                    if (axis == 0) rotate.x += amount * 0.35f;
                    if (axis == 1) rotate.y += amount * 0.35f;
                    if (axis == 2) rotate.z += amount * 0.35f;
                } else {
                    Vector3& scale = editingBone ? selectedBonePose->scale : item.scale;
                    if (axis == 0) scale.x = std::max(0.01f, scale.x + amount);
                    if (axis == 1) scale.y = std::max(0.01f, scale.y + amount);
                    if (axis == 2) scale.z = std::max(0.01f, scale.z + amount);
                }
                transformDragChanged_ = true;
                if (editingBone) {
                    ApplyEditorObjectBonePose_(item);
                } else {
                    ApplyEditorObjectTransform_(item);
                }
            }
        } else if (axis == 3) {
            const float amountX = delta.x / 55.0f;
            const float amountY = -delta.y / 55.0f;
            if (gizmoMode_ == GizmoMode::Translate) {
                const Matrix4x4& cameraWorld = sceneCamera->GetWorldMatrix();
                Vector3& translate = editingBone ? selectedBonePose->translate : item.position;
                translate += CameraRight(cameraWorld) * amountX;
                translate += CameraUp(cameraWorld) * amountY;
            } else if (gizmoMode_ == GizmoMode::Scale) {
                const float amount = (amountX + amountY) * 0.5f;
                Vector3& scale = editingBone ? selectedBonePose->scale : item.scale;
                scale.x = std::max(0.01f, scale.x + amount);
                scale.y = std::max(0.01f, scale.y + amount);
                scale.z = std::max(0.01f, scale.z + amount);
            } else {
                Vector3& rotate = editingBone ? selectedBonePose->rotate : item.rotation;
                rotate.y += amountX * 0.35f;
                rotate.x += amountY * 0.35f;
            }
            transformDragChanged_ = true;
            if (editingBone) {
                ApplyEditorObjectBonePose_(item);
            } else {
                ApplyEditorObjectTransform_(item);
            }
        }
    }

    if (activeViewportGizmoAxis_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (transformDragChanged_) {
            PushUndoSnapshot_(transformDragBefore_);
        }
        activeViewportGizmoAxis_ = -1;
        transformDragActive_ = false;
        transformDragChanged_ = false;
    }

    const char* modeText =
        gizmoMode_ == GizmoMode::Translate ? "Translate" :
        gizmoMode_ == GizmoMode::Rotate ? "Rotate" : "Scale";
    const std::string gizmoLabel = editingBone ? (std::string("Bone ") + modeText) : modeText;
    drawList->AddCircleFilled(center, activeViewportGizmoAxis_ == 3 ? 9.0f : 7.0f, IM_COL32(255, 255, 255, 230));
    drawList->AddCircle(center, gizmoMode_ == GizmoMode::Rotate ? 46.0f : 14.0f, IM_COL32(255, 255, 255, 180), 48, 2.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const float thickness = activeViewportGizmoAxis_ == axis ? 5.0f : 3.0f;
        drawList->AddLine(center, axisEnd[axis], axisColor[axis], thickness);
        drawList->AddCircleFilled(axisEnd[axis], gizmoMode_ == GizmoMode::Scale ? 7.0f : 5.0f, axisColor[axis]);
    }
    drawList->AddText(ImVec2(center.x + 12.0f, center.y + 12.0f), IM_COL32(255, 255, 255, 230), gizmoLabel.c_str());
#endif
}

void ParticleTestScene::DrawEditorCameraControls_()
{
#ifdef USE_IMGUI
    if (!camera_ || !gHasSceneImageRect) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool mouseInsideScene =
        mouse.x >= gSceneImageMin.x && mouse.x <= gSceneImageMax.x &&
        mouse.y >= gSceneImageMin.y && mouse.y <= gSceneImageMax.y;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mouseInsideScene) {
        editorCameraControlActive_ = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        editorCameraControlActive_ = false;
    }

    if (editorCameraControlActive_) {
        editorCameraRotation_.y += io.MouseDelta.x * editorCameraLookSpeed_;
        editorCameraRotation_.x += io.MouseDelta.y * editorCameraLookSpeed_;
        editorCameraRotation_.x = std::clamp(editorCameraRotation_.x, -kPi * 0.49f, kPi * 0.49f);

        const Matrix4x4 cameraRotation = Matrix4x4::RotateXYZ(editorCameraRotation_.x, editorCameraRotation_.y, editorCameraRotation_.z);
        const Vector3 right = CameraRight(cameraRotation);
        const Vector3 up = CameraUp(cameraRotation);
        const Vector3 forward = CameraForward(cameraRotation);
        const float speed = editorCameraMoveSpeed_ * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 3.0f : 1.0f);

        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            editorCameraPosition_ += forward * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            editorCameraPosition_ -= forward * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            editorCameraPosition_ += right * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            editorCameraPosition_ -= right * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            editorCameraPosition_ += up * speed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            editorCameraPosition_ -= up * speed;
        }

        camera_->SetTranslate(editorCameraPosition_);
        camera_->SetRotate(editorCameraRotation_);
        camera_->Update();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Editor Camera");
    bool cameraChanged = false;
    cameraChanged |= ImGui::DragFloat3("Camera Position", &editorCameraPosition_.x, 0.1f);
    cameraChanged |= ImGui::DragFloat3("Camera Rotation", &editorCameraRotation_.x, 0.01f);
    ImGui::DragFloat("Move Speed", &editorCameraMoveSpeed_, 0.01f, 0.01f, 5.0f);
    ImGui::DragFloat("Look Speed", &editorCameraLookSpeed_, 0.0005f, 0.001f, 0.05f, "%.4f");
    const bool applyCamera = ImGui::Button("Apply Camera");
    if (cameraChanged || applyCamera) {
        camera_->SetTranslate(editorCameraPosition_);
        camera_->SetRotate(editorCameraRotation_);
        camera_->Update();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera")) {
        editorCameraPosition_ = { 0.0f, 3.0f, -20.0f };
        editorCameraRotation_ = { 0.0f, 0.0f, 0.0f };
        camera_->SetTranslate(editorCameraPosition_);
        camera_->SetRotate(editorCameraRotation_);
        camera_->Update();
    }
#endif
}

void ParticleTestScene::DrawAnimationCameraControls_()
{
#ifdef USE_IMGUI
    ImGui::Separator();
    ImGui::TextUnformatted("Animation Camera");
    bool cameraChanged = false;
    cameraChanged |= ImGui::Checkbox("Preview Animation Camera", &useAnimationCameraPreview_);
    if (!useAnimationCameraPreview_) {
        animationCameraPreviewSwapped_ = false;
    }
    cameraChanged |= ImGui::DragFloat3("Anim Cam Position", &animationCameraPosition_.x, 0.1f);
    cameraChanged |= ImGui::DragFloat3("Anim Cam Rotation", &animationCameraRotation_.x, 0.01f);
    cameraChanged |= ImGui::SliderFloat("Anim Cam FovY", &animationCameraFovY_, 0.1f, 1.8f, "%.3f");
    if (cameraChanged) {
        ApplyAnimationCamera_();
        ApplyCameraToEditorObjects_();
    }

    if (ImGui::Button("Copy From Editor Camera") && camera_) {
        PushUndoSnapshot_();
        animationCameraPosition_ = camera_->GetTranslate();
        animationCameraRotation_ = camera_->GetRotate();
        animationCameraFovY_ = camera_->GetFovY();
        ApplyAnimationCamera_();
        ApplyCameraToEditorObjects_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Anim Camera")) {
        PushUndoSnapshot_();
        animationCameraPosition_ = { 0.0f, 3.0f, -12.0f };
        animationCameraRotation_ = { 0.0f, 0.0f, 0.0f };
        animationCameraFovY_ = 0.45f;
        ApplyAnimationCamera_();
        ApplyCameraToEditorObjects_();
    }

    if (ImGui::Button("Add / Replace Camera Key")) {
        PushUndoSnapshot_();
        AddCameraKeyframe_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Near Camera Key")) {
        PushUndoSnapshot_();
        DeleteNearestCameraKeyframe_();
    }
#endif
}

void ParticleTestScene::HandleEffectEditorShortcuts_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool rightCameraDrag =
        gHasSceneImageRect &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
        mouse.x >= gSceneImageMin.x && mouse.x <= gSceneImageMax.x &&
        mouse.y >= gSceneImageMin.y && mouse.y <= gSceneImageMax.y;

    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        Undo_(app);
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        Redo_(app);
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        CopySelectedObject_();
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && hasCopiedObject_) {
        PushUndoSnapshot_();
        PasteEditorObject_(app);
    }
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (selectedEditorObject_ >= 0) {
            RequestDeleteSelectedObject_();
        } else if (selectedParticleNode_ >= 0) {
            PushUndoSnapshot_();
            particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
            selectedParticleNode_ = -1;
        }
    }
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        gizmoMode_ = GizmoMode::Translate;
    }
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        gizmoMode_ = GizmoMode::Rotate;
    }
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        gizmoMode_ = GizmoMode::Scale;
    }
#endif
}

void ParticleTestScene::DrawEffectInspectorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGui::Begin("Inspector");

    ImGui::TextUnformatted("Model Source");
    ImGui::InputText("Model Path", editorModelPath_, sizeof(editorModelPath_));
    if (ImGui::Button("Open Model File...")) {
        OpenModelFileDialog_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open + Add")) {
        if (OpenModelFileDialog_()) {
            PushUndoSnapshot_();
            AddEditorObject_(app, editorModelPath_);
        }
    }
    if (ImGui::Button("Add Model")) {
        PushUndoSnapshot_();
        AddEditorObject_(app, editorModelPath_);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Geometry Source");
    ImGui::Combo("Geometry Type", &selectedGeometryType_, kGeometryNames, kGeometryCount);
    if (ImGui::Button("Add Geometry")) {
        PushUndoSnapshot_();
        AddGeometryObject_(app, selectedGeometryType_);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Particle Node Source");
    if (ImGui::Button("Add Particle Node")) {
        std::vector<std::string> groupNames;
        std::string fileName;
        if (OpenParticleFileDialog_(groupNames, fileName)) {
            PushUndoSnapshot_();
            ParticleManager::GetInstance()->LoadAdditional(fileName, "");
            
            std::filesystem::path fp(fileName);
            std::string stemName = fp.stem().string();

            auto* pm = ParticleManager::GetInstance();
            float maxLifeTime = 0.0f;
            for (const auto& groupName : groupNames) {
                maxLifeTime = std::max(maxLifeTime, pm->GetGroupLifeTimeMax(groupName));
            }
            if (maxLifeTime <= 0.0f) {
                maxLifeTime = 1.0f;
            }

            ParticleNode node;
            node.name = stemName + "_" + std::to_string(particleNodes_.size() + 1);
            node.particleFileName = fileName;
            node.startTime = 0.0f;
            node.presetDuration = maxLifeTime;
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            node.position = { 0.0f, 1.0f, 0.0f };
            particleNodes_.push_back(std::move(node));

            selectedParticleNode_ = static_cast<int>(particleNodes_.size()) - 1;
            selectedEditorObject_ = -1;
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Object Actions");
    if (ImGui::Button("Duplicate") && selectedEditorObject_ >= 0) {
        PushUndoSnapshot_();
        DuplicateSelectedObject_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy") && selectedEditorObject_ >= 0) {
        CopySelectedObject_();
    }
    if (ImGui::Button("Paste") && hasCopiedObject_) {
        PushUndoSnapshot_();
        PasteEditorObject_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        if (selectedEditorObject_ >= 0) {
            RequestDeleteSelectedObject_();
        } else if (selectedParticleNode_ >= 0) {
            PushUndoSnapshot_();
            particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
            selectedParticleNode_ = -1;
        }
    }
    if (ImGui::Button("Undo")) {
        Undo_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo")) {
        Redo_(app);
    }

    ImGui::InputText("Effect JSON", effectJsonPath_, sizeof(effectJsonPath_));
    if (ImGui::Button("Save Effect JSON")) {
        SaveEffectJson_(effectJsonPath_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Effect JSON")) {
        PushUndoSnapshot_();
        LoadEffectJson_(app, effectJsonPath_);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Scene Objects");
    for (int i = 0; i < static_cast<int>(editorObjects_.size()); ++i) {
        const bool selected = i == selectedEditorObject_;
        if (ImGui::Selectable(editorObjects_[i].name.c_str(), selected)) {
            selectedEditorObject_ = i;
            selectedParticleNode_ = -1;
        }
    }
    for (int i = 0; i < static_cast<int>(particleNodes_.size()); ++i) {
        const bool selected = i == selectedParticleNode_;
        char label[128];
        sprintf_s(label, "%s (%.2f-%.2f) [Particle]", particleNodes_[i].name.c_str(), particleNodes_[i].startTime, particleNodes_[i].endTime);
        if (ImGui::Selectable(label, selected)) {
            selectedParticleNode_ = i;
            selectedEditorObject_ = -1;
        }
    }

    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        EditorObject& item = editorObjects_[selectedEditorObject_];
        ImGui::Separator();
        ImGui::Text("%s (%s)", item.name.c_str(), item.modelPath.c_str());
        bool changed = false;

        auto trackDragEdit = [&](bool itemChanged) {
            if (ImGui::IsItemActivated() && !transformDragActive_) {
                transformDragBefore_ = CaptureEditorSnapshot_();
                transformDragActive_ = true;
                transformDragChanged_ = false;
            }
            if (itemChanged) {
                transformDragChanged_ = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
                if (transformDragChanged_) {
                    PushUndoSnapshot_(transformDragBefore_);
                }
                transformDragActive_ = false;
                transformDragChanged_ = false;
            }
        };

        bool modelChanged = false;
        if (item.geometryType < 0) {
            char modelBuf[256];
            strncpy_s(modelBuf, sizeof(modelBuf), item.modelPath.c_str(), _TRUNCATE);
            if (ImGui::InputText("Model Path", modelBuf, sizeof(modelBuf))) {
                item.modelPath = modelBuf;
                modelChanged = true;
            }
            if (ImGui::IsItemActivated() && !transformDragActive_) {
                transformDragBefore_ = CaptureEditorSnapshot_();
                transformDragActive_ = true;
                transformDragChanged_ = false;
            }
            if (ImGui::IsItemEdited()) {
                transformDragChanged_ = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
                if (transformDragChanged_) {
                    PushUndoSnapshot_(transformDragBefore_);
                }
                transformDragActive_ = false;
                transformDragChanged_ = false;
            }
            if (ImGui::Button("Open Model File...##SelectedObject")) {
                std::string modelPath;
                if (OpenModelFileDialog_(modelPath)) {
                    PushUndoSnapshot_();
                    item.modelPath = modelPath;
                    modelChanged = true;
                }
            }
        }
        if (modelChanged) {
            item.object->SetModel(item.modelPath);
            changed = true;
        }

        bool positionChanged = ImGui::DragFloat3("Position", &item.position.x, 0.05f);
        changed |= positionChanged;
        trackDragEdit(positionChanged);
        bool rotationChanged = ImGui::DragFloat3("Rotation", &item.rotation.x, 0.01f);
        changed |= rotationChanged;
        trackDragEdit(rotationChanged);
        bool scaleChanged = ImGui::DragFloat3("Scale", &item.scale.x, 0.05f, 0.01f, 100.0f);
        changed |= scaleChanged;
        trackDragEdit(scaleChanged);
        bool colorChanged = ImGui::ColorEdit4("Color / Alpha", &item.color.x);
        changed |= colorChanged;
        trackDragEdit(colorChanged);
        int currentBlend = static_cast<int>(item.blendMode);
        if (ImGui::Combo("Blend Mode", &currentBlend, kObjectBlendModeNames, IM_ARRAYSIZE(kObjectBlendModeNames))) {
            PushUndoSnapshot_();
            currentBlend = std::clamp(currentBlend, 0, static_cast<int>(Object3dCommon::BlendMode::kCountOfBlendMode) - 1);
            item.blendMode = static_cast<Object3dCommon::BlendMode>(currentBlend);
            changed = true;
        }

        char textureBuf[256];
        strncpy_s(textureBuf, sizeof(textureBuf), item.texturePath.c_str(), _TRUNCATE);
        if (ImGui::InputText("Texture Path", textureBuf, sizeof(textureBuf))) {
            item.texturePath = textureBuf;
            changed = true;
        }
        if (ImGui::IsItemActivated() && !transformDragActive_) {
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
        if (ImGui::IsItemEdited()) {
            transformDragChanged_ = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
            if (transformDragChanged_) {
                PushUndoSnapshot_(transformDragBefore_);
            }
            transformDragActive_ = false;
            transformDragChanged_ = false;
        }
        if (ImGui::Button("Open Texture File...")) {
            std::string texturePath;
            if (OpenTextureFileDialog_(texturePath)) {
                PushUndoSnapshot_();
                item.texturePath = texturePath;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Texture")) {
            PushUndoSnapshot_();
            item.texturePath.clear();
            changed = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Object Post Effect");
        const bool bloomBefore = item.bloomPostEffect;
        if (ImGui::Checkbox("Bloom", &item.bloomPostEffect)) {
            item.bloomPostEffect = bloomBefore;
            PushUndoSnapshot_();
            item.bloomPostEffect = !bloomBefore;
        }
        const bool outlineBloomBefore = item.outlineBloomPostEffect;
        if (ImGui::Checkbox("Outline Bloom", &item.outlineBloomPostEffect)) {
            item.outlineBloomPostEffect = outlineBloomBefore;
            PushUndoSnapshot_();
            item.outlineBloomPostEffect = !outlineBloomBefore;
        }
        
        bool bloomColorChanged = ImGui::ColorEdit4("Bloom Color", &item.bloomColor.x);
        if (ImGui::IsItemActivated() && !transformDragActive_) {
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
        if (bloomColorChanged) {
            transformDragChanged_ = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
            if (transformDragChanged_) {
                PushUndoSnapshot_(transformDragBefore_);
            }
            transformDragActive_ = false;
            transformDragChanged_ = false;
        }

        bool outlineBloomColorChanged = ImGui::ColorEdit4("Outline Bloom Color", &item.outlineBloomColor.x);
        if (ImGui::IsItemActivated() && !transformDragActive_) {
            transformDragBefore_ = CaptureEditorSnapshot_();
            transformDragActive_ = true;
            transformDragChanged_ = false;
        }
        if (outlineBloomColorChanged) {
            transformDragChanged_ = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && transformDragActive_) {
            if (transformDragChanged_) {
                PushUndoSnapshot_(transformDragBefore_);
            }
            transformDragActive_ = false;
            transformDragChanged_ = false;
        }

        const bool billboardBefore = item.billboard;
        bool billboardChanged = ImGui::Checkbox("Billboard", &item.billboard);
        if (billboardChanged) {
            item.billboard = billboardBefore;
            PushUndoSnapshot_();
            item.billboard = !billboardBefore;
            changed = true;
        }
        if (changed) {
            ApplyEditorObjectTransform_(item);
        }

        DrawGizmoControls_(item);
        DrawBoneControls_(item);
    }

    if (selectedParticleNode_ >= 0 && selectedParticleNode_ < static_cast<int>(particleNodes_.size())) {
        ParticleNode& node = particleNodes_[selectedParticleNode_];
        ImGui::Separator();
        ImGui::Text("%s (ParticleNode)", node.name.c_str());

        char nameBuf[128];
        strncpy_s(nameBuf, sizeof(nameBuf), node.name.c_str(), _TRUNCATE);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            node.name = nameBuf;
        }

        char fileBuf[128];
        strncpy_s(fileBuf, sizeof(fileBuf), node.particleFileName.c_str(), _TRUNCATE);
        if (ImGui::InputText("Particle File", fileBuf, sizeof(fileBuf))) {
            node.particleFileName = fileBuf;
        }

        if (ImGui::DragFloat("Start Time", &node.startTime, 0.01f, 0.0f, timelineDuration_)) {
            node.startTime = std::clamp(node.startTime, 0.0f, timelineDuration_);
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat("End Time", &node.endTime, 0.01f, 0.0f, timelineDuration_)) {
            node.endTime = std::clamp(node.endTime, node.startTime + 0.01f, timelineDuration_);
            node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat("Preset Duration", &node.presetDuration, 0.01f, 0.01f, 10.0f)) {
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat3("Position", &node.position.x, 0.05f)) {
            RequestTimelineRebuild_(timelineTime_);
        }
        ImGui::DragFloat3("Rotation", &node.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale", &node.scale.x, 0.05f);
        ImGui::DragInt("Emit Count", &node.emitCount, 1, 1, 1000);
    }

    DrawEditorCameraControls_();
    DrawAnimationCameraControls_();
    ImGui::End();
#endif
}

void ParticleTestScene::DrawEffectEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGui::Begin("Effect Editor");

    ImGui::TextUnformatted("Timeline (Dope Sheet)");
    DrawDopeSheet_(app);

    ImGui::Separator();

    // タイムラインコントローラー
    if (ImGui::Button(timelinePlaying_ ? "Stop" : "Play")) {
        const bool startPlayback = !timelinePlaying_;
        timelinePlaying_ = startPlayback;
        if (startPlayback && timelineTime_ == 0.0f) {
            for (auto& node : particleNodes_) {
                node.hasEmitted = false;
            }
            lastTimelineTime_ = -1.0f;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        timelineTime_ = 0.0f;
        lastTimelineTime_ = -0.001f;
        timelinePlaying_ = true;
        pendingTimelineRebuild_ = true;
        pendingTimelineRebuildTime_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &timelineLoop_);
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::DragFloat("Duration", &timelineDuration_, 0.05f, 0.05f, 30.0f, "%.2f s")) {
        if (timelineDuration_ < 0.05f) timelineDuration_ = 0.05f;
        timelineTime_ = std::clamp(timelineTime_, 0.0f, timelineDuration_);
        RequestTimelineRebuild_(timelineTime_);
    }
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::SliderFloat("Current Time", &timelineTime_, 0.0f, timelineDuration_, "%.3f s")) {
        RequestTimelineRebuild_(timelineTime_);
    }

    ImGui::SameLine();
    if (ImGui::Button("Add Particle Node")) {
        std::vector<std::string> groupNames;
        std::string fileName;
        if (OpenParticleFileDialog_(groupNames, fileName)) {
            PushUndoSnapshot_();
            ParticleManager::GetInstance()->LoadAdditional(fileName, "");
            
            std::filesystem::path fp(fileName);
            std::string stemName = fp.stem().string();

            auto* pm = ParticleManager::GetInstance();
            float maxLifeTime = 0.0f;
            for (const auto& groupName : groupNames) {
                maxLifeTime = std::max(maxLifeTime, pm->GetGroupLifeTimeMax(groupName));
            }
            if (maxLifeTime <= 0.0f) {
                maxLifeTime = 1.0f;
            }

            ParticleNode node;
            node.name = stemName + "_" + std::to_string(particleNodes_.size() + 1);
            node.particleFileName = fileName;
            node.startTime = 0.0f;
            node.presetDuration = maxLifeTime;
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            node.position = { 0.0f, 1.0f, 0.0f };
            particleNodes_.push_back(std::move(node));

            selectedParticleNode_ = static_cast<int>(particleNodes_.size()) - 1;
            selectedEditorObject_ = -1;
        }
    }

    ImGui::Separator();

    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        EditorObject& item = editorObjects_[selectedEditorObject_];
        ImGui::Text("Keyframes: %s (%s)", item.name.c_str(), item.modelPath.c_str());
        
        if (ImGui::Button("Add / Replace Keyframe")) {
            PushUndoSnapshot_();
            AddKeyframeToSelected_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Near Keyframe")) {
            PushUndoSnapshot_();
            DeleteNearestKeyframeFromSelected_();
        }

        if (ImGui::BeginTable("KeyframesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Position");
            ImGui::TableSetupColumn("Rotation");
            ImGui::TableSetupColumn("Scale");
            ImGui::TableSetupColumn("Color");
            ImGui::TableHeadersRow();
            for (size_t k = 0; k < item.keyframes.size(); ++k) {
                auto& key = item.keyframes[k];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                
                char timeId[64];
                sprintf_s(timeId, "##keytime_%zu", k);
                ImGui::SetNextItemWidth(70.0f);
                if (ImGui::DragFloat(timeId, &key.time, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
                    SortKeyframes_(item);
                    EvaluateTimeline_(false);
                }
                
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f %.2f %.2f", key.position.x, key.position.y, key.position.z);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f %.2f %.2f", key.rotation.x, key.rotation.y, key.rotation.z);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f %.2f %.2f", key.scale.x, key.scale.y, key.scale.z);
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.2f %.2f %.2f %.2f", key.color.x, key.color.y, key.color.z, key.color.w);
            }
            ImGui::EndTable();
        }
    } else if (selectedParticleNode_ >= 0 && selectedParticleNode_ < static_cast<int>(particleNodes_.size())) {
        ParticleNode& node = particleNodes_[selectedParticleNode_];
        ImGui::Text("Selected Particle Node: %s", node.name.c_str());
        
        char nameBuf[128];
        strncpy_s(nameBuf, sizeof(nameBuf), node.name.c_str(), _TRUNCATE);
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            node.name = nameBuf;
        }

        char fileBuf[128];
        strncpy_s(fileBuf, sizeof(fileBuf), node.particleFileName.c_str(), _TRUNCATE);
        if (ImGui::InputText("Particle File", fileBuf, sizeof(fileBuf))) {
            node.particleFileName = fileBuf;
        }

        if (ImGui::DragFloat("Start Time", &node.startTime, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
            node.startTime = std::clamp(node.startTime, 0.0f, timelineDuration_);
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat("End Time", &node.endTime, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
            node.endTime = std::clamp(node.endTime, node.startTime + 0.01f, timelineDuration_);
            node.presetDuration = GetParticleNodeDuration_(node);
            node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat("Preset Duration", &node.presetDuration, 0.01f, 0.01f, 10.0f)) {
            node.endTime = node.startTime + GetParticleNodeDuration_(node);
            timelineDuration_ = std::max(timelineDuration_, node.endTime);
            RequestTimelineRebuild_(timelineTime_);
        }
        if (ImGui::DragFloat3("Position", &node.position.x, 0.05f)) {
            RequestTimelineRebuild_(timelineTime_);
        }
        ImGui::DragFloat3("Rotation", &node.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale", &node.scale.x, 0.05f);
        ImGui::DragInt("Emit Count", &node.emitCount, 1, 1, 1000);
        
        if (ImGui::Button("Delete Particle Node")) {
            PushUndoSnapshot_();
            particleNodes_.erase(particleNodes_.begin() + selectedParticleNode_);
            selectedParticleNode_ = -1;
        }
    } else {
        ImGui::TextDisabled("Select an object or particle node in Hierarchy, Dope Sheet, or Inspector.");
    }

    ImGui::End();
#endif
}

void ParticleTestScene::DrawDopeSheet_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    int trackCount = 1; // Camera
    trackCount += static_cast<int>(editorObjects_.size());
    trackCount += static_cast<int>(particleNodes_.size());
    
    float trackHeight = 22.0f;
    float headerHeight = 24.0f;
    float totalHeight = headerHeight + trackCount * trackHeight;
    canvasSize.y = std::min(110.0f, totalHeight + 4.0f);
    if (canvasSize.y < 50.0f) canvasSize.y = 50.0f;
    
    ImGui::BeginChild("DopeSheetContainer", canvasSize, true, ImGuiWindowFlags_None);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    
    const float labelWidth = 160.0f;
    float timelineStartX = canvasPos.x + labelWidth;
    float timelineWidth = ImGui::GetContentRegionMax().x - timelineStartX - 16.0f;
    if (timelineWidth < 50.0f) timelineWidth = 50.0f;
    float timelineEndX = timelineStartX + timelineWidth;
    
    auto timeToX = [&](float t) -> float {
        if (timelineDuration_ <= 0.0f) return timelineStartX;
        return timelineStartX + (t / timelineDuration_) * timelineWidth;
    };
    
    auto xToTime = [&](float x) -> float {
        if (timelineWidth <= 0.0f) return 0.0f;
        float t = ((x - timelineStartX) / timelineWidth) * timelineDuration_;
        return std::clamp(t, 0.0f, timelineDuration_);
    };
    
    // ヘッダー背景
    drawList->AddRectFilled(canvasPos, ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_HeaderActive));
    drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + headerHeight), ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_Border));
    
    // グリッド線の描画
    int gridCount = 10;
    if (timelineDuration_ > 5.0f) gridCount = static_cast<int>(timelineDuration_);
    for (int i = 0; i <= gridCount; ++i) {
        float t = (static_cast<float>(i) / gridCount) * timelineDuration_;
        float gridX = timeToX(t);
        drawList->AddLine(ImVec2(gridX, canvasPos.y), ImVec2(gridX, canvasPos.y + totalHeight), ImGui::GetColorU32(ImGuiCol_Border, 0.3f));
        
        char buf[32];
        sprintf_s(buf, "%.1fs", t);
        drawList->AddText(ImVec2(gridX + 2.0f, canvasPos.y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), buf);
    }
    
    float currentY = canvasPos.y + headerHeight;
    ImVec2 mousePos = io.MousePos;
    bool clicked = ImGui::IsMouseClicked(0);
    
    // 1. Camera Track
    {
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_TableRowBg));
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), "Camera Keys");
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));
        
        for (int i = 0; i < static_cast<int>(cameraKeyframes_.size()); ++i) {
            float kx = timeToX(cameraKeyframes_[i].time);
            ImVec2 center(kx, currentY + trackHeight * 0.5f);
            
            bool hovered = (std::abs(mousePos.x - center.x) <= 6.0f && std::abs(mousePos.y - center.y) <= 6.0f);
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::CameraKeyframe;
                dragKeyframeIndex_ = i;
            }
            
            bool isSelected = (dragTarget_ == DragTarget::CameraKeyframe && dragKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
        }
        currentY += trackHeight;
    }
    
    // 2. Model Objects Tracks
    for (int objIdx = 0; objIdx < static_cast<int>(editorObjects_.size()); ++objIdx) {
        auto& item = editorObjects_[objIdx];
        bool isSelectedObj = (selectedEditorObject_ == objIdx);
        
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), isSelectedObj ? ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.4f) : ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
        
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, currentY));
        char selectId[128];
        sprintf_s(selectId, "##select_obj_%d", objIdx);
        if (ImGui::InvisibleButton(selectId, ImVec2(labelWidth, trackHeight))) {
            selectedEditorObject_ = objIdx;
            selectedParticleNode_ = -1;
        }
        
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), isSelectedObj ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), item.name.c_str());
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));
        
        for (int i = 0; i < static_cast<int>(item.keyframes.size()); ++i) {
            float kx = timeToX(item.keyframes[i].time);
            ImVec2 center(kx, currentY + trackHeight * 0.5f);
            
            bool hovered = (std::abs(mousePos.x - center.x) <= 6.0f && std::abs(mousePos.y - center.y) <= 6.0f);
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ModelKeyframe;
                dragObjectIndex_ = objIdx;
                dragKeyframeIndex_ = i;
                selectedEditorObject_ = objIdx;
                selectedParticleNode_ = -1;
            }
            
            bool isSelectedKey = (dragTarget_ == DragTarget::ModelKeyframe && dragObjectIndex_ == objIdx && dragKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
        }
        currentY += trackHeight;
    }
    
    // 3. Particle Nodes Tracks
    for (int nodeIdx = 0; nodeIdx < static_cast<int>(particleNodes_.size()); ++nodeIdx) {
        auto& node = particleNodes_[nodeIdx];
        bool isSelectedNode = (selectedParticleNode_ == nodeIdx);
        
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), isSelectedNode ? ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.4f) : ImGui::GetColorU32(ImGuiCol_TableRowBg));
        
        ImGui::SetCursorScreenPos(ImVec2(canvasPos.x, currentY));
        char selectId[128];
        sprintf_s(selectId, "##select_node_%d", nodeIdx);
        if (ImGui::InvisibleButton(selectId, ImVec2(labelWidth, trackHeight))) {
            selectedParticleNode_ = nodeIdx;
            selectedEditorObject_ = -1;
        }
        
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), isSelectedNode ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled), node.name.c_str());
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));
        
        float barStartX = timeToX(node.startTime);
        float barEndX = timeToX(node.endTime);
        ImVec2 barMin(barStartX, currentY + 3.0f);
        ImVec2 barMax(barEndX, currentY + trackHeight - 3.0f);
        
        ImU32 barColor = isSelectedNode ? ImColor(100, 220, 100, 180) : ImColor(60, 160, 60, 140);
        drawList->AddRectFilled(barMin, barMax, barColor, 4.0f);
        drawList->AddRect(barMin, barMax, ImColor(255, 255, 255, 100), 4.0f, 0, 1.0f);
        
        bool hoveredBar = (mousePos.x >= barMin.x && mousePos.x <= barMax.x && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        bool hoveredStart = (std::abs(mousePos.x - barMin.x) <= 6.0f && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        bool hoveredEnd = (std::abs(mousePos.x - barMax.x) <= 6.0f && mousePos.y >= barMin.y && mousePos.y <= barMax.y);
        
        if (clicked && dragTarget_ == DragTarget::None) {
            if (hoveredStart) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeStart;
                dragParticleNodeIndex_ = nodeIdx;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            } else if (hoveredEnd) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeEnd;
                dragParticleNodeIndex_ = nodeIdx;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            } else if (hoveredBar) {
                PushUndoSnapshot_();
                dragTarget_ = DragTarget::ParticleNodeBar;
                dragParticleNodeIndex_ = nodeIdx;
                dragStartOffset_ = xToTime(mousePos.x) - node.startTime;
                dragStartVal1_ = node.startTime;
                dragStartVal2_ = node.endTime;
                selectedParticleNode_ = nodeIdx;
                selectedEditorObject_ = -1;
            }
        }
        
        drawList->AddRectFilled(ImVec2(barMin.x - 2.0f, barMin.y), ImVec2(barMin.x + 2.0f, barMax.y), ImColor(255, 255, 255, 200), 1.0f);
        drawList->AddRectFilled(ImVec2(barMax.x - 2.0f, barMin.y), ImVec2(barMax.x + 2.0f, barMax.y), ImColor(255, 255, 255, 200), 1.0f);
        
        currentY += trackHeight;
    }
    
    // 再生ヘッドの縦線描画
    float curX = timeToX(timelineTime_);
    drawList->AddLine(ImVec2(curX, canvasPos.y), ImVec2(curX, canvasPos.y + totalHeight), ImColor(50, 150, 255, 200), 2.0f);
    
    // 再生ヘッドのつまみ描画
    ImVec2 headCenter(curX, canvasPos.y + headerHeight);
    drawList->AddTriangleFilled(ImVec2(headCenter.x - 6.0f, headCenter.y - 12.0f), ImVec2(headCenter.x + 6.0f, headCenter.y - 12.0f), ImVec2(headCenter.x, headCenter.y), ImColor(50, 150, 255));
    
    bool hoveredHead = (mousePos.x >= curX - 6.0f && mousePos.x <= curX + 6.0f && mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + headerHeight);
    bool hoveredHeaderArea = (mousePos.x >= timelineStartX && mousePos.x <= timelineEndX && mousePos.y >= canvasPos.y && mousePos.y <= canvasPos.y + headerHeight);
    
    if (clicked && dragTarget_ == DragTarget::None) {
        if (hoveredHead || hoveredHeaderArea) {
            dragTarget_ = DragTarget::TimelineTime;
        }
    }
    
    // ドラッグ中のインタラクション処理
    if (ImGui::IsMouseDown(0) && dragTarget_ != DragTarget::None) {
        float mouseT = xToTime(mousePos.x);
        
        switch (dragTarget_) {
        case DragTarget::TimelineTime:
            timelineTime_ = mouseT;
            RequestTimelineRebuild_(timelineTime_);
            break;
            
        case DragTarget::ModelKeyframe:
            if (dragObjectIndex_ >= 0 && dragObjectIndex_ < static_cast<int>(editorObjects_.size())) {
                auto& item = editorObjects_[dragObjectIndex_];
                if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(item.keyframes.size())) {
                    item.keyframes[dragKeyframeIndex_].time = mouseT;
                    SortKeyframes_(item);
                    EvaluateTimeline_(false);
                }
            }
            break;
            
        case DragTarget::CameraKeyframe:
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(cameraKeyframes_.size())) {
                cameraKeyframes_[dragKeyframeIndex_].time = mouseT;
                SortCameraKeyframes_();
                EvaluateTimeline_(false);
            }
            break;
            
        case DragTarget::ParticleNodeStart:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                node.startTime = std::min(mouseT, node.endTime - 0.01f);
                node.startTime = std::max(0.0f, node.startTime);
                node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        case DragTarget::ParticleNodeEnd:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                node.endTime = std::max(mouseT, node.startTime + 0.01f);
                node.endTime = std::min(timelineDuration_, node.endTime);
                node.presetDuration = std::max(0.01f, node.endTime - node.startTime);
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        case DragTarget::ParticleNodeBar:
            if (dragParticleNodeIndex_ >= 0 && dragParticleNodeIndex_ < static_cast<int>(particleNodes_.size())) {
                auto& node = particleNodes_[dragParticleNodeIndex_];
                float duration = dragStartVal2_ - dragStartVal1_;
                float targetStart = mouseT - dragStartOffset_;
                node.startTime = std::clamp(targetStart, 0.0f, timelineDuration_ - duration);
                node.endTime = node.startTime + duration;
                node.presetDuration = duration;
                RequestTimelineRebuild_(timelineTime_);
            }
            break;
            
        default:
            break;
        }
    }
    
    if (ImGui::IsMouseReleased(0) && dragTarget_ != DragTarget::None) {
        if (dragTarget_ == DragTarget::ModelKeyframe) {
            if (dragObjectIndex_ >= 0 && dragObjectIndex_ < static_cast<int>(editorObjects_.size())) {
                SortKeyframes_(editorObjects_[dragObjectIndex_]);
                EvaluateTimeline_(false);
            }
        } else if (dragTarget_ == DragTarget::CameraKeyframe) {
            SortCameraKeyframes_();
            EvaluateTimeline_(false);
        }
        dragTarget_ = DragTarget::None;
    }
    
    ImGui::EndChild();
#endif
}

bool ParticleTestScene::OpenParticleFileDialog_(std::vector<std::string>& outGroupNames, std::string& outFileName)
{
    char filePath[MAX_PATH]{};
    OPENFILENAMEA openFileName{};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = GetActiveWindow();
    openFileName.lpstrFilter =
        "Particle JSON Files (*.json)\0*.json\0"
        "All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = filePath;
    openFileName.nMaxFile = MAX_PATH;
    if (std::filesystem::exists("Resources/Particles")) {
        openFileName.lpstrInitialDir = "Resources\\Particles";
    } else {
        openFileName.lpstrInitialDir = "resources";
    }
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&openFileName)) {
        return false;
    }

    std::filesystem::path fullPath(filePath);
    std::string fileName = fullPath.filename().string();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    outGroupNames.clear();
    outFileName = fileName;

    try {
        nlohmann::json root;
        file >> root;
        
        if (root.is_array()) {
            for (const auto& item : root) {
                std::string gName = item.value("name", "");
                if (!gName.empty()) {
                    outGroupNames.push_back(gName);
                }
            }
        } else if (root.is_object()) {
            std::string gName = root.value("name", "");
            if (!gName.empty()) {
                outGroupNames.push_back(gName);
            }
        }

        if (outGroupNames.empty()) {
            outGroupNames.push_back(fullPath.stem().string());
        }

        return true;
    }
    catch (const std::exception&) {
        outGroupNames.push_back(fullPath.stem().string());
        return true;
    }
}

void ParticleTestScene::DrawParticleModeImGui_()
{
#ifdef USE_IMGUI
    ImGui::Begin("Particle Mode");
    ImGui::Text("JSON: %s", kParticleJson);
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
    ImGui::Separator();
    ParticleManager::GetInstance()->DrawImGuiContents();
    ImGui::End();

    if (editorParticle_) {
        editorParticle_->DebugImGui();
    }
#endif
}

void ParticleTestScene::DrawImGui(GameApp& app)
{
#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = true;
    gParticleTestEditorMode = std::clamp(gParticleTestEditorMode, 0, 1);
    editorMode_ = gParticleTestEditorMode == 0 ? EditorMode::Blender : EditorMode::Particle;

    if (editorMode_ == EditorMode::Blender) {
        HandleEffectEditorShortcuts_(app);
        if (gParticleTestBlenderHierarchySelectionChanged) {
            gParticleTestBlenderHierarchySelectionChanged = false;
            if (gParticleTestBlenderHierarchySelected >= 0) {
                if (gParticleTestBlenderHierarchySelected < static_cast<int>(editorObjects_.size())) {
                    selectedEditorObject_ = gParticleTestBlenderHierarchySelected;
                    selectedParticleNode_ = -1;
                } else if (gParticleTestBlenderHierarchySelected < static_cast<int>(editorObjects_.size() + particleNodes_.size())) {
                    selectedParticleNode_ = gParticleTestBlenderHierarchySelected - static_cast<int>(editorObjects_.size());
                    selectedEditorObject_ = -1;
                }
            } else {
                selectedEditorObject_ = -1;
                selectedParticleNode_ = -1;
            }
        }

        gParticleTestBlenderHierarchyNames.clear();
        gParticleTestBlenderHierarchyNames.reserve(editorObjects_.size() + particleNodes_.size());
        for (const auto& item : editorObjects_) {
            gParticleTestBlenderHierarchyNames.push_back(item.name + " (" + item.modelPath + ")");
        }
        for (const auto& node : particleNodes_) {
            gParticleTestBlenderHierarchyNames.push_back(node.name + " (" + node.particleFileName + ") [Particle]");
        }

        if (selectedEditorObject_ >= 0) {
            gParticleTestBlenderHierarchySelected = selectedEditorObject_;
        } else if (selectedParticleNode_ >= 0) {
            gParticleTestBlenderHierarchySelected = static_cast<int>(editorObjects_.size()) + selectedParticleNode_;
        } else {
            gParticleTestBlenderHierarchySelected = -1;
        }
        animationCameraPreviewSwapped_ = gParticleTestAnimationCameraPreviewSwapped;
        gParticleTestAnimationCameraPreviewVisible = useAnimationCameraPreview_;
        gParticleTestAnimationCameraPreviewSwapped = animationCameraPreviewSwapped_;
    } else {
        gParticleTestAnimationCameraPreviewVisible = false;
        gParticleTestAnimationCameraPreviewSwapped = false;
    }

    if (editorMode_ == EditorMode::Blender) {
        DrawEffectInspectorImGui_(app);
        DrawEffectEditorImGui_(app);
        DrawViewportBones_();
        DrawViewportGizmo_();
    } else {
        DrawParticleModeImGui_();
    }
#endif
}
