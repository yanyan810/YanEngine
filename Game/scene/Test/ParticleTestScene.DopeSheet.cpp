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
void ParticleTestScene::DrawDopeSheet_(GameApp& app)
{
#ifdef USE_IMGUI
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    
    int trackCount = 1; // Camera
    if (playerAttackEditorEnabled_) {
        ++trackCount;
    }
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

    timelineViewDuration_ = std::clamp(
        timelineViewDuration_ <= 0.0f ? timelineDuration_ : timelineViewDuration_,
        std::min(0.05f, timelineDuration_),
        timelineDuration_);
    timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
    
    auto timeToX = [&](float t) -> float {
        if (timelineViewDuration_ <= 0.0f) return timelineStartX;
        return timelineStartX + ((t - timelineViewStart_) / timelineViewDuration_) * timelineWidth;
    };
    
    auto xToTime = [&](float x) -> float {
        if (timelineWidth <= 0.0f) return 0.0f;
        float t = timelineViewStart_ + ((x - timelineStartX) / timelineWidth) * timelineViewDuration_;
        return std::clamp(t, 0.0f, timelineDuration_);
    };

    const bool mouseOverTimeline =
        io.MousePos.x >= timelineStartX && io.MousePos.x <= timelineEndX &&
        io.MousePos.y >= canvasPos.y && io.MousePos.y <= canvasPos.y + canvasSize.y;
    if (mouseOverTimeline && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && std::abs(io.MouseWheel) > 0.001f) {
        const float mouseRatio = std::clamp((io.MousePos.x - timelineStartX) / timelineWidth, 0.0f, 1.0f);
        // マウス位置での現在時間（ズーム前）
        const float mouseTime = timelineViewStart_ + mouseRatio * timelineViewDuration_;
        
        const float zoomFactor = io.MouseWheel > 0.0f ? 0.8f : 1.25f;
        float nextViewDuration = std::clamp(timelineViewDuration_ * zoomFactor, std::min(0.05f, timelineDuration_), timelineDuration_);
        
        // マウス位置を不変点とする新しいタイムライン開始位置
        timelineViewStart_ = mouseTime - mouseRatio * nextViewDuration;
        timelineViewDuration_ = nextViewDuration;
        
        timelineViewStart_ = std::clamp(timelineViewStart_, 0.0f, std::max(0.0f, timelineDuration_ - timelineViewDuration_));
    }
    
    // ヘッダー背景
    drawList->AddRectFilled(canvasPos, ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_HeaderActive));
    drawList->AddLine(ImVec2(canvasPos.x, canvasPos.y + headerHeight), ImVec2(timelineEndX, canvasPos.y + headerHeight), ImGui::GetColorU32(ImGuiCol_Border));
    
    // タイムライン描画領域をクリップ（ラベル領域へのはみ出しを防ぐ）
    drawList->PushClipRect(ImVec2(timelineStartX, canvasPos.y), ImVec2(timelineEndX, canvasPos.y + totalHeight), true);

    // グリッド線の描画 (表示幅に応じて適切な時間ステップを自動計算)
    float minPixelStep = 60.0f; // グリッドテキスト同士が重ならない最低間隔
    float maxGrids = timelineWidth / minPixelStep;
    if (maxGrids < 1.0f) maxGrids = 1.0f;
    
    float rawStep = timelineViewDuration_ / maxGrids;
    float gridStep = 1.0f;
    if (rawStep < 0.01f) gridStep = 0.01f;
    else if (rawStep < 0.02f) gridStep = 0.02f;
    else if (rawStep < 0.05f) gridStep = 0.05f;
    else if (rawStep < 0.1f)  gridStep = 0.1f;
    else if (rawStep < 0.2f)  gridStep = 0.2f;
    else if (rawStep < 0.5f)  gridStep = 0.5f;
    else if (rawStep < 1.0f)  gridStep = 1.0f;
    else if (rawStep < 2.0f)  gridStep = 2.0f;
    else if (rawStep < 5.0f)  gridStep = 5.0f;
    else gridStep = 10.0f;

    // キリの良い開始時間を決定
    float firstGridTime = std::ceil(timelineViewStart_ / gridStep) * gridStep;
    for (float t = firstGridTime; t <= timelineViewStart_ + timelineViewDuration_ + 0.0001f; t += gridStep) {
        if (t < timelineViewStart_ || t > timelineViewStart_ + timelineViewDuration_) {
            continue;
        }
        float gridX = timeToX(t);
        drawList->AddLine(ImVec2(gridX, canvasPos.y), ImVec2(gridX, canvasPos.y + totalHeight), ImGui::GetColorU32(ImGuiCol_Border, 0.3f));
        
        char buf[32];
        if (gridStep < 0.1f) {
            sprintf_s(buf, "%.3fs", t);
        } else if (gridStep < 1.0f) {
            sprintf_s(buf, "%.2fs", t);
        } else {
            sprintf_s(buf, "%.1fs", t);
        }
        drawList->AddText(ImVec2(gridX + 2.0f, canvasPos.y + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), buf);
    }
    
    float currentY = canvasPos.y + headerHeight;
    ImVec2 mousePos = io.MousePos;
    bool clicked = ImGui::IsMouseClicked(0);
    
    // 1. Camera Track
    bool anyKeyframeClicked = false;
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
                selectedKeyframeType_ = DragTarget::CameraKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = -1;
                anyKeyframeClicked = true;
            }
            
            bool isSelected = (selectedKeyframeType_ == DragTarget::CameraKeyframe && selectedKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelected ? ImColor(255, 200, 0) : ImColor(200, 200, 200));
        }
        currentY += trackHeight;
    }

    if (playerAttackEditorEnabled_) {
        drawList->AddRectFilled(ImVec2(canvasPos.x, currentY), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
        drawList->AddText(ImVec2(canvasPos.x + 6.0f, currentY + 4.0f), ImGui::GetColorU32(ImGuiCol_Text), "Player HitBox");
        drawList->AddLine(ImVec2(canvasPos.x, currentY + trackHeight), ImVec2(timelineEndX, currentY + trackHeight), ImGui::GetColorU32(ImGuiCol_Border));

        for (int i = 0; i < static_cast<int>(playerAttackHitboxKeyframes_.size()); ++i) {
            float kx = timeToX(playerAttackHitboxKeyframes_[i].time);
            ImVec2 center(kx, currentY + trackHeight * 0.5f);
            bool hovered = (std::abs(mousePos.x - center.x) <= 6.0f && std::abs(mousePos.y - center.y) <= 6.0f);
            if (hovered && clicked && dragTarget_ == DragTarget::None) {
                dragTarget_ = DragTarget::PlayerAttackHitboxKeyframe;
                dragKeyframeIndex_ = i;
                selectedKeyframeType_ = DragTarget::PlayerAttackHitboxKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = -1;
                anyKeyframeClicked = true;
            }

            const bool isSelected = selectedKeyframeType_ == DragTarget::PlayerAttackHitboxKeyframe && selectedKeyframeIndex_ == i;
            const ImColor color = playerAttackHitboxKeyframes_[i].active
                ? (isSelected ? ImColor(255, 200, 0) : ImColor(70, 210, 100))
                : ImColor(110, 110, 110);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), color);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), color);
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
                selectedKeyframeType_ = DragTarget::ModelKeyframe;
                selectedKeyframeIndex_ = i;
                selectedKeyframeObjectIndex_ = objIdx;
                anyKeyframeClicked = true;
            }
            
            bool isSelectedKey = (selectedKeyframeType_ == DragTarget::ModelKeyframe && selectedKeyframeObjectIndex_ == objIdx && selectedKeyframeIndex_ == i);
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x + 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
            drawList->AddTriangleFilled(ImVec2(center.x, center.y - 6.0f), ImVec2(center.x - 6.0f, center.y), ImVec2(center.x, center.y + 6.0f), isSelectedKey ? ImColor(255, 200, 0) : ImColor(200, 150, 50));
        }
        currentY += trackHeight;
    }

    // Dope Sheet背景がクリックされたかつ、キーフレームがどれもクリックされていなかったら選択解除
    if (clicked && !anyKeyframeClicked && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        selectedKeyframeType_ = DragTarget::None;
        selectedKeyframeIndex_ = -1;
        selectedKeyframeObjectIndex_ = -1;
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
    
    // クリッピングを終了
    drawList->PopClipRect();
    
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

        case DragTarget::PlayerAttackHitboxKeyframe:
            if (dragKeyframeIndex_ >= 0 && dragKeyframeIndex_ < static_cast<int>(playerAttackHitboxKeyframes_.size())) {
                playerAttackHitboxKeyframes_[dragKeyframeIndex_].time = mouseT;
                SortPlayerAttackHitboxKeyframes_();
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
        } else if (dragTarget_ == DragTarget::PlayerAttackHitboxKeyframe) {
            SortPlayerAttackHitboxKeyframes_();
            EvaluateTimeline_(false);
        }
        dragTarget_ = DragTarget::None;
    }
    
    ImGui::EndChild();
#endif
}

