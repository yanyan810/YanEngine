#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"

#include <algorithm>
#include <format>
#include <numbers>

void Player::UpdateMove_(float /*dt*/, const Input& input) {
    // --- 蟾ｦ蜿ｳ・・・・---
    float mx = 0.0f;
    if (input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A))  mx -= 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D))  mx += 1.0f, isMoving = true;

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    // --- 螂･陦後″・・・・---
    float mz = 0.0f;
    if (input.IsKeyPressed(DIK_UP) || input.IsKeyPressed(DIK_W)) mz += 1.0f, isMoving = true; // 螂･縺ｸ +Z
    if (input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S)) mz -= 1.0f, isMoving = true; // 謇句燕縺ｸ -Z

    vel_.z = mz * depthSpeed_;


}

void Player::ApplyPhysics_(float dt) {
    // 驥榊鴨・・縺縺托ｼ・
    if (!onGround_) {
        vel_.y -= gravity_ * dt;
    }

    // 菴咲ｽｮ譖ｴ譁ｰ・・/Y/Z・・
    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    // 蝨ｰ髱｢・・=0・・
    if (pos_.y <= 0.0f) {
        pos_.y = 0.0f;
        vel_.y = 0.0f;
        onGround_ = true;
    }

    // 螂･陦後″蛻ｶ髯・
    const float zNear = -15.0f; // 謇句燕・・IK_DOWN縺ｧ陦後￥蛛ｴ・・
    const float zFar = 20.0f; // 螂･・・IK_UP縺ｧ陦後￥蛛ｴ・・
    pos_.z = std::clamp(pos_.z, zNear, zFar);

    // 笘・Z縺ｫ蠢懊§縺ｦ X 縺ｮ遽・峇繧貞､峨∴繧・
    const float xMaxNear = 15.0f; // 謇句燕縺ｧ縺ｮ蟾ｦ蜿ｳ蟷・ｼ育強縺擾ｼ・
    const float xMaxFar = 20.0f; // 螂･縺ｧ縺ｮ蟾ｦ蜿ｳ蟷・ｼ亥ｺ・￥・・

    float t = (pos_.z - zNear) / (zFar - zNear); // 0:謇句燕 竊・1:螂･
    t = std::clamp(t, 0.0f, 1.0f);

    // 邱壼ｽ｢陬憺俣・・erp・・
    float xMax = xMaxNear + (xMaxFar - xMaxNear) * t;

    // X蛻ｶ髯・
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
    UpdateModel_(); // 隕九◆逶ｮ繧ょ叉蜿肴丐
}

void Player::UpdateModel_() {
    if (!model_) return;

    // 菴咲ｽｮ
    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

    // 笘・髄縺榊渚霆｢・・繧ｹ繧ｱ繝ｼ繝ｫ繧貞渚霆｢・・
    const float sx = (facing_ > 0) ? 1.0f : -1.0f;
    model_->SetScale({ sx, 1.0f, 1.0f });


}


void Player::Draw() {
    if (shadow_) shadow_->Draw();
    if (model_) model_->Draw();
    if (swordObj_) swordObj_->Draw();
}

void Player::DrawDebugHitBoxes(EnemyManager& enemyMgr) {
    (void)enemyMgr;
    if (!debugAtkCube_) return;

    Vector3 center{};
    Vector3 halfSize{};
    if (!GetAttackDebugHitBox_(center, halfSize)) return;

    debugAtkCube_->SetTranslate(center);
    debugAtkCube_->SetScale(halfSize);
    debugAtkCube_->Update(0.0f);
    debugAtkCube_->Draw();
}


void Player::UpdateBody_() {
    // 縺薙％縺ｯ縺ゅ↑縺溘・隕九◆逶ｮ繧ｵ繧､繧ｺ縺ｫ蜷医ｏ縺帙※隱ｿ謨ｴ
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

