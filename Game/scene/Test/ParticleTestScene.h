#pragma once

#include "IScene.h"
#include "ParticleTestEditorTypes.h"
#include "Object3dCommon.h"
#include "Vector3.h"

#include <deque>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class Camera;
class Object3d;
class Particle;
class GameApp;

class ParticleTestScene : public IScene {
public:
    void OnEnter(GameApp& app) override;
    void OnExit(GameApp& app) override;
    void Update(GameApp& app, float dt) override;
    void DrawRender(GameApp& app) override;
    void Draw3D(GameApp& app) override;
    void Draw2D(GameApp& app) override;
    void Draw(GameApp& app) override;
    void DrawImGui(GameApp& app) override;
    void DrawPreview(GameApp& app) override;
    void DrawPostEffectTargets(GameApp& app) override;
    bool HasObjectBloomTargets() const override;
    bool HasObjectOutlineBloomTargets() const override;

private:
    using ParticleNode = ParticleTestEditor::ParticleNode;
    using EffectKeyframe = ParticleTestEditor::EffectKeyframe;
    using CameraKeyframe = ParticleTestEditor::CameraKeyframe;
    using PlayerAttackHitboxKeyframe = ParticleTestEditor::PlayerAttackHitboxKeyframe;
    using PlayerSpecialHitboxKeyframe = ParticleTestEditor::PlayerSpecialHitboxKeyframe;
    using PlayerSpecialMotionKeyframe = ParticleTestEditor::PlayerSpecialMotionKeyframe;
    using PlayerSpecialAnimationKeyframe = ParticleTestEditor::PlayerSpecialAnimationKeyframe;
    using PlayerSpecialEventKeyframe = ParticleTestEditor::PlayerSpecialEventKeyframe;
    using PlayerSpecialTimeline = ParticleTestEditor::PlayerSpecialTimeline;
    using EditorBonePose = ParticleTestEditor::EditorBonePose;
    using EditorObject = ParticleTestEditor::EditorObject;
    using EditorObjectSnapshot = ParticleTestEditor::EditorObjectSnapshot;
    using EditorSnapshot = ParticleTestEditor::EditorSnapshot;
    using GizmoMode = ParticleTestEditor::GizmoMode;
    using EditorMode = ParticleTestEditor::EditorMode;

    void EnsureHitEffectGroup_();
    void ReloadParticleJson_();
    void SpawnHitEffectPreview_();
    void AddEditorObject_(GameApp& app, const std::string& modelPath);
    void AddGeometryObject_(GameApp& app, int geometryType);
    void PasteEditorObject_(GameApp& app);
    void DuplicateSelectedObject_(GameApp& app);
    void RequestDeleteSelectedObject_();
    void DeleteSelectedObject_();
    void ApplyEditorObjectTransform_(EditorObject& item);
    Camera* GetSceneCamera_() const;
    Camera* GetPreviewCamera_() const;
    void ApplyCameraToEditorObjects_();
    void ApplyAnimationCamera_();
    void DrawSceneContent_(GameApp& app);
    void SyncEditorObjectBones_(EditorObject& item);
    void ApplyEditorObjectBonePose_(EditorObject& item);
    EditorObjectSnapshot CaptureSelectedObject_() const;
    void CopySelectedObject_();
    void AddKeyframeToSelected_();
    void AddKeyframeToObject_(EditorObject& item);
    void DeleteNearestKeyframeFromSelected_();
    void SortKeyframes_(EditorObject& item);
    void SortCameraKeyframes_();
    void EvaluateTimeline_(bool emitParticles = true);
    float GetParticleNodeDuration_(const ParticleNode& node) const;
    void EmitParticleNode_(const ParticleNode& node, float initialAge);
    void RequestTimelineRebuild_(float targetTime);
    void RebuildParticleTimeline_(float targetTime);
    void AddCameraKeyframe_();
    void DeleteNearestCameraKeyframe_();
    void EnsurePlayerAttackEditor_(GameApp& app);
    void AddPlayerAttackHitboxKeyframe_();
    void DeleteNearestPlayerAttackHitboxKeyframe_();
    void SortPlayerAttackHitboxKeyframes_();
    void EvaluatePlayerAttackHitbox_();
    void EnsurePlayerSpecialTimelineDefaults_();
    PlayerSpecialTimeline& CurrentPlayerSpecialTimeline_();
    const PlayerSpecialTimeline& CurrentPlayerSpecialTimeline_() const;
    void SortCurrentPlayerSpecialTimeline_();
    void EvaluatePlayerSpecialTimeline_();
    EditorSnapshot CaptureEditorSnapshot_() const;
    void RestoreEditorSnapshot_(GameApp& app, const EditorSnapshot& snapshot);
    void PushUndoSnapshot_(const EditorSnapshot& snapshot);
    void PushUndoSnapshot_();
    void Undo_(GameApp& app);
    void Redo_(GameApp& app);
    void SaveEffectJson_(const std::string& path) const;
    void LoadEffectJson_(GameApp& app, const std::string& path);
    std::string MakeEffectsJsonPath_(const std::string& path) const;
    bool OpenModelFileDialog_();
    bool OpenModelFileDialog_(std::string& outModelPath);
    bool OpenTextureFileDialog_(std::string& outTexturePath);
    bool OpenEffectJsonFileDialog_(bool saveDialog, std::string& outJsonPath);
    void HandleEffectEditorShortcuts_(GameApp& app);
    void EnsureUniqueModelForObject_(EditorObject& item);
    void SyncParticleModelsWithEditorObjects_();
    void UpdateCameraControls_(GameApp& app, float dt);
    void ApplyVertexOffsets_(EditorObject& item);
    std::unordered_map<uint32_t, Vector3> LerpVertexOffsets_(const EditorObject& item, const EffectKeyframe& a, const EffectKeyframe& b, float t) const;
    void UpdateVertexPositionGroup_(EditorObject& item, int baseVertexIndex, const Vector3& localDelta);
    void MoveSelectedVertices_(EditorObject& item, const Vector3& localDelta);
    void DrawEffectInspectorImGui_(GameApp& app);
    void DrawPlayerAttackInspectorImGui_(GameApp& app);
    void DrawAnimationCameraControls_();
    void DrawGizmoControls_(EditorObject& item);
    void DrawBoneControls_(EditorObject& item);
    void DrawViewportBones_();
    void DrawViewportGizmo_(GameApp& app);
    void DrawEditorCameraControls_();
    void DrawEffectEditorImGui_(GameApp& app);
    void DrawParticleModeImGui_();
    void DrawPlayerAttackEditorImGui_(GameApp& app);

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Camera> animationCamera_;
    std::unique_ptr<Object3d> ground_;
    std::unique_ptr<Object3d> playerAttackHitboxCube_;
    std::unique_ptr<Particle> editorParticle_;
    std::vector<EditorObject> editorObjects_;
    std::deque<EditorSnapshot> undoStack_;
    std::deque<EditorSnapshot> redoStack_;
    EditorObjectSnapshot copiedObject_{};
    bool hasCopiedObject_ = false;

    char hitEffectGroupName_[64] = "HitEffect";
    char editorModelPath_[128] = "cube/cube.obj";
    char effectJsonPath_[128] = "resources/effects/effect_editor.json";
    int hitEffectSpawnCount_ = 24;
    int selectedGeometryType_ = 1;
    int selectedEditorObject_ = -1;
    int nextEditorObjectId_ = 1;
    Vector3 hitEffectSpawnPosition_{ 0.0f, 1.0f, 0.0f };
    float timelineTime_ = 0.0f;
    float timelineDuration_ = 1.0f;
    float timelineViewStart_ = 0.0f;
    float timelineViewDuration_ = 1.0f;
    bool timelinePlaying_ = false;
    bool timelineLoop_ = true;
    bool useAnimationCameraPreview_ = false;
    bool animationCameraPreviewSwapped_ = false;
    std::vector<CameraKeyframe> cameraKeyframes_;
    std::vector<PlayerAttackHitboxKeyframe> playerAttackHitboxKeyframes_;
    PlayerAttackHitboxKeyframe currentPlayerAttackHitbox_{};
    PlayerAttackHitboxKeyframe previewPlayerAttackHitbox_{};
    std::array<PlayerSpecialTimeline, 4> sideSpecialTimelines_{};
    PlayerSpecialHitboxKeyframe currentSpecialHitbox_{};
    PlayerSpecialMotionKeyframe currentSpecialMotion_{};
    PlayerSpecialAnimationKeyframe currentSpecialAnimation_{};
    PlayerSpecialEventKeyframe currentSpecialEvent_{};
    bool playerAttackEditorEnabled_ = false;
    bool drawPlayerAttackHitbox_ = true;
    int playerAttackObjectIndex_ = -1;
    int selectedSideSpecialLevel_ = 2;
    EditorMode editorMode_ = EditorMode::Blender;
    GizmoMode gizmoMode_ = GizmoMode::Translate;
    Vector3 editorCameraPosition_{ 0.0f, 3.0f, -20.0f };
    Vector3 editorCameraRotation_{ 0.0f, 0.0f, 0.0f };
    Vector3 animationCameraPosition_{ 0.0f, 3.0f, -12.0f };
    Vector3 animationCameraRotation_{ 0.0f, 0.0f, 0.0f };
    float animationCameraFovY_ = 0.45f;
    float editorCameraMoveSpeed_ = 0.2f;
    float editorCameraLookSpeed_ = 0.006f;
    bool editorCameraControlActive_ = false;
    bool editorCameraPanActive_ = false;
    bool shiftMovementActive_ = false;
    bool boxSelectActive_ = false;
    ImVec2 boxSelectStart_{};
    bool transformDragActive_ = false;
    bool transformDragChanged_ = false;
    EditorSnapshot transformDragBefore_{};
    int activeViewportGizmoAxis_ = -1;
    int activeViewportBone_ = -1;
    float viewportGizmoLastMouseX_ = 0.0f;
    float viewportGizmoLastMouseY_ = 0.0f;
    float viewportBoneLastMouseX_ = 0.0f;
    float viewportBoneLastMouseY_ = 0.0f;
    bool viewportBoneDragActive_ = false;
    bool viewportBoneDragChanged_ = false;
    bool pendingDeleteSelectedObject_ = false;
    float reloadCooldown_ = 0.0f;
    std::vector<ParticleNode> particleNodes_;
    int selectedParticleNode_ = -1;
    float lastTimelineTime_ = 0.0f;
    float previousTimelineTime_ = 0.0f;
    bool pendingTimelineRebuild_ = false;
    float pendingTimelineRebuildTime_ = 0.0f;
    EditorMode lastEditorMode_ = EditorMode::Blender;
    enum class DragTarget {
        None = 0,
        TimelineTime,
        ModelKeyframe,
        CameraKeyframe,
        PlayerAttackHitboxKeyframe,
        ParticleNodeStart,
        ParticleNodeEnd,
        ParticleNodeBar,
    };
    DragTarget dragTarget_ = DragTarget::None;
    int dragObjectIndex_ = -1;
    int dragKeyframeIndex_ = -1;
    int dragParticleNodeIndex_ = -1;
    float dragStartOffset_ = 0.0f;
    float dragStartVal1_ = 0.0f;
    float dragStartVal2_ = 0.0f;
    int selectedKeyframeIndex_ = -1;
    DragTarget selectedKeyframeType_ = DragTarget::None;
    int selectedKeyframeObjectIndex_ = -1;

    void DrawDopeSheet_(GameApp& app);
    bool OpenParticleFileDialog_(std::vector<std::string>& outGroupNames, std::string& outFileName);
};
