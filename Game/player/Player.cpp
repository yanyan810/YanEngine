#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"
#include "ParticleManager.h"

#include "EnemyManager.h"

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

void Player::ChangeModelSet_(Player::PlayerModelSet set) {
    if (currentModelSet_ == set) return;

    currentModelSet_ = set;

    model_->SetModel(GetPlayerModelPath(set));
    model_->PlayAnimation("", true);
}


void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    cam_ = cam;

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

    swordObj_ = std::make_unique<Object3d>();
    swordObj_->Initialize(objCommon, dx);
    swordObj_->SetModel("Player/sword.obj");
    swordObj_->SetCamera(cam_);
    swordObj_->SetTranslate({ 0,0,0 });


}


void Player::SetCamera(Camera* cam) {
    cam_ = cam;
    if (model_) model_->SetCamera(cam_);
}

void Player::SetWeaponAttachment(
    const std::vector<std::string>& jointCandidates,
    const Vector3& localOffset,
    const Vector3& rotate,
    const Vector3& scale) {
    weaponJointCandidates_ = jointCandidates;
    weaponLocalOffset_ = localOffset;
    weaponRotate_ = rotate;
    weaponScale_ = scale;
}

bool Player::TryGetBoneWorldPosition(const std::string& jointName, Vector3& out, const Vector3& localOffset) const {
    if (!model_) {
        return false;
    }
    return model_->GetJointWorldPosition(jointName, out, localOffset);
}

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

void Player::Update(float dt, const Input& input, EnemyManager& enemyMgr) {
    (void)enemyMgr;

    isMoving = false;
    UpdateActionTimer_(dt);

    if (launched_) {
        if (launchedTimer_ > 0.0f) {
            launchedTimer_ -= dt;
        }
        if (launchedTimer_ <= 0.0f && onGround_) {
            launched_ = false;
            launchedTimer_ = 0.0f;
            action_ = PlayerAction::Idle;
        }
    }

    if (moveLockSec_ > 0.0f) {
        moveLockSec_ -= dt;
        if (moveLockSec_ < 0.0f) moveLockSec_ = 0.0f;
    }

    const bool inputBlockedByLaunch = launched_;
    const bool useDebugCommand = hasDebugCommand_ && !inputBlockedByLaunch;
    PlayerInputCommand command = useDebugCommand
        ? debugCommand_
        : ((externalInputBlocked_ || inputBlockedByLaunch) ? PlayerInputCommand{} : ResolveInput_(input));
    if (launched_) {
        command.action = PlayerAction::Launched;
    }
    hasDebugCommand_ = false;

    if (command.jumpTriggered && jumpCount_ < maxJumpCount_ && actionTimer_ <= 0.0f && !command.guard) {
        onGround_ = false;
        vel_.y = jumpVel_;
        ++jumpCount_;
    }

    if (!inputBlockedByLaunch && !IsMoveLocked() && command.action != PlayerAction::Guard && command.action != PlayerAction::Crouch) {
        if (useDebugCommand) {
            UpdateMove_(dt, command);
        } else {
            UpdateMove_(dt, input);
        }
    }
    else if (!launched_) {
        vel_.x = 0.0f;
        vel_.z = 0.0f;
    }

    ApplyActionCommand_(command);
    if (command.down) {
        vel_.z = 0.0f;
    }
    PlayActionAnimation_(command);

    ApplyPhysics_(dt);

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
    if (model_ && swordObj_) {
        for (const std::string& jointName : weaponJointCandidates_) {
            if (model_->AttachObjectToJoint(*swordObj_, jointName, weaponLocalOffset_, weaponRotate_, weaponScale_)) {
                break;
            }
        }
    }


    //static bool once = true;
    //if (once && swordObj_) {
    //    once = false;
    //}

	if (swordObj_) swordObj_->Update(dt);
    if (debugAtkCube_) debugAtkCube_->Update(dt);

    if (hitFlashSec_ > 0.0f) {
        hitFlashSec_ -= dt;
        if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
    }

    if (model_) {
        model_->SetMaterialColor((hitFlashSec_ > 0.0f) ? hitColor_ : normalColor_);
    }


    SetLighting(light_);

    OutputDebugStringA(("[PlayerAnim] " + curAnim_ + "\n").c_str());

}

void Player::QueueDebugCommand(const PlayerInputCommand& command) {
    debugCommand_ = command;
    hasDebugCommand_ = true;
}



