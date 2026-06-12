#include "Player.h"
#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "Camera.h"

#include "EnemyManager.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

void Player::UpdateMove_(float /*dt*/, const Input& input) {
    float mx = 0.0f;
    if (input.IsKeyPressed(DIK_LEFT) || input.IsKeyPressed(DIK_A))  mx -= 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_RIGHT) || input.IsKeyPressed(DIK_D))  mx += 1.0f, isMoving = true;

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    float mz = 0.0f;
    if (input.IsKeyPressed(DIK_UP)) mz += 1.0f, isMoving = true;
    if (input.IsKeyPressed(DIK_DOWN) || input.IsKeyPressed(DIK_S)) mz -= 1.0f, isMoving = true;

    vel_.z = mz * depthSpeed_;


}

void Player::UpdateMove_(float /*dt*/, const PlayerInputCommand& command) {
    float mx = static_cast<float>(command.horizontal);
    mx = std::clamp(mx, -1.0f, 1.0f);

    if (mx < -0.1f) facing_ = -1;
    if (mx > +0.1f) facing_ = +1;

    vel_.x = mx * moveSpeed_;

    float mz = static_cast<float>(command.depth);
    mz = std::clamp(mz, -1.0f, 1.0f);
    vel_.z = mz * depthSpeed_;

    isMoving = std::abs(mx) > 0.1f || std::abs(mz) > 0.1f;
}

void Player::ApplyPhysics_(float dt) {
    if (!onGround_) {
        vel_.y -= gravity_ * dt;
    }

    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    if (pos_.y <= 0.0f) {
        pos_.y = 0.0f;
        vel_.y = 0.0f;
        onGround_ = true;
        jumpCount_ = 0;
    }

    const float zNear = -15.0f;
    const float zFar = 20.0f;
    pos_.z = std::clamp(pos_.z, zNear, zFar);

}

void Player::Damage(int d) {
    if (dead_) return;
    hp_ -= d;
    if (hp_ <= 0) {
        hp_ = 0;
        dead_ = true;
    }
}

void Player::AddDamagePercent(float damagePercent) {
    damagePercent_ = std::max(0.0f, damagePercent_ + damagePercent);
}

void Player::SetDamagePercent(float damagePercent) {
    damagePercent_ = std::max(0.0f, damagePercent);
}

void Player::ApplyLaunch(const Vector3& velocity, float hitStunSec) {
    vel_ = velocity;
    onGround_ = false;
    launched_ = true;
    launchedTimer_ = std::max(0.0f, hitStunSec);
    action_ = PlayerAction::Launched;
    attackType_ = PlayerAttackType::None;
    guarding_ = false;
    crouching_ = false;
    fastFalling_ = false;
    moveLockSec_ = std::max(moveLockSec_, hitStunSec);
}

void Player::ApplyBossHit(float damagePercent, float baseKnockback, float knockbackScale, const Vector3& knockbackDir, float hitStunSec) {
    const float knockbackPercent = damagePercent_;
    AddDamagePercent(damagePercent);

    Vector3 dir = knockbackDir;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len > 1.0e-6f) {
        dir.x /= len;
        dir.y /= len;
        dir.z /= len;
    } else {
        dir = { 1.0f, 0.35f, 0.0f };
    }

    const float power = baseKnockback + knockbackPercent * knockbackScale;
    ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, hitStunSec);
    TriggerHitFlash(0.25f);
}

void Player::SetHP(int hp) {
    hp_ = std::clamp(hp, 0, GetMaxHP());
    dead_ = hp_ <= 0;
}

void Player::SetSpawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0,0,0 };
    onGround_ = true;
    jumpCount_ = 0;
    launched_ = false;
    launchedTimer_ = 0.0f;

    UpdateBody_();
    UpdateModel_();
}

void Player::SetDropRespawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0.0f, 0.0f, 0.0f };
    onGround_ = false;
    jumpCount_ = 0;
    launched_ = false;
    launchedTimer_ = 0.0f;
    moveLockSec_ = 0.0f;
    action_ = PlayerAction::Jump;
    attackType_ = PlayerAttackType::None;

    UpdateBody_();
    UpdateModel_();
}

void Player::UpdateModel_() {
    if (!model_) return;

    model_->SetTranslate({ pos_.x, pos_.y, pos_.z });

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

