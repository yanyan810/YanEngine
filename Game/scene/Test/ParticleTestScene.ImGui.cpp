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
    ImGui::SameLine();
    if (ImGui::Button("Add Weapon")) {
        PushUndoSnapshot_();
        AddEditorObject_(app, "Player/sword.obj");
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

        ImGui::Separator();
        ImGui::TextUnformatted("Attach To Bone");
        const bool attachBefore = item.attachToBone;
        if (ImGui::Checkbox("Enable Bone Attach", &item.attachToBone)) {
            item.attachToBone = attachBefore;
            PushUndoSnapshot_();
            item.attachToBone = !attachBefore;
        }

        const char* parentPreview = "None";
        for (const auto& parent : editorObjects_) {
            if (parent.id == item.attachParentId) {
                parentPreview = parent.name.c_str();
                break;
            }
        }
        if (ImGui::BeginCombo("Attach Parent", parentPreview)) {
            if (ImGui::Selectable("None", item.attachParentId < 0)) {
                PushUndoSnapshot_();
                item.attachParentId = -1;
            }
            for (const auto& parent : editorObjects_) {
                if (parent.id == item.id) {
                    continue;
                }
                const bool selected = parent.id == item.attachParentId;
                if (ImGui::Selectable(parent.name.c_str(), selected)) {
                    PushUndoSnapshot_();
                    item.attachParentId = parent.id;
                    if (item.attachJointName.empty() && !parent.bonePoses.empty()) {
                        item.attachJointName = parent.bonePoses.front().name;
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        EditorObject* attachParent = nullptr;
        for (auto& parent : editorObjects_) {
            if (parent.id == item.attachParentId && parent.id != item.id) {
                attachParent = &parent;
                break;
            }
        }
        if (attachParent) {
            SyncEditorObjectBones_(*attachParent);
            const char* bonePreview = item.attachJointName.empty() ? "Select Bone" : item.attachJointName.c_str();
            if (ImGui::BeginCombo("Attach Bone", bonePreview)) {
                for (const auto& bone : attachParent->bonePoses) {
                    const bool selected = bone.name == item.attachJointName;
                    if (ImGui::Selectable(bone.name.c_str(), selected)) {
                        PushUndoSnapshot_();
                        item.attachJointName = bone.name;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else if (item.attachToBone) {
            ImGui::TextDisabled("Select an attach parent object.");
        }

        char attachBoneBuf[128];
        strncpy_s(attachBoneBuf, sizeof(attachBoneBuf), item.attachJointName.c_str(), _TRUNCATE);
        if (ImGui::InputText("Attach Bone Name", attachBoneBuf, sizeof(attachBoneBuf))) {
            item.attachJointName = attachBoneBuf;
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

        bool attachOffsetChanged = ImGui::DragFloat3("Attach Offset", &item.attachOffset.x, 0.01f, -20.0f, 20.0f);
        trackDragEdit(attachOffsetChanged);
        bool attachRotationChanged = ImGui::DragFloat3("Attach Rotation", &item.attachRotation.x, 0.01f, -100.0f, 100.0f);
        trackDragEdit(attachRotationChanged);
        bool attachScaleChanged = ImGui::DragFloat3("Attach Scale", &item.attachScale.x, 0.01f, 0.001f, 20.0f);
        trackDragEdit(attachScaleChanged);

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

void ParticleTestScene::DrawPlayerAttackEditorImGui_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGui::Begin("PlayerAttack Editor");

    if (ImGui::Button("Create / Focus PlayerAttack Editor")) {
        EnsurePlayerAttackEditor_(app);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Draw HitBox", &drawPlayerAttackHitbox_);

    if (!playerAttackEditorEnabled_) {
        ImGui::TextDisabled("Create the editor to place a Player model and HitBox track.");
        ImGui::End();
        return;
    }

    ImGui::Text("Timeline: %.3f / %.3f", timelineTime_, timelineDuration_);
    if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
        ImGui::Text("Player Object: %s", editorObjects_[playerAttackObjectIndex_].name.c_str());
        if (ImGui::Button("Select Player Object")) {
            selectedEditorObject_ = playerAttackObjectIndex_;
            selectedParticleNode_ = -1;
        }
    }

    ImGui::SeparatorText("Current HitBox");
    bool changed = false;
    changed |= ImGui::Checkbox("Active", &currentPlayerAttackHitbox_.active);
    changed |= ImGui::DragFloat3("Offset", &currentPlayerAttackHitbox_.offset.x, 0.05f, -20.0f, 20.0f);
    changed |= ImGui::DragFloat3("Half Size", &currentPlayerAttackHitbox_.halfSize.x, 0.05f, 0.01f, 20.0f);
    if (changed && playerAttackHitboxCube_) {
        Vector3 playerBase{};
        if (playerAttackObjectIndex_ >= 0 && playerAttackObjectIndex_ < static_cast<int>(editorObjects_.size())) {
            playerBase = editorObjects_[playerAttackObjectIndex_].position;
        }
        playerAttackHitboxCube_->SetTranslate(playerBase + currentPlayerAttackHitbox_.offset);
        playerAttackHitboxCube_->SetScale(currentPlayerAttackHitbox_.halfSize);
    }

    if (ImGui::Button("Add / Replace HitBox Keyframe")) {
        AddPlayerAttackHitboxKeyframe_();
        EvaluateTimeline_(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Near HitBox Keyframe")) {
        DeleteNearestPlayerAttackHitboxKeyframe_();
        EvaluateTimeline_(false);
    }

    if (ImGui::BeginTable("PlayerAttackHitboxKeys", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Offset");
        ImGui::TableSetupColumn("Half Size");
        ImGui::TableSetupColumn("Use", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(playerAttackHitboxKeyframes_.size()); ++i) {
            auto& key = playerAttackHitboxKeyframes_[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(64.0f);
            if (ImGui::DragFloat("##time", &key.time, 0.01f, 0.0f, timelineDuration_, "%.2f")) {
                SortPlayerAttackHitboxKeyframes_();
                EvaluateTimeline_(false);
            }
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Checkbox("##active", &key.active)) {
                EvaluateTimeline_(false);
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f %.2f %.2f", key.offset.x, key.offset.y, key.offset.z);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f %.2f %.2f", key.halfSize.x, key.halfSize.y, key.halfSize.z);
            ImGui::TableSetColumnIndex(4);
            if (ImGui::Button("Set")) {
                currentPlayerAttackHitbox_ = key;
                timelineTime_ = key.time;
                RequestTimelineRebuild_(timelineTime_);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Bone Pose");
    ImGui::TextDisabled("Use Inspector > Bone Controls and the viewport bone handles for the selected Player object.");

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
    gParticleTestEditorMode = std::clamp(gParticleTestEditorMode, 0, 2);
    if (gParticleTestEditorMode == 2) {
        editorMode_ = EditorMode::PlayerAttack;
    } else {
        editorMode_ = gParticleTestEditorMode == 0 ? EditorMode::Blender : EditorMode::Particle;
    }

    const bool objectEditorMode = editorMode_ == EditorMode::Blender || editorMode_ == EditorMode::PlayerAttack;
    if (objectEditorMode) {
        if (editorMode_ == EditorMode::PlayerAttack) {
            EnsurePlayerAttackEditor_(app);
        }
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

    if (objectEditorMode) {
        DrawEffectInspectorImGui_(app);
        DrawEffectEditorImGui_(app);
        if (editorMode_ == EditorMode::PlayerAttack) {
            DrawPlayerAttackEditorImGui_(app);
        }
        DrawViewportBones_();
        DrawViewportGizmo_();
    } else {
        DrawParticleModeImGui_();
    }
#endif
}
