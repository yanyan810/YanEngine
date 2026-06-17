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

#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <Windows.h>

using json = nlohmann::json;

namespace {
constexpr const char* kParticleJson = "test_particles.json";
constexpr size_t kMaxUndoCount = 64;
constexpr float kPi = 3.14159265358979323846f;

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
}

void ParticleTestScene::OnExit(GameApp&)
{
    ParticleManager::GetInstance()->ClearGroups();
    editorObjects_.clear();
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
        ParticleManager::GetInstance()->Update(dt, *camera_);
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
    if (ground_) {
        ground_->Draw();
    }

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
}

#ifdef USE_IMGUI
extern ImVec2 gSceneImageMin;
extern ImVec2 gSceneImageMax;
extern bool gHasSceneImageRect;
#endif

void ParticleTestScene::AddEditorObject_(GameApp& app, const std::string& modelPath)
{
    EditorObject item;
    item.id = nextEditorObjectId_++;
    char name[64]{};
    std::snprintf(name, sizeof(name), "EffectObject_%02d", item.id);
    item.name = name;
    item.modelPath = modelPath.empty() ? "cube/cube.obj" : modelPath;
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(camera_.get());
    item.object->SetModel(item.modelPath);
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
    item.position = copiedObject_.position + Vector3{ 0.5f, 0.0f, 0.0f };
    item.rotation = copiedObject_.rotation;
    item.scale = copiedObject_.scale;
    item.color = copiedObject_.color;
    item.billboard = copiedObject_.billboard;
    item.keyframes = copiedObject_.keyframes;
    for (auto& key : item.keyframes) {
        key.position += Vector3{ 0.5f, 0.0f, 0.0f };
    }
    item.object = std::make_unique<Object3d>();
    item.object->Initialize(app.ObjCom(), app.Dx());
    item.object->SetCamera(camera_.get());
    item.object->SetModel(item.modelPath);
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

    const EditorObject& src = editorObjects_[selectedEditorObject_];
    AddEditorObject_(app, src.modelPath);
    EditorObject& dst = editorObjects_.back();
    dst.position = src.position + Vector3{ 0.5f, 0.0f, 0.0f };
    dst.rotation = src.rotation;
    dst.scale = src.scale;
    dst.color = src.color;
    dst.billboard = src.billboard;
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
    snapshot.position = item.position;
    snapshot.rotation = item.rotation;
    snapshot.scale = item.scale;
    snapshot.color = item.color;
    snapshot.billboard = item.billboard;
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
    item.object->SetTranslate(item.position);
    if (item.billboard && camera_) {
        item.object->SetRotate(camera_->GetRotate());
    } else {
        item.object->SetRotate(item.rotation);
    }
    item.object->SetScale(item.scale);
    item.object->SetMaterialColor(item.color);
}

void ParticleTestScene::SortKeyframes_(EditorObject& item)
{
    std::sort(item.keyframes.begin(), item.keyframes.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) {
        return a.time < b.time;
    });
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
    snapshot.objects.reserve(editorObjects_.size());
    for (const auto& item : editorObjects_) {
        EditorObjectSnapshot object;
        object.id = item.id;
        object.name = item.name;
        object.modelPath = item.modelPath;
        object.position = item.position;
        object.rotation = item.rotation;
        object.scale = item.scale;
        object.color = item.color;
        object.billboard = item.billboard;
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
        item.position = src.position;
        item.rotation = src.rotation;
        item.scale = src.scale;
        item.color = src.color;
        item.billboard = src.billboard;
        item.keyframes = src.keyframes;
        item.object = std::make_unique<Object3d>();
        item.object->Initialize(app.ObjCom(), app.Dx());
        item.object->SetCamera(camera_.get());
        item.object->SetModel(item.modelPath);
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
    root["objects"] = json::array();
    for (const auto& item : editorObjects_) {
        json object;
        object["id"] = item.id;
        object["name"] = item.name;
        object["modelPath"] = item.modelPath;
        object["position"] = { item.position.x, item.position.y, item.position.z };
        object["rotation"] = { item.rotation.x, item.rotation.y, item.rotation.z };
        object["scale"] = { item.scale.x, item.scale.y, item.scale.z };
        object["color"] = { item.color.x, item.color.y, item.color.z, item.color.w };
        object["billboard"] = item.billboard;
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

    for (const auto& source : root.value("objects", json::array())) {
        EditorObjectSnapshot object;
        object.id = source.value("id", snapshot.nextObjectId);
        object.name = source.value("name", std::string("EffectObject"));
        object.modelPath = source.value("modelPath", std::string("cube/cube.obj"));
        auto p = source.value("position", json::array({ 0.0f, 0.0f, 0.0f }));
        auto r = source.value("rotation", json::array({ 0.0f, 0.0f, 0.0f }));
        auto s = source.value("scale", json::array({ 1.0f, 1.0f, 1.0f }));
        auto c = source.value("color", json::array({ 1.0f, 1.0f, 1.0f, 1.0f }));
        object.position = { p[0], p[1], p[2] };
        object.rotation = { r[0], r[1], r[2] };
        object.scale = { s[0], s[1], s[2] };
        object.color = { c[0], c[1], c[2], c[3] };
        object.billboard = source.value("billboard", false);

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

void ParticleTestScene::DrawViewportGizmo_()
{
#ifdef USE_IMGUI
    if (!gHasSceneImageRect || !camera_) {
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
        const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
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

    ImVec2 center{};
    if (!project(item.position, center)) {
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
        if (!project(item.position + axisWorld[axis] * worldHandleLength, projectedEnd)) {
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

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInsideScene && activeViewportGizmoAxis_ < 0) {
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
                    if (axis == 0) item.position.x += amount;
                    if (axis == 1) item.position.y += amount;
                    if (axis == 2) item.position.z += amount;
                } else if (gizmoMode_ == GizmoMode::Rotate) {
                    if (axis == 0) item.rotation.x += amount * 0.35f;
                    if (axis == 1) item.rotation.y += amount * 0.35f;
                    if (axis == 2) item.rotation.z += amount * 0.35f;
                } else {
                    if (axis == 0) item.scale.x = std::max(0.01f, item.scale.x + amount);
                    if (axis == 1) item.scale.y = std::max(0.01f, item.scale.y + amount);
                    if (axis == 2) item.scale.z = std::max(0.01f, item.scale.z + amount);
                }
                transformDragChanged_ = true;
                ApplyEditorObjectTransform_(item);
            }
        } else if (axis == 3) {
            const float amountX = delta.x / 55.0f;
            const float amountY = -delta.y / 55.0f;
            if (gizmoMode_ == GizmoMode::Translate) {
                const Matrix4x4& cameraWorld = camera_->GetWorldMatrix();
                item.position += CameraRight(cameraWorld) * amountX;
                item.position += CameraUp(cameraWorld) * amountY;
            } else if (gizmoMode_ == GizmoMode::Scale) {
                const float amount = (amountX + amountY) * 0.5f;
                item.scale.x = std::max(0.01f, item.scale.x + amount);
                item.scale.y = std::max(0.01f, item.scale.y + amount);
                item.scale.z = std::max(0.01f, item.scale.z + amount);
            } else {
                item.rotation.y += amountX * 0.35f;
                item.rotation.x += amountY * 0.35f;
            }
            transformDragChanged_ = true;
            ApplyEditorObjectTransform_(item);
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
    drawList->AddCircleFilled(center, activeViewportGizmoAxis_ == 3 ? 9.0f : 7.0f, IM_COL32(255, 255, 255, 230));
    drawList->AddCircle(center, gizmoMode_ == GizmoMode::Rotate ? 46.0f : 14.0f, IM_COL32(255, 255, 255, 180), 48, 2.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const float thickness = activeViewportGizmoAxis_ == axis ? 5.0f : 3.0f;
        drawList->AddLine(center, axisEnd[axis], axisColor[axis], thickness);
        drawList->AddCircleFilled(axisEnd[axis], gizmoMode_ == GizmoMode::Scale ? 7.0f : 5.0f, axisColor[axis]);
    }
    drawList->AddText(ImVec2(center.x + 12.0f, center.y + 12.0f), IM_COL32(255, 255, 255, 230), modeText);
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

void ParticleTestScene::DrawEffectEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGui::Begin("Effect Editor");

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
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        gizmoMode_ = GizmoMode::Translate;
    }
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        gizmoMode_ = GizmoMode::Rotate;
    }
    if (!rightCameraDrag && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
        gizmoMode_ = GizmoMode::Scale;
    }

    ImGui::InputText("Model Path", editorModelPath_, sizeof(editorModelPath_));
    if (ImGui::Button("Add Model")) {
        PushUndoSnapshot_();
        AddEditorObject_(app, editorModelPath_);
    }
    ImGui::SameLine();
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
        PushUndoSnapshot_();
        DeleteSelectedObject_();
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

void ParticleTestScene::DrawImGui(GameApp& app)
{
#ifdef USE_IMGUI
    DrawEffectEditorImGui_(app);
    DrawViewportGizmo_();

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
