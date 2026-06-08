#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"

// ===== Base model sets・医∪縺壹・繝｢繝・Ν遞ｮ鬘槭〒蛻・屬・・=====
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
    if (currentModelSet_ == set) return; // 笘・酔縺倥↑繧我ｽ輔ｂ縺励↑縺・

    currentModelSet_ = set;

    model_->SetModel(GetPlayerModelPath(set));
    model_->PlayAnimation("", true); // gltf / glb 蜈磯ｭ繧｢繝九Γ蜀咲函
}


void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    cam_ = cam;

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);   // 竊仙・騾壼喧縺ｧ縺阪※繧後・縺薙ｌ縺縺代〒OK
    model_->SetCamera(cam_);

    currentModelSet_ = PlayerModelSet::Player2Gltf;
    model_->SetModel("Player/player.gltf"); // 縺ゅ↑縺溘・螳溘ヱ繧ｹ縺ｫ蜷医ｏ縺帙ｋ
    //model_->SetModel("Player/player2.gltf");
    model_->PlayAnimation("Idle", true);
    model_->SetUseEnvironmentMap(false);
    model_->SetEnvironmentCoefficient(1.0f);
    model_->SetEnvironmentTexturePath("resources/skybox/skybox.dds");
    curAnim_ = "Idle";

    // 隕九◆逶ｮ蛻晄悄蜿肴丐
    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    if (model_) {
        model_->SetMaterialColor(normalColor_);
    }

    // 繝・ヰ繝・げ逕ｨcube縺悟ｿ・ｦ√↑繧会ｼ医せ繧ｭ繝ｳ縺倥ｃ縺ｪ縺・↑繧・cube.obj 縺ｧOK・・
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

    // 蠖ｱ縺ｯ縲梧攸繝昴Μ縲阪Δ繝・Ν繧剃ｽｿ縺・ｼ・esources/plane.obj 遲会ｼ・
    shadow_->SetModel("plane/plane.obj"); // 竊舌≠縺ｪ縺溘・迺ｰ蠅・・譚ｿ繝｢繝・Ν縺ｫ蜷医ｏ縺帙※

    shadow_->SetEnableLighting(0); // 笘・Λ繧､繝郁ｨ育ｮ励＠縺ｪ縺・
    shadow_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    // 蠖ｱ縺ｮ濶ｲ・磯ｻ・+ ﾎｱ・・窶ｻ縺薙％縺ｧ縺ｯ蛻晄悄蛟､
    shadow_->SetMaterialColor({ 255,255,255, shadowMaxAlpha_ });

    //蜑｣
    swordObj_ = std::make_unique<Object3d>();
    swordObj_->Initialize(objCommon, dx);
    swordObj_->SetModel("Player/sword.obj");
    swordObj_->SetCamera(cam_);
    swordObj_->SetTranslate({ 0,0,0 }); // 蛻晄悄遒ｺ隱咲畑


}


void Player::SetCamera(Camera* cam) {
    cam_ = cam;
    if (model_) model_->SetCamera(cam_);
}

void Player::Update(float dt, const Input& input, EnemyManager& enemyMgr) {
    (void)enemyMgr;

    isMoving = false;
    UpdateActionTimer_(dt);

    // 笘・ｧｻ蜍輔Ο繝・け譖ｴ譁ｰ
    if (moveLockSec_ > 0.0f) {
        moveLockSec_ -= dt;
        if (moveLockSec_ < 0.0f) moveLockSec_ = 0.0f;
    }

    const PlayerInputCommand command = ResolveInput_(input);

    // --- 繧ｸ繝｣繝ｳ繝暦ｼ・・・---
    if (command.jumpTriggered && onGround_ && actionTimer_ <= 0.0f && !command.guard) {
        onGround_ = false;
        vel_.y = jumpVel_;
    }

    // 1) 遘ｻ蜍募・蜉幢ｼ医Ο繝・け荳ｭ縺ｯ辟｡隕厄ｼ・
    if (!IsMoveLocked() && command.action != PlayerAction::Guard && command.action != PlayerAction::Crouch) {
        UpdateMove_(dt, input);
    }
    else {
        vel_.x = 0.0f;
        vel_.z = 0.0f;
    }

    ApplyActionCommand_(command);
    if (command.down) {
        vel_.z = 0.0f;
    }
    PlayActionAnimation_(command);

    // 2) 迚ｩ逅・ｼ磯㍾蜉帙・蠎ｧ讓呎峩譁ｰ・・
    ApplyPhysics_(dt);

    // 蠖薙◆繧雁愛螳壽峩譁ｰ
    UpdateBody_();

    // 4) 隕九◆逶ｮ・・os_遒ｺ螳壼ｾ後↓蜿肴丐・・
    UpdateModel_();

    // 笘・blob shadow 譖ｴ譁ｰ
    if (shadow_) {
        // 蝨ｰ髱｢y=0蜑肴署・医≠縺ｪ縺溘・蝨ｰ髱｢縺・-5 縺ｨ縺九↑繧・groundY 繧貞粋繧上○縺ｦ・・
        const float groundY = 0.0f;

        float height = pos_.y - groundY;          // 鬮倥＆
        float h01 = std::clamp(height / 5.0f, 0.0f, 1.0f); // 5.0f 縺ｯ螂ｽ縺ｿ縺ｧ

        // 鬮倥＞縺ｻ縺ｩ蠖ｱ繧定埋縺・
        float alpha = shadowMaxAlpha_ * (1.0f - h01);
        alpha = std::max(alpha, shadowMinAlpha_);

        // 鬮倥＞縺ｻ縺ｩ蟆代＠蠎・￡繧具ｼ医・繧上▲縺ｨ・・
        float s = shadowBaseScale_ * (1.0f + 0.25f * h01);

        shadow_->SetTranslate({ pos_.x, groundY + shadowLiftY_, pos_.z });
        shadow_->SetRotate({ -0.0f, 0.0f, 0.0f }); // X霆ｸ -90蠎ｦ縺ｧ蝨ｰ髱｢縺ｫ蟇昴°縺帙ｋ・・lane縺傾Z蟷ｳ髱｢縺ｪ繧我ｸ崎ｦ・ｼ・
        shadow_->SetScale({ s, s, s });

        shadow_->SetMaterialColor({ 0,0,0, alpha });

        shadow_->Update(dt);
    }

    // 笘・％縺薙〒 Object3d 譖ｴ譁ｰ・・VP / palette譖ｴ譁ｰ・・
    if (model_) model_->Update(dt);
    // ===== 蜑｣霑ｽ蠕難ｼ医せ繧ｭ繝ｳ縺ｮ繝懊・繝ｳ縺九ｉ蜿悶ｋ・・====
    if (model_ && swordObj_) {

        const char* handJointName = "RightHand";
        const char* candidates[] = {
            "mixamorig:RightHand",
            "RightHand",
            "hand.R",
            "Hand.R",
            "繝懊・繝ｳ.017",
            "繝懊・繝ｳ.005",
        };
        for (const char* candidate : candidates) {
            if (model_->HasJoint(candidate)) {
                handJointName = candidate;
                break;
            }
        }

        Matrix4x4 handW = model_->GetJointWorldMatrix(handJointName);

        // 謇九・菴咲ｽｮ・医≠縺ｪ縺溘・陦悟・縺ｯ translation 縺・m[3][0..2]・・
        Vector3 handPos{ handW.m[3][0], handW.m[3][1], handW.m[3][2] };

        // 菴咲ｽｮ繧ｪ繝輔そ繝・ヨ・域焔縺ｮ荳ｭ縺ｧ縺ｮ蠕ｮ隱ｿ謨ｴ・・
        Vector3 offset{ 0.0f, 0.0f, 0.0f };
        Vector3 swordScale{ 0.15f, 0.15f, 0.15f };

        swordObj_->SetScale(swordScale);
        swordObj_->SetTranslate({
            handPos.x + offset.x,
            handPos.y + offset.y,
            handPos.z + offset.z
            });

        // ・医∪縺壻ｽ咲ｽｮ縺縺代〒OK縲ょ屓霆｢繧ょ粋繧上○縺溘＞縺ｪ繧画ｬ｡縺ｧ霑ｽ蜉・・
    }


    //static bool once = true;
    //if (once && swordObj_) {
    //    once = false;
    //    swordObj_->SetTranslate({ 5.0f, 0.0f, 15.0f }); // 譏弱ｉ縺九↓繧ｺ繝ｬ繧句､
    //}

    if (swordObj_) {
        const auto& t = swordObj_->GetTranslate();
        OutputDebugStringA(std::format("[SwordObj] translate=({:.3f},{:.3f},{:.3f})\n", t.x, t.y, t.z).c_str());
    }


	if (swordObj_) swordObj_->Update(dt);
    if (debugAtkCube_) debugAtkCube_->Update(dt);

    // 陲ｫ蠑ｾ繝輔Λ繝・す繝･
    if (hitFlashSec_ > 0.0f) {
        hitFlashSec_ -= dt;
        if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
    }

    if (model_) {
        model_->SetMaterialColor((hitFlashSec_ > 0.0f) ? hitColor_ : normalColor_);
    }


    // 繝ｩ繧､繝・
    SetLighting(light_);

    OutputDebugStringA(("[PlayerAnim] " + curAnim_ + "\n").c_str());

}



