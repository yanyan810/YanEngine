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
    if (isGrabbed_) {
        vel_ = { 0.0f, 0.0f, 0.0f };
        return;
    }

    if (!onGround_) {
        vel_.y -= gravity_ * dt;

        // 吹っ飛び中：XZ速度にイーズアウト減衰をかける（疾走感 → だんだん遅く）
        if (launched_) {
            float drag = 1.0f;
            if (launchDragUseTime_) {
                // 残り時間の割合 (1.0 -> 0.0)
                const float timeRatio = (launchedTotalTime_ > 0.0f) ? (launchedTimer_ / launchedTotalTime_) : 0.0f;
                // 100%〜Threshold%までは減衰が小さく(DragHigh)、それ未満になったら急激に減衰(DragLow)
                if (timeRatio >= launchDragThreshold_) {
                    drag = launchXZDragHigh_;
                } else {
                    drag = launchXZDragLow_;
                }
            } else {
                // 速度割合で判定
                const float speed = std::sqrt(vel_.x * vel_.x + vel_.y * vel_.y + vel_.z * vel_.z);
                const float speedRatio = (launchInitialSpeed_ > 1.0e-4f) ? (speed / launchInitialSpeed_) : 0.0f;
                if (speedRatio >= launchDragThreshold_) {
                    drag = launchXZDragHigh_;
                } else {
                    drag = launchXZDragLow_;
                }
            }

            if (drag < 1.0f) {
                const float dragMul = std::pow(drag, dt);
                vel_.x *= dragMul;
                vel_.z *= dragMul;
            }
        }
    }

    pos_.x += vel_.x * dt;
    pos_.y += vel_.y * dt;
    pos_.z += vel_.z * dt;

    if (pos_.y <= 0.0f) {
        pos_.y = 0.0f;
        if (launched_ && vel_.y < -launchBounceMinSpeed_) {
            vel_.y = -vel_.y * launchBounceRestitution_;
            vel_.x *= launchBounceFriction_;
            vel_.z *= launchBounceFriction_;
            onGround_ = false;
        } else {
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
            if (launched_) {
                launched_ = false;
                launchedTimer_ = 0.0f;
                launchedTotalTime_ = 0.0f;
                launchInitialSpeed_ = 0.0f;
                launchActionSpeedRatio_ = 0.0f;
                launchControlUnlocked_ = false;
                action_ = PlayerAction::Idle;
            }
        }
    }

    const float zNear = -15.0f;
    const float zFar = 20.0f;
    if (pos_.z < zNear) {
        pos_.z = zNear;
        if (launched_ && vel_.z < -launchBounceMinSpeed_) {
            vel_.z = -vel_.z * launchBounceRestitution_;
        } else {
            vel_.z = 0.0f;
        }
    } else if (pos_.z > zFar) {
        pos_.z = zFar;
        if (launched_ && vel_.z > launchBounceMinSpeed_) {
            vel_.z = -vel_.z * launchBounceRestitution_;
        } else {
            vel_.z = 0.0f;
        }
    }

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

void Player::ApplyLaunch(const Vector3& velocity, float hitStunSec, float actionSpeedRatio) {
    vel_ = velocity;
    onGround_ = false;
    launched_ = true;
    launchedTimer_ = std::max(0.0f, hitStunSec);
    launchedTotalTime_ = launchedTimer_;
    launchInitialSpeed_ = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    launchActionSpeedRatio_ = std::clamp(actionSpeedRatio, 0.0f, 1.0f);
    launchControlUnlocked_ = false;
    action_ = PlayerAction::Launched;
    attackType_ = PlayerAttackType::None;
    guarding_ = false;
    crouching_ = false;
    fastFalling_ = false;
    moveLockSec_ = std::max(moveLockSec_, hitStunSec);
}

void Player::ApplyBossHit(float damagePercent, float baseKnockback, float knockbackScale, const Vector3& knockbackDir, float hitStunSec, float actionSpeedRatio) {
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
    ApplyLaunch({ dir.x * power, dir.y * power, dir.z * power }, hitStunSec, actionSpeedRatio);
    TriggerHitFlash(0.25f);
}

void Player::SetHP(int hp) {
    hp_ = std::clamp(hp, 0, GetMaxHP());
    dead_ = hp_ <= 0;
}

void Player::SetPos(const Vector3& p) {
    pos_ = p;
    UpdateBody_();
    UpdateModel_();
}

void Player::SetSpawnPos(const Vector3& p) {
    pos_ = p;
    vel_ = { 0.0f, 0.0f, 0.0f };
    onGround_ = true;
    jumpCount_ = 0;
    launched_ = false;
    launchedTimer_ = 0.0f;
    launchedTotalTime_ = 0.0f;
    launchInitialSpeed_ = 0.0f;
    launchActionSpeedRatio_ = 0.0f;
    launchControlUnlocked_ = false;

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
    launchedTotalTime_ = 0.0f;
    launchInitialSpeed_ = 0.0f;
    launchActionSpeedRatio_ = 0.0f;
    launchControlUnlocked_ = false;
    moveLockSec_ = 0.0f;
    action_ = PlayerAction::Jump;
    attackType_ = PlayerAttackType::None;

    UpdateBody_();
    UpdateModel_();
}

bool Player::ResolveGroundAABB(const AABB& ground) {
    UpdateBody_();

    const bool overlapX = body_.max.x >= ground.min.x && body_.min.x <= ground.max.x;
    const bool overlapZ = body_.max.z >= ground.min.z && body_.min.z <= ground.max.z;
    if (!overlapX || !overlapZ) {
        return false;
    }

    const float groundTopY = ground.max.y;
    if (pos_.y <= groundTopY && vel_.y <= 0.0f) {
        pos_.y = groundTopY;
        vel_.y = 0.0f;
        onGround_ = true;
        jumpCount_ = 0;
        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        return true;
    }

    return false;
}

bool Player::ResolveGroundAABB(const std::vector<AABB>& grounds) {
    UpdateBody_();

    const AABB* bestGround = nullptr;
    float highestGroundY = -FLT_MAX;

    for (const auto& ground : grounds) {
        const bool overlapX = body_.max.x >= ground.min.x && body_.min.x <= ground.max.x;
        const bool overlapZ = body_.max.z >= ground.min.z && body_.min.z <= ground.max.z;
        if (!overlapX || !overlapZ) {
            continue;
        }

        const float groundTopY = ground.max.y;
        if (pos_.y <= groundTopY + 0.1f && vel_.y <= 0.0f) {
            if (groundTopY > highestGroundY) {
                highestGroundY = groundTopY;
                bestGround = &ground;
            }
        }
    }

    if (bestGround) {
        pos_.y = highestGroundY;
        vel_.y = 0.0f;
        onGround_ = true;
        jumpCount_ = 0;
        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        return true;
    }

    return false;
}

bool Player::ResolveObstaclesAABB(const std::vector<AABB>& obstacles) {
    UpdateBody_();

    // 地面（Y=0）より上にいる場合は、衝突解決前に一旦空中判定にする
    // 障害物の上に乗っていれば、ループ内で onGround_ = true に設定される
    if (pos_.y > 0.0f) {
        onGround_ = false;
    }

    bool resolved = false;

    for (const auto& obstacle : obstacles) {
        // 衝突判定
        const bool overlapX = body_.max.x > obstacle.min.x && body_.min.x < obstacle.max.x;
        const bool overlapY = body_.max.y > obstacle.min.y && body_.min.y < obstacle.max.y;
        const bool overlapZ = body_.max.z > obstacle.min.z && body_.min.z < obstacle.max.z;
        if (!overlapX || !overlapY || !overlapZ) {
            continue;
        }

        // 各軸の食い込み量を計算
        float overlapXDist = std::min(body_.max.x - obstacle.min.x, obstacle.max.x - body_.min.x);
        float overlapYDist = std::min(body_.max.y - obstacle.min.y, obstacle.max.y - body_.min.y);
        float overlapZDist = std::min(body_.max.z - obstacle.min.z, obstacle.max.z - body_.min.z);

        // 押し戻し方向の決定
        float pushX = (pos_.x < (obstacle.min.x + obstacle.max.x) * 0.5f) ? -overlapXDist : overlapXDist;
        float pushY = (pos_.y + (body_.max.y - body_.min.y) * 0.5f < (obstacle.min.y + obstacle.max.y) * 0.5f) ? -overlapYDist : overlapYDist;
        float pushZ = (pos_.z < (obstacle.min.z + obstacle.max.z) * 0.5f) ? -overlapZDist : overlapZDist;

        // 接地可能かどうかの判定（落下中かつ上方向への押し戻し）
        bool canLand = (vel_.y <= 0.0f) && (pushY > 0.0f) && (overlapYDist < (body_.max.y - body_.min.y) * 0.8f);

        // 最も食い込みが浅い軸で押し戻す（接地可能なら少しの猶予付きで優先）
        float minOverlap = std::min({overlapXDist, overlapYDist, overlapZDist});

        if (launched_) {
            if (minOverlap == overlapYDist) {
                pos_.y += pushY;
                if (pushY > 0.0f) {
                    if (vel_.y < -launchBounceMinSpeed_) {
                        vel_.y = -vel_.y * launchBounceRestitution_;
                        vel_.x *= launchBounceFriction_;
                        vel_.z *= launchBounceFriction_;
                        onGround_ = false;
                    } else {
                        vel_.y = 0.0f;
                        onGround_ = true;
                        jumpCount_ = 0;
                        launched_ = false;
                        launchedTimer_ = 0.0f;
                        launchedTotalTime_ = 0.0f;
                        launchInitialSpeed_ = 0.0f;
                        launchActionSpeedRatio_ = 0.0f;
                        launchControlUnlocked_ = false;
                        action_ = PlayerAction::Idle;
                    }
                } else if (vel_.y > 0.0f) {
                    // 天井ぶつけ
                    if (vel_.y > launchBounceMinSpeed_) {
                        vel_.y = -vel_.y * launchBounceRestitution_;
                        onGround_ = false;
                    } else {
                        vel_.y = 0.0f;
                    }
                }
            } else if (minOverlap == overlapXDist) {
                pos_.x += pushX;
                if (std::abs(vel_.x) > launchBounceMinSpeed_) {
                    vel_.x = std::abs(vel_.x) * (pushX < 0.0f ? -1.0f : 1.0f) * launchBounceRestitution_;
                    vel_.y *= launchBounceFriction_;
                    vel_.z *= launchBounceFriction_;
                } else {
                    vel_.x = 0.0f;
                }
            } else {
                pos_.z += pushZ;
                if (std::abs(vel_.z) > launchBounceMinSpeed_) {
                    vel_.z = std::abs(vel_.z) * (pushZ < 0.0f ? -1.0f : 1.0f) * launchBounceRestitution_;
                    vel_.x *= launchBounceFriction_;
                    vel_.y *= launchBounceFriction_;
                } else {
                    vel_.z = 0.0f;
                }
            }
        } else if (canLand && (overlapYDist <= overlapXDist + 0.2f && overlapYDist <= overlapZDist + 0.2f)) {
            // 接地（着地）
            pos_.y += pushY;
            vel_.y = 0.0f;
            onGround_ = true;
            jumpCount_ = 0;
        } else {
            // 壁・天井などの衝突解決
            if (minOverlap == overlapYDist) {
                pos_.y += pushY;
                if (pushY < 0.0f) { // 天井頭ぶつけ
                    if (vel_.y > 0.0f) vel_.y = 0.0f;
                } else {
                    vel_.y = 0.0f;
                }
            } else if (minOverlap == overlapXDist) {
                pos_.x += pushX;
                vel_.x = 0.0f;
            } else {
                pos_.z += pushZ;
                vel_.z = 0.0f;
            }
        }

        UpdateBody_();
        UpdateModel_();
        if (model_) {
            model_->Update(0.0f);
        }
        resolved = true;
    }

    return resolved;
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

