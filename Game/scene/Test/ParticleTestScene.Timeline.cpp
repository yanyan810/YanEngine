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

void ParticleTestScene::AddPlayerAttackHitboxKeyframe_()
{
    for (auto& key : playerAttackHitboxKeyframes_) {
        if (std::abs(key.time - timelineTime_) < 0.001f) {
            key = currentPlayerAttackHitbox_;
            key.time = timelineTime_;
            SortPlayerAttackHitboxKeyframes_();
            return;
        }
    }

    currentPlayerAttackHitbox_.time = timelineTime_;
    playerAttackHitboxKeyframes_.push_back(currentPlayerAttackHitbox_);
    SortPlayerAttackHitboxKeyframes_();
}

void ParticleTestScene::DeleteNearestPlayerAttackHitboxKeyframe_()
{
    if (playerAttackHitboxKeyframes_.empty()) {
        return;
    }

    auto it = std::min_element(playerAttackHitboxKeyframes_.begin(), playerAttackHitboxKeyframes_.end(), [this](const PlayerAttackHitboxKeyframe& a, const PlayerAttackHitboxKeyframe& b) {
        return std::abs(a.time - timelineTime_) < std::abs(b.time - timelineTime_);
    });
    if (it != playerAttackHitboxKeyframes_.end() && std::abs(it->time - timelineTime_) <= 0.05f) {
        playerAttackHitboxKeyframes_.erase(it);
    }
}

void ParticleTestScene::SortPlayerAttackHitboxKeyframes_()
{
    std::sort(playerAttackHitboxKeyframes_.begin(), playerAttackHitboxKeyframes_.end(), [](const PlayerAttackHitboxKeyframe& a, const PlayerAttackHitboxKeyframe& b) {
        return a.time < b.time;
    });
}

void ParticleTestScene::EvaluatePlayerAttackHitbox_()
{
    if (playerAttackHitboxKeyframes_.empty()) {
        return;
    }

    SortPlayerAttackHitboxKeyframes_();
    if (timelineTime_ <= playerAttackHitboxKeyframes_.front().time) {
        currentPlayerAttackHitbox_ = playerAttackHitboxKeyframes_.front();
        return;
    }
    if (timelineTime_ >= playerAttackHitboxKeyframes_.back().time) {
        currentPlayerAttackHitbox_ = playerAttackHitboxKeyframes_.back();
        return;
    }

    for (size_t i = 0; i + 1 < playerAttackHitboxKeyframes_.size(); ++i) {
        const auto& a = playerAttackHitboxKeyframes_[i];
        const auto& b = playerAttackHitboxKeyframes_[i + 1];
        if (timelineTime_ >= a.time && timelineTime_ <= b.time) {
            const float range = std::max(0.001f, b.time - a.time);
            const float t = (timelineTime_ - a.time) / range;
            currentPlayerAttackHitbox_.time = timelineTime_;
            currentPlayerAttackHitbox_.offset = LerpVector3(a.offset, b.offset, t);
            currentPlayerAttackHitbox_.halfSize = LerpVector3(a.halfSize, b.halfSize, t);
            currentPlayerAttackHitbox_.active = a.active;
            return;
        }
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
            key.bonePoses = item.bonePoses;
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
        item.outlineBloomColor,
        item.bonePoses
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
    EvaluatePlayerAttackHitbox_();

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
            if (!item.keyframes.front().bonePoses.empty()) {
                item.bonePoses = item.keyframes.front().bonePoses;
                ApplyEditorObjectBonePose_(item);
            }
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
            if (!item.keyframes.back().bonePoses.empty()) {
                item.bonePoses = item.keyframes.back().bonePoses;
                ApplyEditorObjectBonePose_(item);
            }
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
                if (!a.bonePoses.empty() && a.bonePoses.size() == b.bonePoses.size()) {
                    item.bonePoses = a.bonePoses;
                    for (size_t boneIndex = 0; boneIndex < item.bonePoses.size(); ++boneIndex) {
                        item.bonePoses[boneIndex].translate = LerpVector3(a.bonePoses[boneIndex].translate, b.bonePoses[boneIndex].translate, t);
                        item.bonePoses[boneIndex].rotate = LerpVector3(a.bonePoses[boneIndex].rotate, b.bonePoses[boneIndex].rotate, t);
                        item.bonePoses[boneIndex].scale = LerpVector3(a.bonePoses[boneIndex].scale, b.bonePoses[boneIndex].scale, t);
                    }
                    ApplyEditorObjectBonePose_(item);
                }
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
        object.attachToBone = item.attachToBone;
        object.attachParentId = item.attachParentId;
        object.attachJointName = item.attachJointName;
        object.attachOffset = item.attachOffset;
        object.attachRotation = item.attachRotation;
        object.attachScale = item.attachScale;
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
        item.attachToBone = src.attachToBone;
        item.attachParentId = src.attachParentId;
        item.attachJointName = src.attachJointName;
        item.attachOffset = src.attachOffset;
        item.attachRotation = src.attachRotation;
        item.attachScale = src.attachScale;
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
    if (editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack) {
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

