#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "Enemy.h" 

#include "PlayerCombo.h"

// ===== Base model sets（まずはモデル種類で分離） =====
static const char* kHumanWalk_Set[] = {
    "human/walk.gltf",
};

static const char* kHumanSneakWalk_Set[] = {
    "human/sneakWalk.gltf",
};

static const char* kGltfWalkGlb_Set[] = {
    "gltf/walk.glb",
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
    default:                             return "human/walk.gltf";
    }
}

void Player::ChangeModelSet_(Player::PlayerModelSet set) {
    if (currentModelSet_ == set) return; // ★同じなら何もしない

    currentModelSet_ = set;

    model_->SetModel(GetPlayerModelPath(set));
    model_->PlayAnimation("", true); // gltf / glb 先頭アニメ再生
}


void Player::Initialize(Object3dCommon* objCommon, DirectXCommon* dx, Camera* cam) {
    cam_ = cam;

    model_ = std::make_unique<Object3d>();
    model_->Initialize(objCommon, dx);   // ←共通化できてればこれだけでOK
    model_->SetCamera(cam_);

    currentModelSet_ = PlayerModelSet::HumanWalk;
    model_->SetModel("Player/player.gltf"); // あなたの実パスに合わせる
    model_->PlayAnimation("Idle", true);
    curAnim_ = "Idle";

    // 見た目初期反映
    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    combo_ = std::make_unique<PlayerCombo>();
    combo_->Reset();

    if (model_) {
        model_->SetMaterialColor(normalColor_);
    }

    // デバッグ用cubeが必要なら（スキンじゃないなら cube.obj でOK）
    debugAtkCube_ = std::make_unique<Object3d>();
    debugAtkCube_->Initialize(objCommon, dx);
    debugAtkCube_->SetCamera(cam_);
    debugAtkCube_->SetModel("cube/cube.obj");

    debugEnemyCube_ = std::make_unique<Object3d>();
    debugEnemyCube_->Initialize(objCommon, dx);
    debugEnemyCube_->SetCamera(cam_);
    // debugEnemyCube_->SetModel("cube/cube.obj");

      // ===== blob shadow =====
    TextureManager::GetInstance()->LoadTexture("resources/shadow/shadow.png");

    shadow_ = std::make_unique<Object3d>();
    shadow_->Initialize(objCommon, dx);
    shadow_->SetCamera(cam_);

    // 影は「板ポリ」モデルを使う（resources/plane.obj 等）
    shadow_->SetModel("plane/plane.obj"); // ←あなたの環境の板モデルに合わせて

    shadow_->SetEnableLighting(0); // ★ライト計算しない
    shadow_->SetBlendMode(Object3dCommon::BlendMode::kBlendModeNormal);

    // 影の色（黒 + α） ※ここでは初期値
    shadow_->SetMaterialColor({ 255,255,255, shadowMaxAlpha_ });

    //剣
    swordObj_ = std::make_unique<Object3d>();
    swordObj_->Initialize(objCommon, dx);
    swordObj_->SetModel("Player/sword.obj");
    swordObj_->SetCamera(cam_);
    swordObj_->SetTranslate({ 0,0,0 }); // 初期確認用


}


void Player::SetCamera(Camera* cam) {
    cam_ = cam;
    if (model_) model_->SetCamera(cam_);
}

void Player::Update(float dt, const Input& input, EnemyManager& enemyMgr) {

    isMoving = false;
    bool isAttacking = combo_ && combo_->IsAttacking(); // ←無ければ後述



    // ★移動ロック更新
    if (moveLockSec_ > 0.0f) {
        moveLockSec_ -= dt;
        if (moveLockSec_ < 0.0f) moveLockSec_ = 0.0f;
    }

    // ★I / O 押下した瞬間に移動ロック
    if (combo_) {
        if (input.IsKeyTrigger(DIK_I)) {
            LockMove(combo_->PreviewAttackDuration(!onGround_, 0, AttackBtn::Weak));
        }
        if (input.IsKeyTrigger(DIK_O)) {
            LockMove(combo_->PreviewAttackDuration(!onGround_, 0, AttackBtn::Strong));
        }
    }

    // --- ジャンプ（Y） ---
    if (onGround_ && input.IsKeyTrigger(DIK_SPACE)) {
        onGround_ = false;
        vel_.y = jumpVel_;
    }

    // 1) 移動入力（ロック中は無視）
    if (!IsMoveLocked()) {
        UpdateMove_(dt, input);
    }
    else {
        vel_.x = 0.0f;
        vel_.z = 0.0f;
    }

    // 3) コンボ（速度/位置を書き換える可能性がある）
    if (combo_) {
        Vector2 p{ pos_.x, pos_.y };
        Vector2 v{ vel_.x, vel_.y };

        combo_->Update(dt, input, p, v, onGround_, facing_, pos_.z, enemyMgr);

        vel_.x = v.x;
        vel_.y = v.y;
        pos_.x = p.x;
        pos_.y = p.y;
    }

    // combo_->Update の後に取り直す
    isAttacking = combo_ && combo_->IsAttacking();

    if (model_) {
        const bool atkITrig = input.IsKeyTrigger(DIK_I);
        const bool atkOTrig = input.IsKeyTrigger(DIK_O);

        const bool inAttackAnim = (curAnim_ == "Attak_I" || curAnim_ == "Attak_O");

        // ★1) まず「攻撃開始トリガー」を最優先（このフレームで必ず再生開始）
        if (!inAttackAnim) {
            if (atkITrig) {
                model_->PlayAnimation("Attak_I", false);
                curAnim_ = "Attak_I";
            } else if (atkOTrig) {
                model_->PlayAnimation("Attak_O", false);
                curAnim_ = "Attak_O";
            }
        }

        // ★2) 攻撃中か判定（コンボ or アニメどちらか）
        const bool inAttack = isAttacking || inAttackAnim;

        if (inAttack) {
            // 攻撃中：終わったら戻す（入力では上書きしない）
            if (model_->IsAnimationFinished()) {
                if (isMoving) {
                    if (curAnim_ != "Walk") {
                        model_->PlayAnimation("Walk", true);
                        curAnim_ = "Walk";
                    }
                } else {
                    if (curAnim_ != "Idle") {
                        model_->PlayAnimation("Idle", true);
                        curAnim_ = "Idle";
                    }
                }
            }
        } else {
            // ★3) 通常（Idle/Walk）
            if (isMoving) {
                if (curAnim_ != "Walk") {
                    model_->PlayAnimation("Walk", true);
                    curAnim_ = "Walk";
                }
            } else {
                if (curAnim_ != "Idle") {
                    model_->PlayAnimation("Idle", true);
                    curAnim_ = "Idle";
                }
            }
        }
    }

   


    // 2) 物理（重力・座標更新）
    ApplyPhysics_(dt);

    // 当たり判定更新
    UpdateBody_();

    // 4) 見た目（pos_確定後に反映）
    UpdateModel_();

    // ★ blob shadow 更新
    if (shadow_) {
        // 地面y=0前提（あなたの地面が -5 とかなら groundY を合わせて）
        const float groundY = 0.0f;

        float height = pos_.y - groundY;          // 高さ
        float h01 = std::clamp(height / 5.0f, 0.0f, 1.0f); // 5.0f は好みで

        // 高いほど影を薄く
        float alpha = shadowMaxAlpha_ * (1.0f - h01);
        alpha = std::max(alpha, shadowMinAlpha_);

        // 高いほど少し広げる（ふわっと）
        float s = shadowBaseScale_ * (1.0f + 0.25f * h01);

        shadow_->SetTranslate({ pos_.x, groundY + shadowLiftY_, pos_.z });
        shadow_->SetRotate({ -0.0f, 0.0f, 0.0f }); // X軸 -90度で地面に寝かせる（planeがXZ平面なら不要）
        shadow_->SetScale({ s, s, s });

        shadow_->SetMaterialColor({ 0,0,0, alpha });

        shadow_->Update(dt);
    }

    // ★ここで Object3d 更新（WVP / palette更新）
    if (model_) model_->Update(dt);
    // ===== 剣追従（スキンのボーンから取る）=====
    if (model_ && swordObj_) {

        Matrix4x4 handW = model_->GetJointWorldMatrix("RightHand");

        // 手の位置（あなたの行列は translation が m[3][0..2]）
        Vector3 handPos{ handW.m[3][0], handW.m[3][1], handW.m[3][2] };

        // 位置オフセット（手の中での微調整）
        Vector3 offset{ 0.0f, 0.0f, 0.0f };

        swordObj_->SetTranslate({
            handPos.x + offset.x,
            handPos.y + offset.y,
            handPos.z + offset.z
            });

        // （まず位置だけでOK。回転も合わせたいなら次で追加）
    }


    //static bool once = true;
    //if (once && swordObj_) {
    //    once = false;
    //    swordObj_->SetTranslate({ 5.0f, 0.0f, 15.0f }); // 明らかにズレる値
    //}

    if (swordObj_) {
        const auto& t = swordObj_->GetTranslate();
        OutputDebugStringA(std::format("[SwordObj] translate=({:.3f},{:.3f},{:.3f})\n", t.x, t.y, t.z).c_str());
    }


	if (swordObj_) swordObj_->Update(dt);
    if (debugAtkCube_) debugAtkCube_->Update(dt);

    // 被弾フラッシュ
    if (hitFlashSec_ > 0.0f) {
        hitFlashSec_ -= dt;
        if (hitFlashSec_ < 0.0f) hitFlashSec_ = 0.0f;
    }

    if (model_) {
        model_->SetMaterialColor((hitFlashSec_ > 0.0f) ? hitColor_ : normalColor_);
    }


    // ライト
    SetLighting(light_);

    OutputDebugStringA(("[PlayerAnim] " + curAnim_ + "\n").c_str());

#ifdef USE_IMGUI
    if (combo_) {
        combo_->DebugImGui();
    }
#endif


}



void Player::UpdateMove_(float /*dt*/, const Input& input) {
    // --- 左右（X） ---
    float mx = 0.0f;
    if (input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A))  mx -= 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D))  mx += 1.0f, isMoving = true;

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    // --- 奥行き（Z） ---
    float mz = 0.0f;
    if (input.IsKeyPressed(DIK_UP) || input.IsKeyPressed(DIK_W)) mz += 1.0f, isMoving = true; // 奥へ +Z
    if (input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S)) mz -= 1.0f, isMoving = true; // 手前へ -Z

    vel_.z = mz * depthSpeed_;


}

void Player::ApplyPhysics_(float dt) {
    // 重力（Yだけ）
    if (!onGround_) {
        vel_.y -= gravity_ * dt;
    }

    // 位置更新（X/Y/Z）
    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    // 地面（y=0）
    if (pos_.y <= 0.0f) {
        pos_.y = 0.0f;
        vel_.y = 0.0f;
        onGround_ = true;
    }

    // 奥行き制限
    const float zNear = -15.0f; // 手前（DIK_DOWNで行く側）
    const float zFar = 20.0f; // 奥（DIK_UPで行く側）
    pos_.z = std::clamp(pos_.z, zNear, zFar);

    // ★ Zに応じて X の範囲を変える
    const float xMaxNear = 15.0f; // 手前での左右幅（狭く）
    const float xMaxFar = 20.0f; // 奥での左右幅（広く）

    float t = (pos_.z - zNear) / (zFar - zNear); // 0:手前 → 1:奥
    t = std::clamp(t, 0.0f, 1.0f);

    // 線形補間（Lerp）
    float xMax = xMaxNear + (xMaxFar - xMaxNear) * t;

    // X制限
    pos_.x = std::clamp(pos_.x, -xMax, xMax);

}

void Player::Damage(int d) {
    if (dead_) return;
    hp_ -= d;
    if (hp_ <= 0) {
        hp_ = 0;
        dead_ = true;
    }
}

void Player::SetSpawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0,0,0 };
    onGround_ = true;

    UpdateBody_();
    UpdateModel_(); // 見た目も即反映
}

void Player::UpdateModel_() {
    if (!model_) return;

    // 位置
    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    // ★向き反転（Xスケールを反転）
    const float sx = (facing_ > 0) ? 1.0f : -1.0f;
    model_->SetScale({ sx, 1.0f, 1.0f });


}


void Player::Draw() {
    if (shadow_) shadow_->Draw();
    if (model_) model_->Draw();
    //if (swordObj_) swordObj_->Draw();
}

void Player::DrawDebugHitBoxes(EnemyManager& enemyMgr) {
    if (!combo_ || !debugAtkCube_) return;

    AABB3 hb{};
    if (combo_->GetDebugHitBox3(hb)) {
        // hb は center + half なので、そのまま
        debugAtkCube_->SetTranslate({ hb.x, hb.y, hb.z });

        // cube.obj は元が 2x2x2（-1..+1）
        // → scale = halfSize をそのまま入れれば一致
        debugAtkCube_->SetScale({ hb.hx, hb.hy, hb.hz });


        debugAtkCube_->Draw();
    }
}


void Player::UpdateBody_() {
    // ここはあなたの見た目サイズに合わせて調整
    const float hx = 0.4f;
    const float hy = 0.9f;
    const float hz = 0.6f;

    body_.min = { pos_.x - hx, pos_.y,         pos_.z - hz };
    body_.max = { pos_.x + hx, pos_.y + hy * 2,  pos_.z + hz };
}

void Player::AddHP(int heal) {
    hp_ += heal;
    if (hp_ > GetMaxHP()) hp_ = GetMaxHP();
}

void Player::SetLighting(const LightingParam& p)
{
    light_ = p;
    if (!model_) return;

    model_->SetEnableLighting(light_.lightingMode);

    model_->SetDirection(light_.dir);
    model_->SetIntensity(light_.dirIntensity);
    model_->SetLightColor(light_.dirColor);

    model_->SetPointLightPos(light_.pointPos);
    model_->SetPointLightIntensity(light_.pointIntensity);
    model_->SetPointLightColor(light_.pointColor);
    model_->SetPointLightRadius(light_.pointRadius);
    model_->SetPointLightDecay(light_.pointDecay);

    light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

    const float cosOuter = std::cosf(light_.spotAngleDeg * (std::numbers::pi_v<float> / 180.0f));
    const float cosInner = std::cosf(light_.spotFalloffStartDeg * (std::numbers::pi_v<float> / 180.0f));

    model_->SetSpotLightPos(light_.spotPos);
    model_->SetSpotLightDirection(light_.spotDir);
    model_->SetSpotLightIntensity(light_.spotIntensity);
    model_->SetSpotLightDistance(light_.spotDistance);
    model_->SetSpotLightDecay(light_.spotDecay);
    model_->SetSpotLightCosAngle(cosOuter);
    model_->SetSpotLightCosFalloffStart(cosInner);
    model_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });
    if (!swordObj_) return;

    swordObj_->SetEnableLighting(light_.lightingMode);

    swordObj_->SetDirection(light_.dir);
    swordObj_->SetIntensity(light_.dirIntensity);
    swordObj_->SetLightColor(light_.dirColor);

    swordObj_->SetPointLightPos(light_.pointPos);
    swordObj_->SetPointLightIntensity(light_.pointIntensity);
    swordObj_->SetPointLightColor(light_.pointColor);
    swordObj_->SetPointLightRadius(light_.pointRadius);
    swordObj_->SetPointLightDecay(light_.pointDecay);

    light_.spotFalloffStartDeg = std::min(light_.spotFalloffStartDeg, light_.spotAngleDeg - 0.1f);

  
    swordObj_->SetSpotLightPos(light_.spotPos);
    swordObj_->SetSpotLightDirection(light_.spotDir);
    swordObj_->SetSpotLightIntensity(light_.spotIntensity);
    swordObj_->SetSpotLightDistance(light_.spotDistance);
    swordObj_->SetSpotLightDecay(light_.spotDecay);
    swordObj_->SetSpotLightCosAngle(cosOuter);
    swordObj_->SetSpotLightCosFalloffStart(cosInner);
    swordObj_->SetSpotLightColor({ light_.spotColor.x, light_.spotColor.y, light_.spotColor.z, 1.0f });

}


void Player::ResetTitleAttackDemo()
{
    titleDemoTimer_ = 0.0f;
    titleDemoNextIsI_ = true;

    // 見た目もリセットしたければ
    if (model_) {
        model_->PlayAnimation("Idle", true);
        curAnim_ = "Idle";
    }
}

void Player::UpdateTitleAttackDemo(float dt, float intervalSec)
{
    if (!model_) return;

    // interval の安全策
    if (intervalSec < 0.05f) intervalSec = 0.05f;

    // まずアニメ時間を進める（←これ超重要）
    model_->Update(dt);

    // 攻撃アニメ中なら「終わるまで待つ」
    const bool inAttackAnim = (curAnim_ == "Attak_I" || curAnim_ == "Attak_O");
    if (inAttackAnim) {
        if (model_->IsAnimationFinished()) {
            model_->PlayAnimation("Idle", true);
            curAnim_ = "Idle";
        }
        return;
    }

    // 次の攻撃タイミング
    titleDemoTimer_ += dt;
    if (titleDemoTimer_ >= intervalSec) {
        titleDemoTimer_ = 0.0f;

        if (titleDemoNextIsI_) {
            model_->PlayAnimation("Attak_I", false);
            curAnim_ = "Attak_I";
        } else {
            model_->PlayAnimation("Attak_O", false);
            curAnim_ = "Attak_O";
        }
        titleDemoNextIsI_ = !titleDemoNextIsI_;
    }
}
void Player::SetTitleTransform(const Vector3& t, const Vector3& r, const Vector3& s)
{
    model_->SetTranslate(t);
    model_->SetRotate(r);
    model_->SetScale(s);
}
