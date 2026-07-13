#pragma once

#include "Object3dCommon.h"
#include "Vector3.h"

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class Object3d;

namespace ParticleTestEditor {

struct ParticleNode {
    std::string name;
    std::string particleFileName;
    float startTime = 0.5f;
    float endTime = 1.0f;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Vector3 rotation{ 0.0f, 0.0f, 0.0f };
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    int emitCount = 10;
    float presetDuration = 1.0f;
    bool hasEmitted = false;
};

struct EditorBonePose {
    std::string name;
    Vector3 translate{ 0.0f, 0.0f, 0.0f };
    Vector3 rotate{ 0.0f, 0.0f, 0.0f };
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
};

struct EffectKeyframe {
    float time = 0.0f;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Vector3 rotation{ 0.0f, 0.0f, 0.0f };
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool bloomPostEffect = false;
    bool outlineBloomPostEffect = false;
    Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    std::vector<EditorBonePose> bonePoses;
    std::unordered_map<uint32_t, Vector3> vertexOffsets;
    int interpolationType = 0;
};

struct CameraKeyframe {
    float time = 0.0f;
    Vector3 position{ 0.0f, 3.0f, -12.0f };
    Vector3 rotation{ 0.0f, 0.0f, 0.0f };
    float fovY = 0.45f;
};

struct PlayerAttackHitboxKeyframe {
    float time = 0.0f;
    Vector3 offset{ 1.0f, 1.0f, 0.0f };
    Vector3 halfSize{ 0.6f, 0.8f, 0.5f };
    bool active = true;
};

struct PlayerSpecialHitboxKeyframe {
    float time = 0.0f;
    float duration = 0.08f;
    Vector3 offset{ 1.0f, 1.0f, 0.0f };
    Vector3 halfSize{ 0.6f, 0.8f, 0.5f };
    int damage = 12;
    bool active = true;
    bool multiHit = false;
};

struct PlayerSpecialMotionKeyframe {
    float time = 0.0f;
    float duration = 0.10f;
    Vector3 velocity{ 0.0f, 0.0f, 0.0f };
    bool lockVelocity = false;
};

struct PlayerSpecialAnimationKeyframe {
    float time = 0.0f;
    std::string animationName = "Attak_O";
    float blendSec = 0.10f;
    bool loop = false;
};

struct PlayerSpecialEventKeyframe {
    float time = 0.0f;
    float duration = 0.08f;
    int type = 0;
    float value = 1.0f;
};

struct PlayerSpecialTimeline {
    std::string name = "SideSpecial";
    float totalSec = 0.45f;
    std::vector<PlayerSpecialHitboxKeyframe> hitboxes;
    std::vector<PlayerSpecialMotionKeyframe> motions;
    std::vector<PlayerSpecialAnimationKeyframe> animations;
    std::vector<PlayerSpecialEventKeyframe> events;
};

struct EditorObject {
    int id = 0;
    std::string name;
    std::string modelPath;
    std::string texturePath;
    int geometryType = -1;
    std::unique_ptr<Object3d> object;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Vector3 rotation{ 0.0f, 0.0f, 0.0f };
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Object3dCommon::BlendMode blendMode = Object3dCommon::BlendMode::kBlendModeNormal;
    bool billboard = false;
    bool bloomPostEffect = false;
    bool outlineBloomPostEffect = false;
    Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    bool showBones = false;
    int selectedBone = 0;
    std::vector<EditorBonePose> bonePoses;
    bool attachToBone = false;
    int attachParentId = -1;
    std::string attachJointName;
    Vector3 attachOffset{ 0.0f, 0.0f, 0.0f };
    Vector3 attachRotation{ 0.0f, 0.0f, 0.0f };
    Vector3 attachScale{ 1.0f, 1.0f, 1.0f };
    bool active = true;
    std::vector<EffectKeyframe> keyframes;
    bool editMode = false;
    int selectedVertexIndex = -1;
    std::vector<int> selectedVertexIndices;
    int vertexRangeStart = 0;
    int vertexRangeEnd = 0;
    Vector3 vertexSelectionOffset{ 0.0f, 0.0f, 0.0f };
    std::unordered_map<uint32_t, Vector3> vertexOffsets;
    bool selected = false;
    bool visible = true;
};

struct EditorObjectSnapshot {
    int id = 0;
    std::string name;
    std::string modelPath;
    std::string texturePath;
    int geometryType = -1;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    Vector3 rotation{ 0.0f, 0.0f, 0.0f };
    Vector3 scale{ 1.0f, 1.0f, 1.0f };
    Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Object3dCommon::BlendMode blendMode = Object3dCommon::BlendMode::kBlendModeNormal;
    bool billboard = false;
    bool bloomPostEffect = false;
    bool outlineBloomPostEffect = false;
    Vector4 bloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    Vector4 outlineBloomColor{ 1.0f, 0.72f, 0.22f, 1.0f };
    bool showBones = false;
    int selectedBone = 0;
    std::vector<EditorBonePose> bonePoses;
    bool attachToBone = false;
    int attachParentId = -1;
    std::string attachJointName;
    Vector3 attachOffset{ 0.0f, 0.0f, 0.0f };
    Vector3 attachRotation{ 0.0f, 0.0f, 0.0f };
    Vector3 attachScale{ 1.0f, 1.0f, 1.0f };
    std::vector<EffectKeyframe> keyframes;
    int selectedVertexIndex = -1;
    std::vector<int> selectedVertexIndices;
    int vertexRangeStart = 0;
    int vertexRangeEnd = 0;
    Vector3 vertexSelectionOffset{ 0.0f, 0.0f, 0.0f };
    std::unordered_map<uint32_t, Vector3> vertexOffsets;
    bool selected = false;
    bool visible = true;
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
    std::vector<ParticleNode> particleNodes;
    int selectedParticleNode = -1;
};

enum class GizmoMode {
    Translate = 0,
    Rotate,
    Scale,
};

enum class EditorMode {
    Blender = 0,
    Particle,
    PlayerAttack,
};

} // namespace ParticleTestEditor
