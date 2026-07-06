#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "ParticleManager.h"

#include "EnemyManager.h"

#include <algorithm>
#include <cmath>
#include <limits>

static const char* kHumanWalk_Set[] = {
    "human/walk.gltf",
};

static const char* kHumanSneakWalk_Set[] = {
    "human/sneakWalk.gltf",
};

static const char* kGltfWalkGlb_Set[] = {
    "gltf/walk.glb",
};

static const char* kGltfTestGltf_Set[] = {
    "gltf/test.gltf",
};

static const char* kPlayer2Gltf_Set[] = {
    "Player/player2.gltf",
};

static Vector4 Mul(const Matrix4x4& m, const Vector4& v);

auto LogModel = [](const char* tag) {
    OutputDebugStringA(tag);
    OutputDebugStringA("\n");
    };

static const char* GetPlayerModelPath(Player::PlayerModelSet set) {
    switch (set) {
    case Player::PlayerModelSet::HumanWalk:      return "human/walk.gltf";
    case Player::PlayerModelSet::HumanSneakWalk: return "human/sneakWalk.gltf";
    case Player::PlayerModelSet::GltfWalkGlb:    return "gltf/walk.glb";
    case Player::PlayerModelSet::GltfTestGltf:   return "Player/test.gltf";
    case Player::PlayerModelSet::Player2Gltf:    return "Player/player.gltf";
    default:                             return "human/walk.gltf";
    }
}

// ===== モデルの変更・切替 =====
void Player::ChangeModelSet_(Player::PlayerModelSet set) {
    if (currentModelSet_ == set) return;

    currentModelSet_ = set;

    model_->SetModel(GetPlayerModelPath(set));
    model_->PlayAnimation("", true);
}

// ===== 着地時のキャンセル情報やコンボデータ等のリセット =====
void Player::ApplyLandingRecovery_() {
    // 着地でコンボを区切るため、ゲージ・キャンセル権・強化Lvをまとめて初期化する。
    cancelGauge_ = kMaxCancelGauge_;
    specialCancelCount_ = 0;
    specialCancelEffectLevel_ = 0;
    specialCancelCameraLevel_ = 0;
    specialCancelSoundLevel_ = 0;
    iSpecialVariant_ = PlayerISpecialVariant::Lv0;
    iSpecialPulseIndex_ = 0;
    hasSpecialCancelRight_ = false;
    hasSpecialChainCancelRight_ = false;
    specialChainCancelEligible_ = false;
    specialCancelUsedThisAction_ = false;
    sideSpecialHitBounceUsed_ = false;
    nextSideSpecialLockOn_ = false;
    sideSpecialLockOnActive_ = false;
    uComboResetTimer_ = 0.0f;
    uComboBufferTimer_ = 0.0f;
    uComboDebugFlashSec_ = 0.0f;
    specialCancelBufferTimer_ = 0.0f;
    landingRecoveryPending_ = false;
}


// ===== プレイヤーの初期化処理 (モデル・デバッグ表示・影の生成) =====
void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    cam_ = cam;

    InitializeUAttackDefinitions_();
    InitializeIAttackDefinitions_();

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);
    model_->SetCamera(cam_);

    currentModelSet_ = PlayerModelSet::Player2Gltf;
    model_->SetModel("Player/player.gltf");
    //model_->SetModel("Player/player2.gltf");
    model_->PlayAnimation("Idle", true);
    model_->SetUseEnvironmentMap(false);
    model_->SetEnvironmentCoefficient(1.0f);
    model_->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    curAnim_ = "Idle";

    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    if (model_) {
        model_->SetMaterialColor(normalColor_);
    }

    debugAtkCube_ = std::make_unique<Object3d>();
    debugAtkCube_->Initialize(objCommon, dx);
    debugAtkCube_->SetCamera(cam_);
    debugAtkCube_->SetModel("cube/cube.obj");
    debugAtkCube_->SetEnableLighting(0);
    debugAtkCube_->SetMaterialColor({ 0.1f, 1.0f, 0.2f, 0.65f });

    debugEnemyCube_ = std::make_unique<Object3d>();
    debugEnemyCube_->Initialize(objCommon, dx);
    debugEnemyCube_->SetCamera(cam_);
    // debugEnemyCube_->SetModel("cube/cube.obj");

      // ===== blob shadow =====
    TextureManager::GetInstance()->LoadTexture("resources/shadow/shadow.png");

    shadow_ = std::make_unique<Object3d>();
    shadow_->Initialize(objCommon, dx);
    shadow_->SetCamera(cam_);

    shadow_->SetModel("plane/plane.obj");

    shadow_->SetEnableLighting(0);
    shadow_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    shadow_->SetMaterialColor({ 255,255,255, shadowMaxAlpha_ });

}


// ===== カメラの設定 =====
void Player::SetCamera(Camera* cam) {
    cam_ = cam;
    if (model_) model_->SetCamera(cam_);
}

// ===== ボーン情報（ジョイント位置）の取得 =====
bool Player::TryGetBoneWorldPosition(const std::string& jointName, Vector3& out, const Vector3& localOffset) const {
    if (!model_) {
        return false;
    }
    return model_->GetJointWorldPosition(jointName, out, localOffset);
}

// ===== 指定したジョイント位置からパーティクルを放出 =====
bool Player::EmitParticleFromBone(
    const std::string& groupName,
    const std::string& jointName,
    uint32_t count,
    const Vector3& localOffset) const {
    Vector3 position{};
    if (!TryGetBoneWorldPosition(jointName, position, localOffset)) {
        return false;
    }

    ParticleManager::GetInstance()->Emit(groupName, position, count);
    return true;
}

// ===== メイン更新処理 (Update) =====
void Player::Update(float dt, const Input& input, EnemyManager& enemyMgr) {
    isMoving = false;
    UpdateActionTimer_(dt);

    if (launched_) {
        if (launchedTimer_ > 0.0f) {
            launchedTimer_ -= dt;
        }
        if (!launchControlUnlocked_ && launchActionSpeedRatio_ > 0.0f && launchInitialSpeed_ > 1.0e-4f) {
            const float speed = std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
            if (speed <= launchInitialSpeed_ * launchActionSpeedRatio_) {
                launchControlUnlocked_ = true;
            }
        }
        if (launchedTimer_ <= 0.0f && onGround_) {
            ResetLaunchState_(PlayerAction::Idle);
        }
    }

    if (moveLockSec_ > 0.0f) {
        moveLockSec_ -= dt;
        if (moveLockSec_ < 0.0f) moveLockSec_ = 0.0f;
    }

    const bool preserveSideSpecialBounce =
        sideSpecialHitBounceUsed_ &&
        !onGround_ &&
        !hasSpecialChainCancelRight_;
    const bool inputBlockedByLaunch = launched_ && !launchControlUnlocked_;
    const bool inputBlockedBySideSpecialBounce = preserveSideSpecialBounce;
    const bool useDebugCommand = hasDebugCommand_ && !inputBlockedByLaunch && !inputBlockedBySideSpecialBounce;
    PlayerInputCommand command = useDebugCommand
        ? debugCommand_
        : ((externalInputBlocked_ || inputBlockedByLaunch || inputBlockedBySideSpecialBounce) ? PlayerInputCommand{} : ResolveInput_(input));
    if (launched_ && !launchControlUnlocked_) {
        command.action = PlayerAction::Launched;
    }
    hasDebugCommand_ = false;

    if (command.jumpTriggered && jumpCount_ < maxJumpCount_ && actionTimer_ <= 0.0f && !command.guard) {
        onGround_ = false;
        vel_.y = launched_ ? std::max(vel_.y, jumpVel_) : jumpVel_;
        ++jumpCount_;
        if (launched_) {
            ResetLaunchState_(PlayerAction::Jump);
        }
    }

    if (launched_ && launchControlUnlocked_) {
        const float airControlAccelX = moveSpeed_ * 3.0f;
        const float airControlAccelZ = depthSpeed_ * 3.0f;
        const float airControlBrakeMul = 1.35f;

        const float inputX = static_cast<float>(command.horizontal);
        const float inputZ = static_cast<float>(command.depth);
        const float accelX = (inputX != 0.0f && vel_.x * inputX < 0.0f)
            ? airControlAccelX * airControlBrakeMul
            : airControlAccelX;
        const float accelZ = (inputZ != 0.0f && vel_.z * inputZ < 0.0f)
            ? airControlAccelZ * airControlBrakeMul
            : airControlAccelZ;

        vel_.x += inputX * accelX * dt;
        vel_.z += inputZ * accelZ * dt;

        const float maxHorizontalSpeed = std::max(launchInitialSpeed_ * 1.15f, moveSpeed_);
        const float maxDepthSpeed = std::max(launchInitialSpeed_ * 0.80f, depthSpeed_);
        vel_.x = std::clamp(vel_.x, -maxHorizontalSpeed, maxHorizontalSpeed);
        vel_.z = std::clamp(vel_.z, -maxDepthSpeed, maxDepthSpeed);
        isMoving = command.horizontal != 0 || command.depth != 0;
    }
    else if (!preserveSideSpecialBounce &&
        !inputBlockedByLaunch &&
        !IsMoveLocked() &&
        command.action != PlayerAction::Guard &&
        command.action != PlayerAction::Crouch) {
        if (useDebugCommand) {
            UpdateMove_(dt, command);
        } else {
            UpdateMove_(dt, input);
        }
    }
    else if (!launched_ && !preserveSideSpecialBounce) {
        vel_.x = 0.0f;
        vel_.z = 0.0f;
    }

    PrepareSpecialCommandTarget_(command, enemyMgr);
    ApplyActionCommand_(command);
    if (command.down && !launched_) {
        vel_.z = 0.0f;
    }
    specialCancelDebugFlashSec_ = std::max(0.0f, specialCancelDebugFlashSec_ - dt);
    UpdateIAttack_(dt);
    PlayActionAnimation_(command);

    const bool wasOnGround = onGround_;
    ApplyPhysics_(dt);
    if (!wasOnGround && onGround_) {
        if (suppressLandingRecoveryUntilAttackEnd_ && action_ == PlayerAction::Attack) {
            landingRecoveryPending_ = true;
        } else {
            ApplyLandingRecovery_();
        }
    }

    UpdateBody_();

    UpdateModel_();

    if (shadow_) {
        const float groundY = 0.0f;

        float height = pos_.y - groundY;
        float h01 = std::clamp(height / 5.0f, 0.0f, 1.0f);

        float alpha = shadowMaxAlpha_ * (1.0f - h01);
        alpha = std::max(alpha, shadowMinAlpha_);

        float s = shadowBaseScale_ * (1.0f + 0.25f * h01);

        shadow_->SetTranslate({ pos_.x, groundY + shadowLiftY_, pos_.z });
        shadow_->SetRotate({ -0.0f, 0.0f, 0.0f });
        shadow_->SetScale({ s, s, s });

        shadow_->SetMaterialColor({ 0,0,0, alpha });

        shadow_->Update(dt);
    }

    if (model_) model_->Update(dt);
    if (debugAtkCube_) debugAtkCube_->Update(dt);

    if (hitFlashSec_ > 0.0f) {
        hitFlashSec_ -= dt;
        if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
    }

    if (model_) {
        model_->SetMaterialColor((hitFlashSec_ > 0.0f) ? hitColor_ : normalColor_);
    }


    SetLighting(light_);

 //   OutputDebugStringA(("[PlayerAnim] " + curAnim_ + "\n").c_str());

}

// ===== デバッグ用コマンドのキューイング =====
void Player::QueueDebugCommand(const PlayerInputCommand& command) {
    debugCommand_ = command;
    hasDebugCommand_ = true;
}
