#pragma once
#include <memory>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "AABB.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "LightingParam.h"
#include "PlayerAttackI.h"

class Object3d;
class Object3dCommon;
class DirectXCommon;
class Camera;

class EnemyManager;


class Player {
    enum class LaunchState {
        None,
        Launch,
        FreeFall,
        Down,
    };

public:

    enum class PlayerModelSet {
        HumanWalk,
        HumanSneakWalk,
        GltfWalkGlb,
        GltfTestGltf,
        Player2Gltf,
    };

    enum class ModelId { Walk, I0, I1, I2, O0, O1, O2 /*邵ｺ・ｪ邵ｺ・ｩ*/ };

    enum class PlayerAction {
        Idle,
        Move,
        Jump,
        Crouch,
        FastFall,
        Guard,
        Attack,
        Launched,
    };

    enum class PlayerAttackType {
        None,
        Weak,
        Tilt,
        Smash,
        NeutralSpecial,
        SideSpecial,
        UpSpecial,
        DownSpecial,
    };

    enum class PlayerAttackGroup : uint8_t {
        Ground,
        Smash,
        Air,
        Count,
    };

    enum class PlayerAttackVariant : uint8_t {
        Neutral,
        Side,
        Up,
        Count,
    };

    struct PlayerAttackDefinition {
        std::string name;
        Vector3 offset = { 0.9f, 0.7f, 0.0f };
        Vector3 halfSize = { 0.55f, 0.70f, 0.45f };
        float startDelaySec = 0.0f;
        float activeSec = 0.12f;
        float actionSec = 0.28f;
        int damage = 8;
    };

    struct PlayerInputCommand {
        PlayerAction action = PlayerAction::Idle;
        PlayerAttackType attackType = PlayerAttackType::None;
        PlayerAttackGroup attackGroup = PlayerAttackGroup::Ground;
        PlayerAttackVariant attackVariant = PlayerAttackVariant::Neutral;
        int horizontal = 0;
        int depth = 0;
        bool down = false;
        bool jumpTriggered = false;
        bool guard = false;
        bool specialHeld = false;
        bool specialReleased = false;
    };


    void Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam);
    void SetCamera(Camera* cam);

    void Update(float dt, const Input& input, EnemyManager& enemyMgr);
    void QueueDebugCommand(const PlayerInputCommand& command);
    void SetExternalInputBlocked(bool blocked) { externalInputBlocked_ = blocked; }
    void Draw();
    void DrawDebugHitBoxes(EnemyManager& enemyMgr);
    bool GetAttackDebugVisualBox(Vector3& outCenter, Vector3& outHalfSize, bool& outIsActive) const;
    bool GetAttackHitBox(AABB& outHitBox) const;
    int GetAttackDamage() const;
    unsigned int GetAttackSerial() const { return attackSerial_; }
    int GetCancelGauge() const { return cancelGauge_; }
    int GetMaxCancelGauge() const { return kMaxCancelGauge_; }
    bool HasSpecialCancelRight() const { return hasSpecialCancelRight_; }
    bool HasSpecialChainCancelRight() const { return hasSpecialChainCancelRight_; }
    bool CanSpecialCancelNow() const;
    bool HasSpecialHitDuringAction() const { return specialHitDuringAction_; }
    bool DidUseSpecialCancelThisAction() const { return specialCancelUsedThisAction_; }
    float GetSpecialCancelDebugFlashSec() const { return specialCancelDebugFlashSec_; }
    int GetSpecialCancelCount() const { return specialCancelCount_; }
    int GetSpecialCancelEffectLevel() const { return specialCancelEffectLevel_; }
    int GetSpecialCancelCameraLevel() const { return specialCancelCameraLevel_; }
    int GetSpecialCancelSoundLevel() const { return specialCancelSoundLevel_; }
    float GetCurrentSpecialHitStopRate() const;
    int GetUComboStageDisplay() const { return lastUComboStage_ + 1; }
    bool IsUComboAccepting() const { return IsUComboAccepting_(); }
    float GetUComboResetTimer() const { return uComboResetTimer_; }
    float GetUComboBufferTimer() const { return uComboBufferTimer_; }
    float GetUComboDebugFlashSec() const { return uComboDebugFlashSec_; }
    void NotifyAttackHit();
    bool IsCounterActive() const;
    void NotifyCounterSuccess();


    void LockMove(float sec) { if (sec > moveLockSec_) moveLockSec_ = sec; }
    bool IsMoveLocked() const { return moveLockSec_ > 0.0f; }

    Vector2 GetPos2D() const { return { pos_.x, pos_.y }; }
    Vector2 GetVel2D() const { return { vel_.x, vel_.y }; }

    float GetZ() const { return pos_.z; }
    Vector3 GetPos3D() const { return pos_; }

    bool IsOnGround() const { return onGround_; }
    int  GetFacing() const { return facing_; }




    AABB GetBodyAABB() const { return body_; }

    void TriggerHitFlash(float sec = 0.20f) { hitFlashSec_ = std::max(hitFlashSec_, sec); }

    void AddHP(int heal);

    void Damage(int Damage);
    void AddDamagePercent(float damagePercent);
    void SetDamagePercent(float damagePercent);
    float GetDamagePercent() const { return damagePercent_; }
    float GetGravity() const { return gravity_; }
    void ApplyLaunch(const Vector3& velocity, float hitStunSec, float actionSpeedRatio = 0.0f);
    void ApplyBossHit(float damagePercent, float baseKnockback, float knockbackScale, const Vector3& knockbackDir, float hitStunSec, float actionSpeedRatio = 0.0f);

    // 吹っ飛び中のXZ速度減衰率（0=即停止, 1=減衰なし, 例:0.15=疾走感イーズアウト）
    float GetLaunchXZDrag() const { return launchXZDrag_; }
    void  SetLaunchXZDrag(float drag) { launchXZDrag_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchXZDragHigh() const { return launchXZDragHigh_; }
    void  SetLaunchXZDragHigh(float drag) { launchXZDragHigh_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchXZDragLow() const { return launchXZDragLow_; }
    void  SetLaunchXZDragLow(float drag) { launchXZDragLow_ = std::clamp(drag, 0.0f, 1.0f); }
    float GetLaunchDragThreshold() const { return launchDragThreshold_; }
    void  SetLaunchDragThreshold(float th) { launchDragThreshold_ = std::clamp(th, 0.0f, 1.0f); }
    bool  GetLaunchDragUseTime() const { return launchDragUseTime_; }
    void  SetLaunchDragUseTime(bool use) { launchDragUseTime_ = use; }
    float GetLaunchBounceRestitution() const { return launchBounceRestitution_; }
    void  SetLaunchBounceRestitution(float rest) { launchBounceRestitution_ = std::clamp(rest, 0.0f, 1.0f); }
    float GetLaunchBounceFriction() const { return launchBounceFriction_; }
    void  SetLaunchBounceFriction(float fric) { launchBounceFriction_ = std::clamp(fric, 0.0f, 1.0f); }
    float GetLaunchBounceMinSpeed() const { return launchBounceMinSpeed_; }
    void  SetLaunchBounceMinSpeed(float speed) { launchBounceMinSpeed_ = std::max(0.0f, speed); }
    float GetLaunchKeepSpeedThreshold() const { return launchKeepSpeedThreshold_; }
    void  SetLaunchKeepSpeedThreshold(float speed) { launchKeepSpeedThreshold_ = std::max(0.0f, speed); }
    float GetFreeFallGroundBounceSpeed() const { return freeFallGroundBounceSpeed_; }
    void  SetFreeFallGroundBounceSpeed(float speed) { freeFallGroundBounceSpeed_ = std::max(0.0f, speed); }
    float GetFreeFallGroundBounceDamping() const { return freeFallGroundBounceDamping_; }
    void  SetFreeFallGroundBounceDamping(float damping) { freeFallGroundBounceDamping_ = std::clamp(damping, 0.0f, 1.0f); }
    bool IsLaunched() const { return launched_; }
    bool IsLaunchFastPhase() const {
        return launched_ && launchState_ == LaunchState::Launch;
    }
    void SetHP(int hp);

    float GetX() const { return pos_.x; }

	int GetHP() const { return hp_; }
	int GetMaxHP() const { return 100; }

    void SetSpawnPos(const Vector3& p);
    void SetDropRespawnPos(const Vector3& p);
    void SetPos(const Vector3& p);
    bool ResolveGroundAABB(const AABB& ground);
    bool ResolveGroundAABB(const std::vector<AABB>& grounds);
    bool ResolveObstaclesAABB(const std::vector<AABB>& obstacles);

    bool IsDead() const { return dead_; }

    void SetGrabbed(bool grabbed) {
        isGrabbed_ = grabbed;
        if (grabbed) {
            SetExternalInputBlocked(true);
            vel_ = { 0.0f, 0.0f, 0.0f };
        } else {
            SetExternalInputBlocked(false);
        }
    }
    bool IsGrabbed() const { return isGrabbed_; }

    void SetLighting(const LightingParam& p);

    void ChangeModelSet_(Player::PlayerModelSet set);


    void UpdateTitleAttackDemo(float dt, float intervalSec = 1.0f);
    void ResetTitleAttackDemo();

    void SetTitleTransform(const Vector3& t, const Vector3& r, const Vector3& s);

    Object3d* GetModelObject() const { return model_.get(); }
    PlayerAction GetCurrentAction() const { return action_; }
    PlayerAttackType GetCurrentAttackType() const { return attackType_; }
    PlayerAttackGroup GetCurrentAttackGroup() const { return activeAttackGroup_; }
    PlayerAttackVariant GetCurrentAttackVariant() const { return activeAttackVariant_; }
    PlayerAttackDefinition& AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant);
    const PlayerAttackDefinition& AttackDefinition(PlayerAttackGroup group, PlayerAttackVariant variant) const;
    static const char* AttackGroupName(PlayerAttackGroup group);
    static const char* AttackVariantName(PlayerAttackVariant variant);
    bool TryGetBoneWorldPosition(
        const std::string& jointName,
        Vector3& out,
        const Vector3& localOffset = { 0.0f, 0.0f, 0.0f }) const;
    bool EmitParticleFromBone(
        const std::string& groupName,
        const std::string& jointName,
        uint32_t count,
        const Vector3& localOffset = { 0.0f, 0.0f, 0.0f }) const;

private:
    PlayerInputCommand ResolveInput_(const Input& input);
    void UpdateSmashInputWindow_(const Input& input);
    void ApplyActionCommand_(const PlayerInputCommand& command);
    void StartAttackAction_(PlayerAttackType type, int horizontal, PlayerAttackGroup group, PlayerAttackVariant variant);
    void UpdateActionTimer_(float dt);
    void PlayActionAnimation_(const PlayerInputCommand& command);
    bool CanStartAttackCommand_(const PlayerInputCommand& command) const;
    bool IsUAttackType_(PlayerAttackType type) const;
    bool IsIAttackType_(PlayerAttackType type) const;
    bool IsUComboAccepting_() const;
    bool IsFinalUComboRecoveryCancelable_() const;
    bool IsSpecialRecoveryCancelable_() const;
    void ApplyLandingRecovery_();
    bool GetAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    void PrepareSpecialCommandTarget_(const PlayerInputCommand& command, const EnemyManager& enemyMgr);
    bool BuildUAttackCommand_(PlayerInputCommand& command) const;
    bool BuildIAttackCommand_(PlayerInputCommand& command) const;
    void InitializeUAttackDefinitions_();
    void InitializeIAttackDefinitions_();
    float GetAttackActionSec_(PlayerAttackType type, PlayerAttackGroup group, PlayerAttackVariant variant) const;
    bool GetUAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    bool GetIAttackDebugHitBox_(Vector3& outCenter, Vector3& outHalfSize) const;
    int GetUAttackDamage_() const;
    int GetIAttackDamage_() const;
    void StartIAttack_(PlayerAttackType type);
    void UpdateIAttack_(float dt);
    void ChangeIAttackState_(PlayerIAttackState state);

    void UpdateMove_(float dt, const Input& input);
    void UpdateMove_(float dt, const PlayerInputCommand& command);
    void ApplyPhysics_(float dt);
    void UpdateModel_();

    void UpdateBody_();
    float GetLaunchSpeed_() const;
    void ResetLaunchState_(PlayerAction nextAction);
    void EnterFreeFall_();
    void UpdateLaunchStateAfterBounce_();
    bool HandleLaunchGroundContact_();
    bool HandleFreeFallGroundContact_();


private:
    
    ModelId currentModel_ = ModelId::Walk;


    std::unique_ptr<Object3d> model_;
    std::unique_ptr<Object3d> debugAtkCube_;
    std::unique_ptr<Object3d> debugEnemyCube_;
    Camera* cam_ = nullptr;


    Vector3 pos_{ 0.0f, 0.0f, 15.0f };
    Vector3 vel_{ 0.0f, 0.0f, 0.0f };

    bool onGround_ = true;
    int  facing_ = +1;
    int jumpCount_ = 0;
    int maxJumpCount_ = 2;


    float moveLockSec_ = 0.0f;

    static constexpr int kSmashInputWindowFrames_ = 5;
    int recentHorizontalDir_ = 0;
    int recentHorizontalFrames_ = 0;

    PlayerAction action_ = PlayerAction::Idle;
    PlayerAttackType attackType_ = PlayerAttackType::None;
    PlayerAttackGroup activeAttackGroup_ = PlayerAttackGroup::Ground;
    PlayerAttackVariant activeAttackVariant_ = PlayerAttackVariant::Neutral;
    float actionTimer_ = 0.0f;
    float attackElapsedSec_ = 0.0f;
    static constexpr int kMaxCancelGauge_ = 3;
    static constexpr int kMaxSpecialCancelCount_ = 3;
    int cancelGauge_ = kMaxCancelGauge_;
    int specialCancelCount_ = 0;
    int specialCancelEffectLevel_ = 0;
    int specialCancelCameraLevel_ = 0;
    int specialCancelSoundLevel_ = 0;
    bool hasSpecialCancelRight_ = false;
    bool hasSpecialChainCancelRight_ = false;
    bool specialChainCancelEligible_ = false;
    bool specialHitDuringAction_ = false;
    bool specialCancelUsedThisAction_ = false;
    bool suppressLandingRecoveryUntilAttackEnd_ = false;
    bool landingRecoveryPending_ = false;
    float specialCancelDebugFlashSec_ = 0.0f;
    bool currentAttackHit_ = false;
    bool sideSpecialHitBounceUsed_ = false;
    bool nextSideSpecialLockOn_ = false;
    bool sideSpecialLockOnActive_ = false;
    Vector3 sideSpecialLockOnTarget_{};
    Vector3 sideSpecialLockOnDirection_{ 1.0f, 0.0f, 0.0f };
    int uComboStage_ = 0;
    int lastUComboStage_ = 0;
    float uComboResetTimer_ = 0.0f;
    float uComboBufferTimer_ = 0.0f;
    float uComboDebugFlashSec_ = 0.0f;
    PlayerInputCommand bufferedUComboCommand_{};
    PlayerInputCommand bufferedSpecialCancelCommand_{};
    float specialCancelBufferTimer_ = 0.0f;
    bool latestSpecialHeld_ = false;
    bool latestSpecialReleased_ = false;
    float iSpecialChargeSec_ = 0.0f;
    bool iCounterSuccess_ = false;
    PlayerIAttackState iAttackState_ = PlayerIAttackState::None;
    float iAttackStateTime_ = 0.0f;
    bool iAttackHitActive_ = false;
    bool guarding_ = false;
    bool crouching_ = false;
    bool fastFalling_ = false;
    bool launched_ = false;
    float launchedTimer_ = 0.0f;
    float launchInitialSpeed_ = 0.0f;
    float launchActionSpeedRatio_ = 0.0f;
    bool launchControlUnlocked_ = false;
    float launchXZDrag_ = 0.18f;   // 吹っ飛びXZ速度の減衰率（pow(drag, dt)で毎フレーム乗算）
    float launchedTotalTime_ = 0.0f;
    float launchXZDragHigh_ = 0.95f; // 高速時のXZドラッグ (1.0に近いほど減衰しにくい)
    float launchXZDragLow_ = 0.15f;  // 低速時のXZドラッグ
    float launchDragThreshold_ = 0.20f; // 切り替えの閾値 (0.0〜1.0)
    bool launchDragUseTime_ = true;  // 残り時間割合で判定するか (falseなら残り速度割合)
    float launchBounceRestitution_ = 0.65f; // 跳ね返り反発係数 (0.0〜1.0)
    float launchBounceFriction_ = 0.90f;    // 跳ね返り時の他軸摩擦係数 (0.0〜1.0)
    float launchBounceMinSpeed_ = 4.0f;     // 跳ね返りが発生する最低速度
    LaunchState launchState_ = LaunchState::None;
    bool freeFallSmallBounceUsed_ = false;
    bool launchHasBounced_ = false;
    float launchKeepSpeedThreshold_ = 8.0f;
    float freeFallGroundBounceSpeed_ = 3.5f;
    float freeFallGroundBounceDamping_ = 0.35f;
    float fastFallSpeed_ = 28.0f;


    float moveSpeed_ = 10.0f;
    float depthSpeed_ = 14.0f;
    float jumpVel_ = 12.0f;
    float gravity_ = 25.0f;
    float zView_ = 15.0f;

    int hp_ = 100;
    float damagePercent_ = 0.0f;


    AABB body_{};

    float hitFlashSec_ = 0.0f;
    Vector4 normalColor_{ 1,1,1,1 };
    Vector4 hitColor_{ 1,0.2f,0.2f,1 };


    float walkAnimTime_ = 0.0f;
    static constexpr int   kWalkFrameCount_ = 5;
    static constexpr float kWalkFps_ = 10.0f;

    Model* walkModels_[5];

    Model* iAtkModels_[3] = { nullptr, nullptr, nullptr };
    float iAtkAnimTime_ = 0.0f;
    static constexpr float kIAttackFps_ = 12.0f;

    Model* oAtkModels_[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
    float oAtkAnimTime_ = 0.0f;
    static constexpr int   kOFrameCount_ = 5;
    static constexpr float kOAttackFps_ = 12.0f;

    bool dead_ = false;
 
    LightingParam light_;

    std::unique_ptr<Object3d> shadow_;
    float shadowBaseScale_ = 1.2f;
    float shadowMaxAlpha_ = 0.45f;
    float shadowLiftY_ = 0.02f;
    float shadowMinAlpha_ = 0.05f;

    PlayerModelSet currentModelSet_ = PlayerModelSet::HumanWalk;

    bool isMoving = false;
    bool hasDebugCommand_ = false;
    bool externalInputBlocked_ = false;
    bool isGrabbed_ = false;
    PlayerInputCommand debugCommand_{};
    unsigned int attackSerial_ = 0;
    std::array<std::array<PlayerAttackDefinition, static_cast<size_t>(PlayerAttackVariant::Count)>, static_cast<size_t>(PlayerAttackGroup::Count)> attackDefinitions_{};


    std::string curAnim_ = "";
    bool prevAtkI_ = false;
    bool prevAtkO_ = false;


    float titleDemoTimer_ = 0.0f;
    bool  titleDemoNextIsI_ = true;

    friend class PlayerIAttack;

};
