#pragma once
#include "Object3d.h"
#include "EnemyParts.h"
#include "EnemyAI.h"
#include "DetachedEnemyPart.h"
#include <random>

struct EnemyPartVisual {
    EnemyPartType type = EnemyPartType::None;
    std::unique_ptr<Object3d> object;
    bool visible = true;
};

struct DetachedEnemyPart {
    std::unique_ptr<Object3d> object;
    DetachedPartMotion motion;
    uint64_t spawnOrder = 0;
};

enum class FragmentMode { Chunk, Face };
struct FaceShard {
    std::array<Vector3,3> vertices{}; // world-oriented offsets from triangle centroid
    DetachedPartMotion motion;
    uint64_t spawnOrder = 0;
};
using DetachedEnemyFragment = DetachedEnemyPart; // Same object ownership and physics.

// Uses only the Boss model resource; no legacy AI, attacks or animation playback.
class Enemy {
public:
    ~Enemy();
    void Initialize(Object3dCommon* common, DirectXCommon* dx, Camera* camera, bool useSplitAssets = false);
    using RaycastHit = EnemyPartHit;
    bool Raycast(const Vector3& origin, const Vector3& direction, float maxDistance, RaycastHit& hit) const;
    void ShowHitFeedback(EnemyPartType part);
    float ApplyDamage(EnemyPartType part, float damage, const Vector3& shotDirection);
    void DrawPartDebug(const Matrix4x4& viewProjection, const Vector2& screenMin, const Vector2& screenMax) const;
    void DrawImGui();
    float Update(float dt, const Vector3& playerPosition);
    const Vector3& GetPosition() const { return position_; }
    void SetPosition(const Vector3& position) { position_ = position; }
    void SetRotation(const Vector3& rotation) { rotation_ = rotation; }
    void SetSpawnIdentity(uint64_t id, const std::string& trigger) {
        spawnId_ = id;
        id_ = "Enemy_" + std::string(id < 10 ? 2 : id < 100 ? 1 : 0, '0') + std::to_string(id);
        spawnTriggerId_ = trigger;
    }
    uint64_t GetSpawnId() const { return spawnId_; }
    const std::string& GetId() const { return id_; }
    const std::string& GetSpawnTriggerId() const { return spawnTriggerId_; }
    unsigned int PendingAttackCount() const { return ai_.attacksThisUpdate; }
    void ConfirmAttack(float actualDamage) { attackCount_ += ai_.attacksThisUpdate; lastAttackDamage_ = actualDamage; attackFlash_ = .35f; }
    bool IsDead() const { return EnemyPartsDead(parts_); }
    EnemyState GetState() const { return IsDead() ? EnemyState::Dead : ai_.state; }
    void Draw();
    void SetPartVisible(EnemyPartType type, bool visible);
private:
    uint64_t spawnId_ = 0;
    std::string id_;
    std::string spawnTriggerId_;
    EnemyAI ai_{};
    unsigned long long attackCount_ = 0;
    float lastAttackDamage_ = 0;
    float attackFlash_ = 0;
    Object3d object_;
    FragmentMode breakMode_ = FragmentMode::Face;
    std::array<std::vector<std::array<Vector3,3>>,6> faceData_{};
    std::vector<FaceShard> faceShards_;
    ModelCommon faceModelCommon_;
    std::unique_ptr<Model> faceModel_;
    std::unique_ptr<Object3d> faceBatch_;
    inline static int maxActiveFaces_ = 256;
    int maxFacesPerBreak_ = 64;
    float faceLifetime_ = 5.0f;
    static constexpr size_t kFaceCapacity = 1024;
    static void TrimFacePool(size_t reserve);
    bool SpawnFaces(size_t part, const Vector3& direction);
    void DrawFaces();
    Object3dCommon* common_ = nullptr;
    DirectXCommon* dx_ = nullptr;
    Camera* camera_ = nullptr;
    std::array<std::vector<std::string>, 6> fragmentFiles_{};
    float spreadPower_ = 1.5f;
    float outwardPower_ = 1.5f;
    static constexpr size_t kMaxFragments = 128;
    inline static std::vector<Enemy*> fragmentOwners_{};
    inline static uint64_t nextSpawnOrder_ = 0;
    static void MakeFragmentRoom();
    std::vector<DetachedEnemyPart> detachedParts_;
    DetachedPartSettings detachedSettings_{};
    std::mt19937 random_{std::random_device{}()};
    std::array<EnemyPartVisual, 6> visuals_{};
    bool splitVisuals_ = false;
    EnemyParts parts_{};
    bool showPartColliders_ = false;
    Vector3 position_{3.0f,0.0f,18.0f};
    Vector3 rotation_{0.0f,1.5707963f,0.0f};
    Vector3 scale_{2.0f,2.0f,2.0f};
    bool hasHitBox_ = false;
};
