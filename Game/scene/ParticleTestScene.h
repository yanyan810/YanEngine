#pragma once

#include "IScene.h"
#include "Vector3.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

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

private:
    struct EffectKeyframe {
        float time = 0.0f;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct CameraKeyframe {
        float time = 0.0f;
        Vector3 position{ 0.0f, 3.0f, -12.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        float fovY = 0.45f;
    };

    struct EditorBonePose {
        std::string name;
        Vector3 translate{ 0.0f, 0.0f, 0.0f };
        Vector3 rotate{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct EditorObject {
        int id = 0;
        std::string name;
        std::string modelPath;
        int geometryType = -1;
        std::unique_ptr<Object3d> object;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool billboard = false;
        bool showBones = false;
        int selectedBone = 0;
        std::vector<EditorBonePose> bonePoses;
        std::vector<EffectKeyframe> keyframes;
    };

    struct EditorObjectSnapshot {
        int id = 0;
        std::string name;
        std::string modelPath;
        int geometryType = -1;
        Vector3 position{ 0.0f, 0.0f, 0.0f };
        Vector3 rotation{ 0.0f, 0.0f, 0.0f };
        Vector3 scale{ 1.0f, 1.0f, 1.0f };
        Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool billboard = false;
        bool showBones = false;
        int selectedBone = 0;
        std::vector<EditorBonePose> bonePoses;
        std::vector<EffectKeyframe> keyframes;
    };

    struct EditorSnapshot {
        std::vector<EditorObjectSnapshot> objects;
        int selectedObject = -1;
        int nextObjectId = 1;
        float timelineTime = 0.0f;
        float timelineDuration = 1.0f;
        bool timelineLoop = true;
        Vector3 animationCameraPosition{ 0.0f, 3.0f, -12.0f };
        Vector3 animationCameraRotation{ 0.0f, 0.0f, 0.0f };
        float animationCameraFovY = 0.45f;
        bool useAnimationCameraPreview = false;
        bool animationCameraPreviewSwapped = false;
        std::vector<CameraKeyframe> cameraKeyframes;
    };

    enum class GizmoMode {
        Translate = 0,
        Rotate,
        Scale,
    };

    enum class EditorMode {
        Blender = 0,
        Particle,
    };

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
    void DeleteNearestKeyframeFromSelected_();
    void SortKeyframes_(EditorObject& item);
    void SortCameraKeyframes_();
    void EvaluateTimeline_();
    void AddCameraKeyframe_();
    void DeleteNearestCameraKeyframe_();
    EditorSnapshot CaptureEditorSnapshot_() const;
    void RestoreEditorSnapshot_(GameApp& app, const EditorSnapshot& snapshot);
    void PushUndoSnapshot_(const EditorSnapshot& snapshot);
    void PushUndoSnapshot_();
    void Undo_(GameApp& app);
    void Redo_(GameApp& app);
    void SaveEffectJson_(const std::string& path) const;
    void LoadEffectJson_(GameApp& app, const std::string& path);
    bool OpenModelFileDialog_();
    void HandleEffectEditorShortcuts_(GameApp& app);
    void DrawEffectInspectorImGui_(GameApp& app);
    void DrawAnimationCameraControls_();
    void DrawGizmoControls_(EditorObject& item);
    void DrawBoneControls_(EditorObject& item);
    void DrawViewportBones_();
    void DrawViewportGizmo_();
    void DrawEditorCameraControls_();
    void DrawEffectEditorImGui_(GameApp& app);
    void DrawParticleModeImGui_();

    std::unique_ptr<Camera> camera_;
    std::unique_ptr<Camera> animationCamera_;
    std::unique_ptr<Object3d> ground_;
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
    bool timelinePlaying_ = false;
    bool timelineLoop_ = true;
    bool useAnimationCameraPreview_ = false;
    bool animationCameraPreviewSwapped_ = false;
    std::vector<CameraKeyframe> cameraKeyframes_;
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
};
