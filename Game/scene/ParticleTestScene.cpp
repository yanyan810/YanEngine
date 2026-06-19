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

    ReloadParticleJson_();
    AddEditorObject_(app, editorModelPath_);

#ifdef USE_IMGUI
    gParticleTestEditorModeSwitcherVisible = true;
    gParticleTestEditorMode = static_cast<int>(editorMode_);
#endif
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

    if (timelinePlaying_) {
        timelineTime_ += dt;
        if (timelineTime_ > timelineDuration_) {
            if (timelineLoop_ && timelineDuration_ > 0.0f) {
                timelineTime_ = std::fmod(timelineTime_, timelineDuration_);
            } else {
                timelineTime_ = timelineDuration_;
                timelinePlaying_ = false;
            }
        }
        EvaluateTimeline_();
    }

    if (camera_) {
        camera_->Update();
    }
    ApplyAnimationCamera_();
    ApplyCameraToEditorObjects_();
    if (GetSceneCamera_()) {
        ParticleManager::GetInstance()->Update(dt, *GetSceneCamera_());
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

    if (editorParticle_) {
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
    item.billboard = copiedObject_.billboard;
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
    dst.billboard = src.billboard;
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
    snapshot.billboard = item.billboard;
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
            return;
        }
    }

    item.keyframes.push_back({ timelineTime_, item.position, item.rotation, item.scale, item.color });
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

void ParticleTestScene::EvaluateTimeline_()
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
            ApplyEditorObjectTransform_(item);
            continue;
        }
        if (timelineTime_ >= item.keyframes.back().time) {
            item.position = item.keyframes.back().position;
            item.rotation = item.keyframes.back().rotation;
            item.scale = item.keyframes.back().scale;
            item.color = item.keyframes.back().color;
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
                ApplyEditorObjectTransform_(item);
                break;
            }
        }
    }
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
        object.billboard = item.billboard;
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
        item.billboard = src.billboard;
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
        object["geometryType"] = item.geometryType;
        object["position"] = { item.position.x, item.position.y, item.position.z };
        object["rotation"] = { item.rotation.x, item.rotation.y, item.rotation.z };
        object["scale"] = { item.scale.x, item.scale.y, item.scale.z };
        object["color"] = { item.color.x, item.color.y, item.color.z, item.color.w };
        object["billboard"] = item.billboard;
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
                { "color", { key.color.x, key.color.y, key.color.z, key.color.w } }
            });
        }
        root["objects"].push_back(std::move(object));
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
        object.geometryType = source.value("geometryType", -1);
        auto p = source.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = source.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = source.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        auto c = source.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }));
        object.position = { p[0], p[1], p[2] };
        object.rotation = { r[0], r[1], r[2] };
        object.scale = { s[0], s[1], s[2] };
        object.color = { c[0], c[1], c[2], c[3] };
        object.billboard = source.value("billboard", false);
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
            object.keyframes.push_back(key);
        }

        snapshot.nextObjectId = std::max(snapshot.nextObjectId, object.id + 1);
        snapshot.objects.push_back(std::move(object));
    }

    if (!snapshot.objects.empty()) {
        snapshot.selectedObject = 0;
    }
    RestoreEditorSnapshot_(app, snapshot);
    undoStack_.clear();
    redoStack_.clear();
}

bool ParticleTestScene::OpenModelFileDialog_()
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

    const std::string modelPath = ToResourceRelativeModelPath(std::filesystem::path(filePath));
    strncpy_s(editorModelPath_, sizeof(editorModelPath_), modelPath.c_str(), _TRUNCATE);
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
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selectedEditorObject_ >= 0) {
        RequestDeleteSelectedObject_();
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
    if (ImGui::Button("Delete") && selectedEditorObject_ >= 0) {
        RequestDeleteSelectedObject_();
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

    DrawEditorCameraControls_();
    DrawAnimationCameraControls_();
    ImGui::End();
#endif
}

void ParticleTestScene::DrawEffectEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGui::Begin("Effect Editor");

    if (selectedEditorObject_ >= 0 && selectedEditorObject_ < static_cast<int>(editorObjects_.size())) {
        EditorObject& item = editorObjects_[selectedEditorObject_];
        ImGui::Text("%s (%s)", item.name.c_str(), item.modelPath.c_str());
        if (ImGui::Button("Add / Replace Keyframe")) {
            PushUndoSnapshot_();
            AddKeyframeToSelected_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Near Keyframe")) {
            PushUndoSnapshot_();
            DeleteNearestKeyframeFromSelected_();
        }

        if (ImGui::BeginTable("Keyframes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Position");
            ImGui::TableSetupColumn("Rotation");
            ImGui::TableSetupColumn("Scale");
            ImGui::TableSetupColumn("Color");
            ImGui::TableHeadersRow();
            for (const auto& key : item.keyframes) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.3f", key.time);
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
    } else {
        ImGui::TextDisabled("Select a model in Hierarchy or Inspector.");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Animation Camera Keys");
    if (ImGui::BeginTable("CameraKeyframes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Rotation");
        ImGui::TableSetupColumn("FovY");
        ImGui::TableHeadersRow();
        for (const auto& key : cameraKeyframes_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%.3f", key.time);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f %.2f %.2f", key.position.x, key.position.y, key.position.z);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f %.2f %.2f", key.rotation.x, key.rotation.y, key.rotation.z);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", key.fovY);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Timeline");
    if (ImGui::Button(timelinePlaying_ ? "Stop" : "Play")) {
        timelinePlaying_ = !timelinePlaying_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        timelineTime_ = 0.0f;
        timelinePlaying_ = true;
        EvaluateTimeline_();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &timelineLoop_);
    ImGui::DragFloat("Duration", &timelineDuration_, 0.05f, 0.05f, 30.0f);
    if (ImGui::SliderFloat("Current Time", &timelineTime_, 0.0f, timelineDuration_)) {
        EvaluateTimeline_();
    }

    ImGui::End();
    return;

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
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selectedEditorObject_ >= 0) {
        RequestDeleteSelectedObject_();
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
    ImGui::SameLine();
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

    ImGui::TextUnformatted("Object Actions");
    if (ImGui::Button("Duplicate") && selectedEditorObject_ >= 0) {
        PushUndoSnapshot_();
        DuplicateSelectedObject_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy") && selectedEditorObject_ >= 0) {
        CopySelectedObject_();
    }
    ImGui::SameLine();
    if (ImGui::Button("Paste") && hasCopiedObject_) {
        PushUndoSnapshot_();
        PasteEditorObject_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selectedEditorObject_ >= 0) {
        RequestDeleteSelectedObject_();
    }
    ImGui::SameLine();
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

        changed |= ImGui::DragFloat3("Position", &item.position.x, 0.05f);
        trackDragEdit(changed);
        bool rotationChanged = ImGui::DragFloat3("Rotation", &item.rotation.x, 0.01f);
        changed |= rotationChanged;
        trackDragEdit(rotationChanged);
        bool scaleChanged = ImGui::DragFloat3("Scale", &item.scale.x, 0.05f, 0.01f, 100.0f);
        changed |= scaleChanged;
        trackDragEdit(scaleChanged);
        bool colorChanged = ImGui::ColorEdit4("Color / Alpha", &item.color.x);
        changed |= colorChanged;
        trackDragEdit(colorChanged);
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

        if (ImGui::Button("Add / Replace Keyframe")) {
            PushUndoSnapshot_();
            AddKeyframeToSelected_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Near Keyframe")) {
            PushUndoSnapshot_();
            DeleteNearestKeyframeFromSelected_();
        }

        if (ImGui::BeginTable("Keyframes", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("Position");
            ImGui::TableSetupColumn("Rotation");
            ImGui::TableSetupColumn("Scale");
            ImGui::TableSetupColumn("Color");
            ImGui::TableHeadersRow();
            for (const auto& key : item.keyframes) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.3f", key.time);
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
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Timeline");
    if (ImGui::Button(timelinePlaying_ ? "Stop" : "Play")) {
        timelinePlaying_ = !timelinePlaying_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        timelineTime_ = 0.0f;
        timelinePlaying_ = true;
        EvaluateTimeline_();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &timelineLoop_);
    ImGui::DragFloat("Duration", &timelineDuration_, 0.05f, 0.05f, 30.0f);
    if (ImGui::SliderFloat("Current Time", &timelineTime_, 0.0f, timelineDuration_)) {
        EvaluateTimeline_();
    }

    DrawEditorCameraControls_();

    ImGui::End();
#endif
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
            if (gParticleTestBlenderHierarchySelected >= 0 &&
                gParticleTestBlenderHierarchySelected < static_cast<int>(editorObjects_.size())) {
                selectedEditorObject_ = gParticleTestBlenderHierarchySelected;
            }
        }

        gParticleTestBlenderHierarchyNames.clear();
        gParticleTestBlenderHierarchyNames.reserve(editorObjects_.size());
        for (const auto& item : editorObjects_) {
            gParticleTestBlenderHierarchyNames.push_back(item.name + " (" + item.modelPath + ")");
        }
        gParticleTestBlenderHierarchySelected = selectedEditorObject_;
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
